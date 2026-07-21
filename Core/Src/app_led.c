#include "app_led.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "app_led_config.h"
#include "stm32h5xx_hal.h"

typedef enum
{
    LED_ENGINE_BASE_PATTERN = 0,
    LED_ENGINE_SLAVE_ID_RED_DIGIT,
    LED_ENGINE_SLAVE_ID_DIGIT_SEPARATOR,
    LED_ENGINE_SLAVE_ID_GREEN_DIGIT,
    LED_ENGINE_STAGE_SEPARATOR_AFTER_SLAVE_ID,
    LED_ENGINE_BAUD_DIGIT,
    LED_ENGINE_STAGE_SEPARATOR_AFTER_BAUD,
    LED_ENGINE_PARITY_DIGIT,
    LED_ENGINE_STAGE_SEPARATOR_AFTER_PARITY,
    LED_ENGINE_SEQUENCE_END,
    LED_ENGINE_IDENTIFY
} led_engine_mode_t;

typedef enum
{
    LED_DIGIT_PHASE_IDLE = 0,
    LED_DIGIT_PHASE_ON,
    LED_DIGIT_PHASE_OFF_BETWEEN,
    LED_DIGIT_PHASE_SEPARATOR
} led_digit_phase_t;

typedef struct
{
    led_base_state_t requested_state;
    led_engine_mode_t engine_mode;
    uint32_t frame_time_ms;
    uint32_t step_time_ms;
    uint32_t last_update_tick_ms;
    bool fatal_fault_active;
    bool device_fault_active;
    bool config_invalid_active;
    bool general_warning_active;
    bool comm_warning_active;
    uint32_t green_event_timer_ms;
    uint32_t red_event_timer_ms;
    uint32_t last_green_event_ms;
    uint32_t last_red_event_ms;
    uint8_t slave_id;
    uint8_t baud_display;
    uint8_t parity_display;
    uint8_t repeat_remaining;
    uint8_t current_digit;
    uint8_t digit_pulses_remaining;
    uint32_t digit_phase_elapsed_ms;
    led_digit_phase_t digit_phase;
    uint32_t identify_remaining_ms;
    bool startup_lock_active;
} led_context_t;

typedef struct
{
    uint32_t pulse_on_ms;
    uint32_t pulse_off_ms;
    uint32_t zero_on_ms;
    uint32_t digit_separator_ms;
    uint32_t stage_separator_ms;
    uint32_t end_separator_ms;
} led_digit_timing_t;

static led_context_t led_ctx;
static const led_digit_timing_t k_led_startup_timing = {
    .pulse_on_ms = LED_STARTUP_PULSE_ON_MS,
    .pulse_off_ms = LED_STARTUP_PULSE_OFF_MS,
    .zero_on_ms = LED_STARTUP_ZERO_ON_MS,
    .digit_separator_ms = LED_STARTUP_DIGIT_SEPARATOR_MS,
    .stage_separator_ms = LED_STARTUP_STAGE_SEPARATOR_MS,
    .end_separator_ms = LED_STARTUP_END_SEPARATOR_MS
};

static void LED_HW_Write(GPIO_TypeDef *port,
                         uint16_t pin,
                         led_gpio_active_level_t active_level,
                         bool on);
static void LED_HW_SetGreen(bool on);
static void LED_HW_SetRed(bool on);
static void LED_UpdateTimers(uint32_t elapsed_ms);
static led_base_state_t LED_ResolveBaseState(void);
static bool LED_IsSpecialEngineActive(void);
static void LED_ServiceElapsed(uint32_t elapsed_ms);
static void LED_RunBasePattern(led_base_state_t state, bool *green, bool *red);
static void LED_RunSpecialEngine(bool *green, bool *red);
static void LED_ApplyEventOverlays(bool *green, bool *red);
static bool LED_RunDigitPhase(bool use_green,
                              bool use_red,
                              bool *green,
                              bool *red,
                              bool *digit_complete);
static uint8_t LED_HighHexDigit(uint8_t value);
static uint8_t LED_LowHexDigit(uint8_t value);
static uint32_t LED_DigitDurationMs(uint8_t digit, const led_digit_timing_t *timing);
static void LED_StartSlaveIdSequence(uint8_t slave_id);
static void LED_BeginDigitMode(led_engine_mode_t mode, uint8_t digit);
static void LED_ResetTransientState(void);

void LED_Init(void)
{
    memset(&led_ctx, 0, sizeof(led_ctx));
    led_ctx.requested_state = LED_STATE_NO_POLLING_YET;
    led_ctx.last_update_tick_ms = HAL_GetTick();
    LED_HW_SetGreen(false);
    LED_HW_SetRed(false);
}

void LED_Task_10ms(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed_ms;

    elapsed_ms = now - led_ctx.last_update_tick_ms;
    if (elapsed_ms == 0U)
    {
        return;
    }
    led_ctx.last_update_tick_ms = now;

    taskENTER_CRITICAL();
    LED_ServiceElapsed(elapsed_ms);
    taskEXIT_CRITICAL();
}

void LED_Task_1ms_ISR(void)
{
    LED_ServiceElapsed(1U);
}

void LED_SetBaseState(led_base_state_t state)
{
    taskENTER_CRITICAL();
    led_ctx.requested_state = state;
    taskEXIT_CRITICAL();
}

void LED_RequestSlaveIdDisplay(uint8_t slave_id, uint8_t repeat_count)
{
    if ((slave_id < MODBUS_SLAVE_ID_MIN) || (slave_id > MODBUS_SLAVE_ID_MAX) || (repeat_count == 0U))
    {
        LED_SetConfigurationInvalid(true);
        return;
    }

    taskENTER_CRITICAL();
    if (led_ctx.startup_lock_active)
    {
        taskEXIT_CRITICAL();
        return;
    }
    led_ctx.slave_id = slave_id;
    led_ctx.baud_display = 0U;
    led_ctx.parity_display = 0U;
    led_ctx.repeat_remaining = repeat_count;
    LED_StartSlaveIdSequence(slave_id);
    taskEXIT_CRITICAL();
}

void LED_RequestStartupDisplay(uint8_t slave_id, uint8_t baud_display, uint8_t parity_display, uint8_t repeat_count)
{
    if ((slave_id < MODBUS_SLAVE_ID_MIN) || (slave_id > MODBUS_SLAVE_ID_MAX) ||
        (repeat_count == 0U) || (baud_display > 4U) || (parity_display == 0U) || (parity_display > 4U))
    {
        LED_SetConfigurationInvalid(true);
        return;
    }

    taskENTER_CRITICAL();
    led_ctx.slave_id = slave_id;
    led_ctx.baud_display = baud_display;
    led_ctx.parity_display = parity_display;
    led_ctx.repeat_remaining = repeat_count;
    led_ctx.startup_lock_active = true;
    LED_ResetTransientState();
    LED_StartSlaveIdSequence(slave_id);
    taskEXIT_CRITICAL();
}

uint32_t LED_GetStartupDisplayDurationMs(uint8_t slave_id, uint8_t baud_display, uint8_t parity_display, uint8_t repeat_count)
{
    uint32_t single_sequence_ms;
    uint8_t high_digit;

    if ((slave_id < MODBUS_SLAVE_ID_MIN) || (slave_id > MODBUS_SLAVE_ID_MAX) ||
        (repeat_count == 0U) || (baud_display > 4U) || (parity_display == 0U) || (parity_display > 4U))
    {
        return 0U;
    }

    high_digit = LED_HighHexDigit(slave_id);
    single_sequence_ms = LED_DigitDurationMs(LED_LowHexDigit(slave_id), &k_led_startup_timing) +
                         k_led_startup_timing.stage_separator_ms +
                         k_led_startup_timing.digit_separator_ms +
                         LED_DigitDurationMs(baud_display, &k_led_startup_timing) +
                         k_led_startup_timing.stage_separator_ms +
                         k_led_startup_timing.digit_separator_ms +
                         LED_DigitDurationMs(parity_display, &k_led_startup_timing) +
                         k_led_startup_timing.stage_separator_ms +
                         k_led_startup_timing.end_separator_ms;

    if (high_digit != 0U)
    {
        single_sequence_ms += LED_DigitDurationMs(high_digit, &k_led_startup_timing) +
                              k_led_startup_timing.digit_separator_ms;
    }

    return single_sequence_ms * repeat_count;
}

void LED_RequestIdentify(uint32_t duration_ms)
{
    taskENTER_CRITICAL();
    if (led_ctx.startup_lock_active)
    {
        taskEXIT_CRITICAL();
        return;
    }
    led_ctx.identify_remaining_ms = duration_ms;
    led_ctx.engine_mode = LED_ENGINE_IDENTIFY;
    led_ctx.step_time_ms = 0U;
    taskEXIT_CRITICAL();
}

void LED_NotifyValidModbusTransaction(void)
{
    uint32_t now = HAL_GetTick();

    taskENTER_CRITICAL();
    if ((now - led_ctx.last_green_event_ms) >= GREEN_ACTIVITY_MIN_INTERVAL_MS)
    {
        led_ctx.green_event_timer_ms = LED_ACTIVITY_PULSE_MS;
        led_ctx.last_green_event_ms = now;
    }
    taskEXIT_CRITICAL();
}

void LED_NotifyCommunicationError(void)
{
    uint32_t now = HAL_GetTick();

    taskENTER_CRITICAL();
    if ((now - led_ctx.last_red_event_ms) >= RED_ERROR_MIN_INTERVAL_MS)
    {
        led_ctx.red_event_timer_ms = LED_ERROR_PULSE_MS;
        led_ctx.last_red_event_ms = now;
    }
    taskEXIT_CRITICAL();
}

void LED_SetFatalFault(bool active)
{
    taskENTER_CRITICAL();
    led_ctx.fatal_fault_active = active;
    taskEXIT_CRITICAL();
}

void LED_SetDeviceFault(bool active)
{
    taskENTER_CRITICAL();
    led_ctx.device_fault_active = active;
    taskEXIT_CRITICAL();
}

void LED_SetConfigurationInvalid(bool active)
{
    taskENTER_CRITICAL();
    led_ctx.config_invalid_active = active;
    taskEXIT_CRITICAL();
}

void LED_SetGeneralWarning(bool active)
{
    taskENTER_CRITICAL();
    led_ctx.general_warning_active = active;
    taskEXIT_CRITICAL();
}

void LED_SetCommunicationWarning(bool active)
{
    taskENTER_CRITICAL();
    led_ctx.comm_warning_active = active;
    taskEXIT_CRITICAL();
}

static void LED_HW_Write(GPIO_TypeDef *port,
                         uint16_t pin,
                         led_gpio_active_level_t active_level,
                         bool on)
{
    GPIO_PinState state;

    if (active_level == LED_GPIO_ACTIVE_HIGH)
    {
        state = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    else
    {
        state = on ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }

    HAL_GPIO_WritePin(port, pin, state);
}

static void LED_HW_SetGreen(bool on)
{
    LED_HW_Write(LED_GREEN_GPIO_Port, LED_GREEN_Pin, LED_GREEN_ACTIVE_LEVEL, on);
}

static void LED_HW_SetRed(bool on)
{
    LED_HW_Write(LED_RED_GPIO_Port, LED_RED_Pin, LED_RED_ACTIVE_LEVEL, on);
}

static void LED_UpdateTimers(uint32_t elapsed_ms)
{
    led_ctx.frame_time_ms += elapsed_ms;
    led_ctx.step_time_ms += elapsed_ms;
    led_ctx.digit_phase_elapsed_ms += elapsed_ms;

    if (led_ctx.green_event_timer_ms > elapsed_ms)
    {
        led_ctx.green_event_timer_ms -= elapsed_ms;
    }
    else
    {
        led_ctx.green_event_timer_ms = 0U;
    }

    if (led_ctx.red_event_timer_ms > elapsed_ms)
    {
        led_ctx.red_event_timer_ms -= elapsed_ms;
    }
    else
    {
        led_ctx.red_event_timer_ms = 0U;
    }

    if (led_ctx.identify_remaining_ms > elapsed_ms)
    {
        led_ctx.identify_remaining_ms -= elapsed_ms;
    }
    else
    {
        led_ctx.identify_remaining_ms = 0U;
        if (led_ctx.engine_mode == LED_ENGINE_IDENTIFY)
        {
            led_ctx.engine_mode = LED_ENGINE_BASE_PATTERN;
            led_ctx.step_time_ms = 0U;
        }
    }
}

static led_base_state_t LED_ResolveBaseState(void)
{
    if (led_ctx.fatal_fault_active)
    {
        return LED_STATE_FATAL_FAULT;
    }
    if (led_ctx.device_fault_active)
    {
        return LED_STATE_DEVICE_FAULT;
    }
    if (led_ctx.config_invalid_active)
    {
        return LED_STATE_CONFIG_INVALID;
    }
    if (led_ctx.comm_warning_active)
    {
        return LED_STATE_COMM_WARNING;
    }
    if (led_ctx.general_warning_active)
    {
        return LED_STATE_GENERAL_WARNING;
    }
    return led_ctx.requested_state;
}

static bool LED_IsSpecialEngineActive(void)
{
    return (led_ctx.engine_mode != LED_ENGINE_BASE_PATTERN);
}

static void LED_RunBasePattern(led_base_state_t state, bool *green, bool *red)
{
    uint16_t t = (uint16_t)(led_ctx.frame_time_ms % LED_STATUS_FRAME_MS);

    *green = false;
    *red = false;

    switch (state)
    {
        case LED_STATE_DEVICE_FAULT:
            *red = ((led_ctx.frame_time_ms % (LED_FAST_ON_MS + LED_FAST_OFF_MS)) < LED_FAST_ON_MS);
            break;

        case LED_STATE_CONFIG_INVALID:
            if ((t < LED_DOUBLE_ON_MS) ||
                ((t >= (LED_DOUBLE_ON_MS + LED_DOUBLE_OFF_MS)) &&
                 (t < (LED_DOUBLE_ON_MS + LED_DOUBLE_OFF_MS + LED_DOUBLE_ON_MS))))
            {
                *green = true;
                *red = true;
            }
            break;

        case LED_STATE_COMM_WARNING:
            if (t < LED_HEARTBEAT_ON_MS)
            {
                *green = true;
            }
            if (((t >= 700U) && (t < 900U)) ||
                ((t >= 1100U) && (t < 1300U)))
            {
                *red = true;
            }
            break;

        case LED_STATE_GENERAL_WARNING:
            if (t < LED_HEARTBEAT_ON_MS)
            {
                *green = true;
            }
            if ((t >= 750U) && (t < 1250U))
            {
                *red = true;
            }
            break;

        case LED_STATE_NETWORK_LOST:
            if ((t < LED_DOUBLE_ON_MS) ||
                ((t >= (LED_DOUBLE_ON_MS + LED_DOUBLE_OFF_MS)) &&
                 (t < (LED_DOUBLE_ON_MS + LED_DOUBLE_OFF_MS + LED_DOUBLE_ON_MS))))
            {
                *green = true;
            }
            break;

        case LED_STATE_NO_POLLING_YET:
            *green = (t < LED_SLOW_ON_MS);
            break;

        case LED_STATE_NORMAL_POLLING_ACTIVE:
            *green = (t < LED_HEARTBEAT_ON_MS);
            break;

        default:
            break;
    }
}

static void LED_ServiceElapsed(uint32_t elapsed_ms)
{
    bool green = false;
    bool red = false;
    led_base_state_t resolved;

    LED_UpdateTimers(elapsed_ms);
    if (led_ctx.startup_lock_active)
    {
        LED_RunSpecialEngine(&green, &red);
    }
    else
    {
        resolved = LED_ResolveBaseState();

        if (resolved == LED_STATE_FATAL_FAULT)
        {
            green = false;
            red = true;
        }
        else
        {
            if (LED_IsSpecialEngineActive())
            {
                LED_RunSpecialEngine(&green, &red);
            }
            else
            {
                LED_RunBasePattern(resolved, &green, &red);
            }

            LED_ApplyEventOverlays(&green, &red);
        }
    }

    LED_HW_SetGreen(green);
    LED_HW_SetRed(red);
}

static void LED_RunSpecialEngine(bool *green, bool *red)
{
    bool digit_complete = false;

    *green = false;
    *red = false;

    switch (led_ctx.engine_mode)
    {
        case LED_ENGINE_SLAVE_ID_RED_DIGIT:
            LED_RunDigitPhase(false, true, green, red, &digit_complete);
            if (digit_complete)
            {
                led_ctx.engine_mode = LED_ENGINE_SLAVE_ID_DIGIT_SEPARATOR;
                led_ctx.step_time_ms = 0U;
            }
            break;

        case LED_ENGINE_SLAVE_ID_DIGIT_SEPARATOR:
            if (led_ctx.step_time_ms >= k_led_startup_timing.digit_separator_ms)
            {
                LED_BeginDigitMode(LED_ENGINE_SLAVE_ID_GREEN_DIGIT, LED_LowHexDigit(led_ctx.slave_id));
            }
            break;

        case LED_ENGINE_SLAVE_ID_GREEN_DIGIT:
            LED_RunDigitPhase(true, false, green, red, &digit_complete);
            if (digit_complete)
            {
                led_ctx.engine_mode = led_ctx.startup_lock_active ? LED_ENGINE_STAGE_SEPARATOR_AFTER_SLAVE_ID : LED_ENGINE_SEQUENCE_END;
                led_ctx.step_time_ms = 0U;
            }
            break;

        case LED_ENGINE_STAGE_SEPARATOR_AFTER_SLAVE_ID:
            *green = true;
            *red = true;
            if (led_ctx.step_time_ms >= k_led_startup_timing.stage_separator_ms)
            {
                LED_BeginDigitMode(LED_ENGINE_BAUD_DIGIT, led_ctx.baud_display);
            }
            break;

        case LED_ENGINE_BAUD_DIGIT:
            LED_RunDigitPhase(true, false, green, red, &digit_complete);
            if (digit_complete)
            {
                led_ctx.engine_mode = LED_ENGINE_STAGE_SEPARATOR_AFTER_BAUD;
                led_ctx.step_time_ms = 0U;
            }
            break;

        case LED_ENGINE_STAGE_SEPARATOR_AFTER_BAUD:
            *green = true;
            *red = true;
            if (led_ctx.step_time_ms >= k_led_startup_timing.stage_separator_ms)
            {
                LED_BeginDigitMode(LED_ENGINE_PARITY_DIGIT, led_ctx.parity_display);
            }
            break;

        case LED_ENGINE_PARITY_DIGIT:
            LED_RunDigitPhase(true, false, green, red, &digit_complete);
            if (digit_complete)
            {
                led_ctx.engine_mode = LED_ENGINE_STAGE_SEPARATOR_AFTER_PARITY;
                led_ctx.step_time_ms = 0U;
            }
            break;

        case LED_ENGINE_STAGE_SEPARATOR_AFTER_PARITY:
            *green = true;
            *red = true;
            if (led_ctx.step_time_ms >= k_led_startup_timing.stage_separator_ms)
            {
                led_ctx.engine_mode = LED_ENGINE_SEQUENCE_END;
                led_ctx.step_time_ms = 0U;
            }
            break;

        case LED_ENGINE_SEQUENCE_END:
            if (led_ctx.step_time_ms >= k_led_startup_timing.end_separator_ms)
            {
                if (led_ctx.repeat_remaining > 1U)
                {
                    led_ctx.repeat_remaining--;
                    LED_ResetTransientState();
                    LED_StartSlaveIdSequence(led_ctx.slave_id);
                }
                else
                {
                    led_ctx.repeat_remaining = 0U;
                    led_ctx.startup_lock_active = false;
                    led_ctx.engine_mode = LED_ENGINE_BASE_PATTERN;
                    led_ctx.step_time_ms = 0U;
                }
            }
            break;

        case LED_ENGINE_IDENTIFY:
            if ((led_ctx.step_time_ms % (LED_FAST_ON_MS + LED_FAST_OFF_MS)) < LED_FAST_ON_MS)
            {
                *green = true;
            }
            else
            {
                *red = true;
            }
            break;

        default:
            led_ctx.engine_mode = LED_ENGINE_BASE_PATTERN;
            led_ctx.step_time_ms = 0U;
            break;
    }
}

static void LED_ApplyEventOverlays(bool *green, bool *red)
{
    if (led_ctx.startup_lock_active)
    {
        return;
    }

    if (led_ctx.red_event_timer_ms > 0U)
    {
        *green = false;
        *red = true;
        return;
    }

    if (led_ctx.green_event_timer_ms > 0U)
    {
        *green = true;
    }
}

static bool LED_RunDigitPhase(bool use_green,
                              bool use_red,
                              bool *green,
                              bool *red,
                              bool *digit_complete)
{
    *digit_complete = false;

    if (green == NULL || red == NULL)
    {
        *digit_complete = true;
        return false;
    }

    *green = false;
    *red = false;

    if (led_ctx.digit_phase == LED_DIGIT_PHASE_ON)
    {
        if (use_green)
        {
            *green = true;
        }
        if (use_red)
        {
            *red = true;
        }

        if (led_ctx.current_digit == 0U)
        {
            if (led_ctx.digit_phase_elapsed_ms >= k_led_startup_timing.zero_on_ms)
            {
                led_ctx.digit_phase = LED_DIGIT_PHASE_SEPARATOR;
                led_ctx.digit_phase_elapsed_ms = 0U;
            }
        }
        else if (led_ctx.digit_phase_elapsed_ms >= k_led_startup_timing.pulse_on_ms)
        {
            if (led_ctx.digit_pulses_remaining > 0U)
            {
                led_ctx.digit_pulses_remaining--;
            }

            led_ctx.digit_phase = (led_ctx.digit_pulses_remaining > 0U) ? LED_DIGIT_PHASE_OFF_BETWEEN
                                                                         : LED_DIGIT_PHASE_SEPARATOR;
            led_ctx.digit_phase_elapsed_ms = 0U;
        }

        return true;
    }

    if (led_ctx.digit_phase == LED_DIGIT_PHASE_OFF_BETWEEN)
    {
        if (led_ctx.digit_phase_elapsed_ms >= k_led_startup_timing.pulse_off_ms)
        {
            led_ctx.digit_phase = LED_DIGIT_PHASE_ON;
            led_ctx.digit_phase_elapsed_ms = 0U;
        }
        return false;
    }

    if (led_ctx.digit_phase == LED_DIGIT_PHASE_SEPARATOR)
    {
        if (led_ctx.digit_phase_elapsed_ms >= k_led_startup_timing.digit_separator_ms)
        {
            led_ctx.digit_phase = LED_DIGIT_PHASE_IDLE;
            led_ctx.digit_phase_elapsed_ms = 0U;
            *digit_complete = true;
        }
        return false;
    }

    *digit_complete = true;
    return false;
}

static uint8_t LED_HighHexDigit(uint8_t value)
{
    return (uint8_t)((value >> 4) & 0x0FU);
}

static uint8_t LED_LowHexDigit(uint8_t value)
{
    return (uint8_t)(value & 0x0FU);
}

static uint32_t LED_DigitDurationMs(uint8_t digit, const led_digit_timing_t *timing)
{
    if ((timing == NULL) || (timing->pulse_on_ms == 0U))
    {
        return 0U;
    }

    if (digit == 0U)
    {
        return timing->zero_on_ms + timing->digit_separator_ms;
    }

    return (((uint32_t)digit * (timing->pulse_on_ms + timing->pulse_off_ms)) - timing->pulse_off_ms) +
           timing->digit_separator_ms;
}

static void LED_StartSlaveIdSequence(uint8_t slave_id)
{
    uint8_t high_digit = LED_HighHexDigit(slave_id);

    if (high_digit == 0U)
    {
        LED_BeginDigitMode(LED_ENGINE_SLAVE_ID_GREEN_DIGIT, LED_LowHexDigit(slave_id));
    }
    else
    {
        LED_BeginDigitMode(LED_ENGINE_SLAVE_ID_RED_DIGIT, high_digit);
    }
}

static void LED_BeginDigitMode(led_engine_mode_t mode, uint8_t digit)
{
    led_ctx.current_digit = digit;
    led_ctx.engine_mode = mode;
    led_ctx.step_time_ms = 0U;
    led_ctx.digit_phase_elapsed_ms = 0U;
    led_ctx.digit_pulses_remaining = digit;
    led_ctx.digit_phase = LED_DIGIT_PHASE_ON;
}

static void LED_ResetTransientState(void)
{
    led_ctx.frame_time_ms = 0U;
    led_ctx.step_time_ms = 0U;
    led_ctx.current_digit = 0U;
    led_ctx.digit_pulses_remaining = 0U;
    led_ctx.digit_phase_elapsed_ms = 0U;
    led_ctx.digit_phase = LED_DIGIT_PHASE_IDLE;
    led_ctx.green_event_timer_ms = 0U;
    led_ctx.red_event_timer_ms = 0U;
    led_ctx.identify_remaining_ms = 0U;
    led_ctx.last_update_tick_ms = HAL_GetTick();
}
