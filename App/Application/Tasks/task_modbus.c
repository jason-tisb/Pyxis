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
#include "modbus_persistence.h"
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
static bool modbus_task_read_holding(void *user, uint16_t address,
                                     uint16_t *value);
static void modbus_task_process_one_frame(void);
#if MODBUS_ENGINE_ACTIVE
static void modbus_task_service_comm_persistence(void);
#endif

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
    /* Communication-code mapping:
     *   0 = 8E1, 1 = 8O1, 2 = 8N2, 3 = 8N1. */
    return (parity_code == 2U) ? UART_STOPBITS_2 : UART_STOPBITS_1;
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

static bool modbus_task_read_holding(void *user, uint16_t address,
                                     uint16_t *value)
{
    const USHORT *holding = (const USHORT *)user;

    if ((holding == NULL) || (value == NULL) ||
        (address >= S_REG_HOLDING_NREGS)) {
        return false;
    }
    *value = holding[address];
    return true;
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
            bool tx_complete = ModbusEngineTx_Send(response, response_length);
            (void)ModbusEnginePort_UartEnable(true);
            /* Destructive system commands are deliberately serviced only
             * after ModbusEngineTx_Send() has waited for UART TC and DE has
             * been returned to receive mode. */
            if (ModbusEngineRuntime_GetPendingSystemAction() !=
                MODBUS_SYSTEM_ACTION_NONE) {
                if (tx_complete) {
                    (void)ModbusEngineRuntime_ServiceSystemAction();
                } else {
                    ModbusEngineRuntime_CancelSystemAction();
                }
            }
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

#if MODBUS_ENGINE_ACTIVE
static void modbus_task_service_comm_persistence(void)
{
    if (!ModbusEngineRuntime_HasCommPersistencePending()) {
        return;
    }
    (void)ModbusEngineRuntime_PersistConfiguredComm();
}
#endif

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
#if MODBUS_ENGINE_ACTIVE
    uint32_t uart_startup_baud;
    uint8_t uart_startup_parity;
#endif
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

    /* Initialise canonical storage and bind the persistence shadow before
     * loading the configured communication image.  The map remains the sole
     * source of persistent Holding-word enumeration. */
    ModbusEngineMap_Init();
    ModbusPersistence_BindHoldingStorage(holding_regs, S_REG_HOLDING_NREGS);
    ModbusModule_Init();
    ModbusModule_GetConfig(&cfg);
#if MODBUS_ENGINE_ACTIVE && MODBUS_ENGINE_FORCE_FIXED_COMM
    cfg.slave_id = MODBUS_ENGINE_FIXED_SLAVE_ID;
    cfg.baud_rate = MODBUS_ENGINE_FIXED_BAUD_RATE;
    cfg.parity = MODBUS_ENGINE_FIXED_PARITY;
#endif
    config_valid = ModbusModule_ValidateConfig(&cfg);
    LED_SetConfigurationInvalid(!config_valid);
    VibrationModule_Init();
    VibrationModule_GetConfig(&vib_cfg);

    /* A verified H1 shadow is authoritative for persistent Holding words.
     * Legacy per-module storage remains a migration source only when no H1
     * record exists. */
    if (ModbusPersistence_HasVerifiedRecord()) {
        VibConfig_t persisted_vib_cfg;
        if (VibrationAdapter_BuildConfigFromReader(
                modbus_task_read_holding, holding_regs, &vib_cfg,
                &persisted_vib_cfg) &&
            VibrationModule_CommitValidatedRuntimeConfig(&persisted_vib_cfg)) {
            vib_cfg = persisted_vib_cfg;
        }
    }

    modbus_ctx.holding_regs = holding_regs;
    modbus_ctx.reg_count = S_REG_HOLDING_NREGS;
    vibration_ctx.holding_regs = holding_regs;
    vibration_ctx.reg_count = S_REG_HOLDING_NREGS;

    ModbusAdapter_InitMap(&modbus_ctx, &cfg);
    VibrationAdapter_InitMap(&vibration_ctx, &vib_cfg);
    ModbusEnginePort_Init();
    ModbusEngineRuntime_Init();
    /* Freeze the communication settings selected for this boot.  Later
     * Modbus writes update the configured/persistent image only. */
    ModbusEngineRuntime_SetActiveCommConfig(&cfg);
#if MODBUS_AUTOBAUD_ENABLE
    /* Validation must use the resolved user/default address immediately. */
    ModbusEngineRuntime_SetSlaveId(cfg.slave_id);
#endif
    ModbusAutobaudDiag_Init();
#if MODBUS_ENGINE_ACTIVE
    (void)ModbusEngineTx_Init();
#endif

    slave_addr = cfg.slave_id;
#if !MODBUS_ENGINE_ACTIVE
    parity = ModbusAdapter_ParityFromCode(cfg.parity);
#endif
    baud_rate = cfg.baud_rate;
#if MODBUS_ENGINE_ACTIVE
    /* A zero baud selector means Auto.  The capture coordinator owns the
     * actual UART candidate selection, but HAL still needs a valid provisional
     * baud/framing while the transport is brought up.  Keep that provisional
     * setup separate from the configured and active snapshots. */
    uart_startup_baud = (baud_rate == 0U) ? MODBUS_ENGINE_FIXED_BAUD_RATE :
                        baud_rate;
    uart_startup_parity = (baud_rate == 0U) ? MODBUS_ENGINE_FIXED_PARITY :
                          cfg.parity;
#endif
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
        ModbusEnginePort_UartInit(uart_startup_baud, 8U,
                                  uart_startup_parity) ? 1U : 0U;
    g_custom_uart_enable_ok = 0U;
    if (g_custom_uart_init_ok != 0U) {
        g_custom_uart_enable_ok =
            ModbusEnginePort_UartEnable(true) ? 1U : 0U;
    }
    transport_ready =
        (g_custom_uart_init_ok != 0U) &&
        (g_custom_uart_enable_ok != 0U);
#if MODBUS_AUTOBAUD_ENABLE
    /* Only selector 0 requests automatic detection.  A fixed configured
     * selector must start and remain on that configuration for this boot. */
    if (transport_ready && (cfg.baud_rate == 0U)) {
        ModbusAutobaudStartup_Start();
        for (;;) {
            modbus_task_process_one_frame();
            if (ModbusAutobaudStartup_Service() !=
                MODBUS_AUTOBAUD_STARTUP_RUNNING) {
                break;
            }
            osDelay(1U);
        }
        /* Autobaud does not change the resolved user/default address. */
        ModbusEngineRuntime_SetSlaveId(cfg.slave_id);
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
#if !MODBUS_ENGINE_ACTIVE && !MODBUS_ENGINE_FORCE_FIXED_COMM
        ModbusConfig_t next_cfg;
#endif
        VibConfig_t next_vib_cfg;
        VibrationAdapterCommand_t vib_cmd;
#if !MODBUS_ENGINE_ACTIVE
        eMBPoll();
#endif
        modbus_task_process_one_frame();

#if MODBUS_ENGINE_ACTIVE
        /* Communication settings are persisted after the transaction is
         * accepted, but remain boot-applied.  No UART or slave ID change is
         * performed in this runtime path. */
        modbus_task_service_comm_persistence();
#endif

#if !MODBUS_ENGINE_ACTIVE && !MODBUS_ENGINE_FORCE_FIXED_COMM
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
            bool persisted;
#if MODBUS_ENGINE_ACTIVE
            ModbusConfig_t persist_cfg = cfg;
            (void)ModbusEngineRuntime_GetConfiguredCommConfig(&persist_cfg);
            persisted = ModbusPersistence_SaveConfig(&persist_cfg);
#else
            persisted = VibrationModule_PersistConfig(&next_vib_cfg);
#endif
#if !MODBUS_ENGINE_ACTIVE
            if (persisted) {
                persisted = VibrationModule_CommitRuntimeConfig(&next_vib_cfg);
            }
#endif

            ModbusEngineRuntime_RecordPersistenceResult(persisted);
            if (persisted) {
                vib_cfg = next_vib_cfg;
                notify_vibration_config_changed(&old_vib_cfg, &vib_cfg);
                VibrationAdapter_InitMap(&vibration_ctx, &vib_cfg);
            } else {
                /* The RAM transaction was already accepted and published;
                 * retain the configured/active value even when NVM fails. */
                vib_cfg = next_vib_cfg;
                notify_vibration_config_changed(&old_vib_cfg, &vib_cfg);
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
