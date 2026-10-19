#include "task_modbus.h"

#include <stddef.h>

#include "app_led.h"
#include "main.h"
#include "stm32h5xx_hal.h"
#include "mb.h"
#include "mbport.h"
#include "NonVolatileData.h"
#include "app_freertos.h"
#include "modbus_app_adapter.h"
#include "modbus_register_map.h"
#include "modbus_module.h"
#include "user_mb_app.h"
#include "vibration_modbus_adapter.h"
#include "vibration_module.h"
#include "modbus_engine_port.h"
#include "modbus_engine_map.h"
#include "modbus_engine_runtime.h"
#include "modbus_engine_config.h"
#include "modbus_engine_tx.h"
#include "modbus_autobaud_startup.h"
#include "modbus_autobaud_diag.h"

#if !MODBUS_ENGINE_ACTIVE
extern USHORT usSRegInBuf[S_REG_INPUT_NREGS];
extern USHORT usSRegHoldBuf[S_REG_HOLDING_NREGS];
#endif
extern UART_HandleTypeDef huart3;

static ModbusConfig_t cfg;
uint32_t g_modbus_stop_bits = UART_STOPBITS_1;
volatile uint32_t g_custom_uart_init_ok = 0U;
volatile uint32_t g_custom_uart_enable_ok = 0U;

#if !MODBUS_AUTOBAUD_ENABLE
static uint32_t uart3_autobaud_lock(uint32_t timeout_ms);
static uint32_t snap_modbus_rate(uint32_t measured);
#endif
static uint32_t modbus_stop_bits_from_code(uint16_t parity_code);
static void notify_vibration_config_changed(const VibConfig_t *old_cfg, const VibConfig_t *new_cfg);
static void modbus_task_process_one_frame(void);

#if !MODBUS_AUTOBAUD_ENABLE
static uint32_t uart3_autobaud_lock(uint32_t timeout_ms)
{
#ifdef USART_CR2_ABRMODE
    MODIFY_REG(huart3.Instance->CR2, USART_CR2_ABRMODE, 0x0U);
#endif
    SET_BIT(huart3.Instance->CR2, USART_CR2_ABREN);

    {
        uint32_t t0 = HAL_GetTick();

        for (;;) {
            uint32_t isr = huart3.Instance->ISR;

            if ((isr & USART_ISR_ABRF) != 0U) {
                uint32_t pclk;
                uint32_t brr;

                CLEAR_BIT(huart3.Instance->CR2, USART_CR2_ABREN);
                if ((isr & USART_ISR_RXNE_RXFNE) != 0U) {
                    (void)huart3.Instance->RDR;
                }

                pclk = HAL_RCC_GetPCLK1Freq();
                brr = huart3.Instance->BRR;
                return (brr != 0U) ? (pclk + (brr / 2U)) / brr : 0U;
            }

            if ((isr & USART_ISR_ABRE) != 0U) {
                CLEAR_BIT(huart3.Instance->CR2, USART_CR2_ABREN);
                return 0U;
            }

            if ((HAL_GetTick() - t0) > timeout_ms) {
                CLEAR_BIT(huart3.Instance->CR2, USART_CR2_ABREN);
                return 0U;
            }

            osDelay(1U);
        }
    }
}

static uint32_t snap_modbus_rate(uint32_t measured)
{
    static const uint32_t rates[] = {9600U, 19200U, 38400U, 115200U};
    uint32_t best = rates[0];
    uint32_t best_err_ppm = UINT32_MAX;
    size_t i;

    for (i = 0U; i < (sizeof(rates) / sizeof(rates[0])); ++i) {
        uint32_t delta = (measured > rates[i]) ? (measured - rates[i]) : (rates[i] - measured);
        uint32_t err_ppm = (delta * 1000000U) / rates[i];

        if (err_ppm < best_err_ppm) {
            best_err_ppm = err_ppm;
            best = rates[i];
        }
    }

    return (best_err_ppm <= 30000U) ? best : measured;
}
#endif

static uint32_t modbus_stop_bits_from_code(uint16_t parity_code)
{
    if (parity_code == 3U) {
        return UART_STOPBITS_1;
    }

    return UART_STOPBITS_2;
}

static void notify_vibration_config_changed(const VibConfig_t *old_cfg, const VibConfig_t *new_cfg)
{
    EventBits_t bits = 0U;

    if (old_cfg == NULL || new_cfg == NULL) {
        return;
    }

    if (old_cfg->gear_ratio != new_cfg->gear_ratio) {
        bits |= EVT_GEAR_RATIO_CHANGED;
    }
    if (old_cfg->motor_speed_rpm != new_cfg->motor_speed_rpm) {
        bits |= EVT_MOTOR_SPEED_CHANGED;
    }
    if (old_cfg->bearing_ball_count != new_cfg->bearing_ball_count) {
        bits |= EVT_BEARING_BALL_COUNT_CHANGED;
    }
    if (old_cfg->bearing_ball_size_mm != new_cfg->bearing_ball_size_mm) {
        bits |= EVT_BEARING_BALL_SIZE_CHANGED;
    }
    if (old_cfg->bearing_pitch_diameter_mm != new_cfg->bearing_pitch_diameter_mm) {
        bits |= EVT_BEARING_DIAMETER_CHANGED;
    }
    if (old_cfg->bearing_alpha_angle_deg != new_cfg->bearing_alpha_angle_deg) {
        bits |= EVT_BEARING_ALPHA_ANGLE_CHANGED;
    }
    if (old_cfg->filter_high_pass_hz != new_cfg->filter_high_pass_hz) {
        bits |= EVT_FILTER_HIGH_PASS_CHANGED;
    }
    if (old_cfg->filter_low_pass_hz != new_cfg->filter_low_pass_hz) {
        bits |= EVT_FILTER_LOW_PASS_CHANGED;
    }

    if (bits != 0U) {
        xEventGroupSetBits(xSettingsEventGroup, bits);
    }
}

static void modbus_task_process_one_frame(void)
{
    const uint8_t *shadow_frame;
    uint16_t shadow_length;

    if (ModbusEnginePort_Poll(&shadow_frame, &shadow_length)) {
        const uint8_t *response = NULL;
        uint16_t response_length = 0U;
        bool has_response = ModbusEngineRuntime_ProcessFrame(
            shadow_frame, shadow_length, &response, &response_length);
#if MODBUS_ENGINE_ACTIVE
        if (has_response) {
            ModbusEnginePort_ClearFrame();
            (void)ModbusEnginePort_UartEnable(false);
            (void)ModbusEngineTx_Send(response, response_length);
            (void)ModbusEnginePort_UartEnable(true);
        } else {
            ModbusEnginePort_ClearFrame();
        }
#else
        (void)has_response;
        (void)response;
        (void)response_length;
        ModbusEnginePort_ClearFrame();
#endif
    }
}

void main_TaskModbus(void *argument)
{
    ModbusAdapterContext_t modbus_ctx;
    VibrationAdapterContext_t vibration_ctx;
    VibConfig_t vib_cfg;
    UCHAR slave_addr;
#if !MODBUS_ENGINE_ACTIVE
    eMBParity parity;
#endif
    ULONG baud_rate;
    uint8_t baud_display;
    uint8_t parity_display;
#if !MODBUS_ENGINE_ACTIVE
    eMBErrorCode status;
#endif
    bool transport_ready = false;
    bool config_valid;
#if MODBUS_ENGINE_ACTIVE
    USHORT *input_regs = ModbusEngineMap_InputStorage();
    USHORT *holding_regs = ModbusEngineMap_HoldingStorage();
#else
    USHORT *input_regs = usSRegInBuf;
    USHORT *holding_regs = usSRegHoldBuf;
#endif

    (void)argument;

    ModbusModule_Init();
    ModbusModule_GetConfig(&cfg);
#if MODBUS_ENGINE_ACTIVE && MODBUS_ENGINE_FORCE_FIXED_COMM
    cfg.slave_id = MODBUS_ENGINE_FIXED_SLAVE_ID;
    cfg.baud_rate = MODBUS_ENGINE_FIXED_BAUD_RATE;
    cfg.parity = MODBUS_ENGINE_FIXED_PARITY;
#endif
#if MODBUS_ENGINE_ACTIVE && MODBUS_AUTOBAUD_ENABLE
    /* Autobaud owns startup selection, but begins from the safe commissioning
     * UART and fixed slave ID until a validated candidate is found. */
    cfg.slave_id = MODBUS_ENGINE_FIXED_SLAVE_ID;
    cfg.baud_rate = MODBUS_ENGINE_FIXED_BAUD_RATE;
    cfg.parity = MODBUS_ENGINE_FIXED_PARITY;
#endif
    config_valid = ModbusModule_ValidateConfig(&cfg);
    LED_SetConfigurationInvalid(!config_valid);
    VibrationModule_Init();
    VibrationModule_GetConfig(&vib_cfg);

    modbus_ctx.holding_regs = holding_regs;
    modbus_ctx.reg_count = S_REG_HOLDING_NREGS;
    vibration_ctx.holding_regs = holding_regs;
    vibration_ctx.reg_count = S_REG_HOLDING_NREGS;

    /* Initialize custom storage before adapters populate configuration values. */
    ModbusEngineMap_Init();
    ModbusAdapter_InitMap(&modbus_ctx, &cfg);
    VibrationAdapter_InitMap(&vibration_ctx, &vib_cfg);
    ModbusEnginePort_Init();
    ModbusEngineRuntime_Init();
    ModbusAutobaudDiag_Init();
#if MODBUS_ENGINE_ACTIVE
    (void)ModbusEngineTx_Init();
#endif

    slave_addr = cfg.slave_id;
#if !MODBUS_ENGINE_ACTIVE
    parity = ModbusAdapter_ParityFromCode(cfg.parity);
#endif
    baud_rate = cfg.baud_rate;
    baud_display = (uint8_t)ModbusAdapter_BaudToCode(cfg.baud_rate);
    parity_display = (uint8_t)(cfg.parity + 1U);
    g_modbus_stop_bits = modbus_stop_bits_from_code(cfg.parity);
    LED_SetBaseState(LED_STATE_NO_POLLING_YET);

    if (config_valid) {
        LED_RequestStartupDisplay(slave_addr, baud_display, parity_display, 1U);
    }

#if !MODBUS_AUTOBAUD_ENABLE
    if (baud_rate == 0U) {
        uint32_t detected = 0U;

        while ((detected = uart3_autobaud_lock(500U)) == 0U) {
        }

        if (detected >= 1200U && detected <= 1000000U) {
            baud_rate = snap_modbus_rate(detected);
        }
    }
#endif

#if MODBUS_ENGINE_ACTIVE
    /* Custom mode owns UART3/DMA.  Do not initialize or enable FreeModbus. */
    g_custom_uart_init_ok =
        ModbusEnginePort_UartInit(baud_rate, 8U, cfg.parity) ? 1U : 0U;
    g_custom_uart_enable_ok = 0U;
    if (g_custom_uart_init_ok != 0U) {
        g_custom_uart_enable_ok =
            ModbusEnginePort_UartEnable(true) ? 1U : 0U;
    }
    transport_ready =
        (g_custom_uart_init_ok != 0U) &&
        (g_custom_uart_enable_ok != 0U);
#if MODBUS_AUTOBAUD_ENABLE
    if (transport_ready) {
        ModbusAutobaudStartup_Start();
        for (;;) {
            modbus_task_process_one_frame();
            if (ModbusAutobaudStartup_Service() !=
                MODBUS_AUTOBAUD_STARTUP_RUNNING) {
                break;
            }
            osDelay(1U);
        }
        /* Keep the effective custom-engine address aligned with the startup
         * policy's fixed slave ID.  The selected UART profile is retained by
         * the coordinator after a measured/scanned lock. */
        ModbusEngineRuntime_SetSlaveId(MODBUS_ENGINE_FIXED_SLAVE_ID);
    }
#endif
#else
    status = eMBInit(MB_RTU, slave_addr, 2U, baud_rate, parity);
    if (status == MB_ENOERR) {
        status = eMBEnable();
    }
    transport_ready = (status == MB_ENOERR);
#endif

    if (transport_ready) {
        //when eMBInit no Error and after run eMBEnable no error also
        holding_regs[MB_REG_SLAVE_ID] = slave_addr;
        holding_regs[MB_REG_BAUDRATE] = ModbusAdapter_BaudToCode(cfg.baud_rate);
        holding_regs[MB_REG_PARITY] = ModbusAdapter_ParityToCode(cfg.parity);

        /* 3xxxx identity block (30001..30006). */
        input_regs[MB_IN_DEVICE_CODE] = MB_DEVICE_CODE_DEFAULT;
        input_regs[MB_IN_HARDWARE_VERSION] = MB_HARDWARE_VERSION_DEFAULT;
        input_regs[MB_IN_FIRMWARE_VERSION] = MB_FIRMWARE_VERSION_DEFAULT;
        input_regs[MB_IN_MANUFACTURING_DATE] = MB_MANUFACTURING_DATE_DEFAULT;
        input_regs[MB_IN_SERIAL_NUMBER_UPPER_W0] = MB_SERIAL_NUMBER_UPPER_W0_DEFAULT;
        input_regs[MB_IN_SERIAL_NUMBER_UPPER_W1] = MB_SERIAL_NUMBER_UPPER_W1_DEFAULT;
    }

    for (;;) {
#if !(MODBUS_ENGINE_ACTIVE && MODBUS_ENGINE_FORCE_FIXED_COMM)
        ModbusConfig_t next_cfg;
#endif
        VibConfig_t next_vib_cfg;
        VibrationAdapterCommand_t vib_cmd;
#if !MODBUS_ENGINE_ACTIVE
        eMBPoll();
#endif
        modbus_task_process_one_frame();

#if !(MODBUS_ENGINE_ACTIVE && MODBUS_ENGINE_FORCE_FIXED_COMM)
        if (ModbusAdapter_CheckConfigUpdate(&modbus_ctx, &cfg, &next_cfg)) {
            cfg = next_cfg;
            if (ModbusModule_UpdateConfig(&cfg)) {
                vTaskDelay(pdMS_TO_TICKS(500));
                NVIC_SystemReset();
            } else {
                ModbusAdapter_InitMap(&modbus_ctx, &cfg);
            }
        }
#endif

        if (VibrationAdapter_CheckConfigUpdate(&vibration_ctx, &vib_cfg, &next_vib_cfg)) {
            VibConfig_t old_vib_cfg = vib_cfg;

            if (VibrationModule_UpdateConfig(&next_vib_cfg)) {
                vib_cfg = next_vib_cfg;
                notify_vibration_config_changed(&old_vib_cfg, &vib_cfg);
                VibrationAdapter_InitMap(&vibration_ctx, &vib_cfg);
            } else {
                VibrationAdapter_InitMap(&vibration_ctx, &vib_cfg);
            }
        }

        if (VibrationAdapter_ReadCommand(&vibration_ctx, &vib_cmd)) {
            if (vib_cmd == VIBRATION_ADAPTER_CMD_CAPTURE_BASELINE) {
                (void)VibrationModule_SaveBaseline();
            } else if (vib_cmd == VIBRATION_ADAPTER_CMD_RESET_BASELINE) {
                VibrationModule_ResetBaseline();
            }
            VibrationAdapter_ClearCommand(&vibration_ctx);
        }

        osDelay(1U);
    }
}
