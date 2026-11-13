#include "modbus_autobaud_capture.h"

#include "main.h"
#include "cmsis_os2.h"
#include "modbus_engine_port.h"
#include "modbus_engine_config.h"
#if MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE
#include "pyxis_autobaud_capture_stm32h563.h"
#endif

TIM_HandleTypeDef htim2;

static volatile modbus_autobaud_capture_result_t capture;
static volatile uint32_t last_capture;
static volatile uint32_t last_edge_tick;
static volatile uint8_t have_previous_edge;
static uint32_t capture_start_tick;

static void pc4_to_capture(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOC, &gpio);
}

static void pc4_to_usart3(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOC, &gpio);
}

static uint32_t timer_clock_hz(void)
{
    /* Pyxis currently runs APB1 with DIV1, so TIM2 receives PCLK1 directly.
     * Keep the value derived from RCC rather than baking in 32 MHz. */
    return HAL_RCC_GetPCLK1Freq();
}

bool ModbusAutobaudCapture_Start(void)
{
    TIM_IC_InitTypeDef ic = {0};

    if (capture.active != 0U) {
        return false;
    }

    /* The caller must be in task context. This also clears the parser frame
     * before ownership moves from USART3/DMA to TIM2/PC4. */
    ModbusEnginePort_ClearFrame();
    if (!ModbusEnginePort_UartEnable(false)) {
        return false;
    }

    capture.interval_count = 0U;
    capture.timer_frequency_hz = timer_clock_hz() / 3U;
    capture.overrun = 0U;
    capture.timed_out = 0U;
    capture.active = 0U;
    have_previous_edge = 0U;
    last_capture = 0U;
    last_edge_tick = HAL_GetTick();

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 2U;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFFU;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.RepetitionCounter = 0U;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_IC_Init(&htim2) != HAL_OK) {
        pc4_to_usart3();
        (void)ModbusEnginePort_UartEnable(true);
        return false;
    }

    ic.ICPolarity = TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter = 0U;
    if (HAL_TIM_IC_ConfigChannel(&htim2, &ic, TIM_CHANNEL_4) != HAL_OK) {
        (void)HAL_TIM_IC_DeInit(&htim2);
        pc4_to_usart3();
        (void)ModbusEnginePort_UartEnable(true);
        return false;
    }

    pc4_to_capture();
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    capture_start_tick = HAL_GetTick();
    if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_4) != HAL_OK) {
        (void)HAL_TIM_IC_DeInit(&htim2);
        pc4_to_usart3();
        (void)ModbusEnginePort_UartEnable(true);
        return false;
    }

    capture.active = 1U;
    return true;
}

bool ModbusAutobaudCapture_Service(uint32_t timeout_ms)
{
    if (capture.active == 0U) {
        return true;
    }

    /* Cooperative wait: capture itself is interrupt-driven, so this does not
     * consume the Modbus task's CPU time while waiting for bus edges. */
    (void)osDelay(1U);
    if ((capture.interval_count >= MODBUS_AUTOBAUD_CAPTURE_MAX_INTERVALS) ||
        ((capture.interval_count >= 8U) &&
         ((HAL_GetTick() - last_edge_tick) >= 5U)) ||
        ((HAL_GetTick() - capture_start_tick) >= timeout_ms)) {
        if (capture.interval_count < MODBUS_AUTOBAUD_CAPTURE_MAX_INTERVALS) {
            capture.timed_out = 1U;
        }
        (void)ModbusAutobaudCapture_Stop();
        return true;
    }
    return false;
}

bool ModbusAutobaudCapture_Stop(void)
{
    bool ok = true;

    if (capture.active != 0U) {
        if (HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_4) != HAL_OK) {
            ok = false;
        }
    }
    capture.active = 0U;
    (void)HAL_TIM_IC_DeInit(&htim2);
    pc4_to_usart3();
    if (!ModbusEnginePort_UartEnable(true)) {
        ok = false;
    }
    return ok;
}

bool ModbusAutobaudCapture_IsActive(void)
{
    return capture.active != 0U;
}

uint32_t ModbusAutobaudCapture_GetTimerFrequencyHz(void)
{
    return capture.timer_frequency_hz;
}

bool ModbusAutobaudCapture_GetResult(modbus_autobaud_capture_result_t *result)
{
    if (result == NULL) {
        return false;
    }
    *result = capture;
    return true;
}

void ModbusAutobaudCapture_OnCaptureIrq(void)
{
    uint32_t timestamp;
    uint32_t interval;

    if (capture.active == 0U) {
        return;
    }

    timestamp = HAL_TIM_ReadCapturedValue(&htim2, TIM_CHANNEL_4);
    last_edge_tick = HAL_GetTick();
    if (have_previous_edge == 0U) {
        last_capture = timestamp;
        have_previous_edge = 1U;
        return;
    }

    interval = timestamp - last_capture;
    last_capture = timestamp;

    /* Reject only impossible/glitch-sized intervals. Scoring is deferred to
     * Milestone B and therefore is not performed in interrupt context. */
    if (interval < 4U) {
        return;
    }
    if (capture.interval_count >= MODBUS_AUTOBAUD_CAPTURE_MAX_INTERVALS) {
        capture.overrun = 1U;
        return;
    }
    capture.interval_ticks[capture.interval_count++] = interval;
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if ((htim != NULL) && (htim->Instance == TIM2) &&
        (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_4)) {
#if MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE
        if (PyxisAutobaudCapture_IsActive()) {
            PyxisAutobaudCapture_OnCaptureIrq();
            return;
        }
#endif
        ModbusAutobaudCapture_OnCaptureIrq();
    }
}
