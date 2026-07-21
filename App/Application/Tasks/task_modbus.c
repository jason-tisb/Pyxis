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

extern USHORT usSRegInBuf[S_REG_INPUT_NREGS];
extern USHORT usSRegHoldBuf[S_REG_HOLDING_NREGS];
extern UART_HandleTypeDef huart3;

static ModbusConfig_t cfg;
uint32_t g_modbus_stop_bits = UART_STOPBITS_1;

static uint32_t uart3_autobaud_lock(uint32_t timeout_ms);
static uint32_t snap_modbus_rate(uint32_t measured);
static uint32_t modbus_stop_bits_from_code(uint16_t parity_code);
static void notify_vibration_config_changed(const VibConfig_t *old_cfg, const VibConfig_t *new_cfg);

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

void main_TaskModbus(void *argument)
{
    ModbusAdapterContext_t modbus_ctx;
    VibrationAdapterContext_t vibration_ctx;
    VibConfig_t vib_cfg;
    UCHAR slave_addr;
    eMBParity parity;
    ULONG baud_rate;
    uint8_t baud_display;
    uint8_t parity_display;
    eMBErrorCode status;
    bool config_valid;

    (void)argument;

    ModbusModule_Init();
    ModbusModule_GetConfig(&cfg);
    config_valid = ModbusModule_ValidateConfig(&cfg);
    LED_SetConfigurationInvalid(!config_valid);
    VibrationModule_Init();
    VibrationModule_GetConfig(&vib_cfg);

    modbus_ctx.holding_regs = usSRegHoldBuf;
    modbus_ctx.reg_count = S_REG_HOLDING_NREGS;
    vibration_ctx.holding_regs = usSRegHoldBuf;
    vibration_ctx.reg_count = S_REG_HOLDING_NREGS;

    ModbusAdapter_InitMap(&modbus_ctx, &cfg);
    VibrationAdapter_InitMap(&vibration_ctx, &vib_cfg);

    slave_addr = cfg.slave_id;
    parity = ModbusAdapter_ParityFromCode(cfg.parity);
    baud_rate = cfg.baud_rate;
    baud_display = (uint8_t)ModbusAdapter_BaudToCode(cfg.baud_rate);
    parity_display = (uint8_t)(cfg.parity + 1U);
    g_modbus_stop_bits = modbus_stop_bits_from_code(cfg.parity);
    LED_SetBaseState(LED_STATE_NO_POLLING_YET);

    if (config_valid) {
        LED_RequestStartupDisplay(slave_addr, baud_display, parity_display, 1U);
    }

    if (baud_rate == 0U) {
        uint32_t detected = 0U;

        while ((detected = uart3_autobaud_lock(500U)) == 0U) {
        }

        if (detected >= 1200U && detected <= 1000000U) {
            baud_rate = snap_modbus_rate(detected);
        }
    }

    status = eMBInit(MB_RTU, slave_addr, 2U, baud_rate, parity);
    if (status == MB_ENOERR) {
        //when eMBInit no Error
        status = eMBEnable();
    }

    if (status == MB_ENOERR) {
        //when eMBInit no Error and after run eMBEnable no error also
        usSRegHoldBuf[MB_REG_SLAVE_ID] = slave_addr;
        usSRegHoldBuf[MB_REG_BAUDRATE] = ModbusAdapter_BaudToCode(cfg.baud_rate);
        usSRegHoldBuf[MB_REG_PARITY] = ModbusAdapter_ParityToCode(cfg.parity);

        /* 3xxxx identity block (30001..30006). */
        usSRegInBuf[MB_IN_DEVICE_CODE] = MB_DEVICE_CODE_DEFAULT;
        usSRegInBuf[MB_IN_HARDWARE_VERSION] = MB_HARDWARE_VERSION_DEFAULT;
        usSRegInBuf[MB_IN_FIRMWARE_VERSION] = MB_FIRMWARE_VERSION_DEFAULT;
        usSRegInBuf[MB_IN_MANUFACTURING_DATE] = MB_MANUFACTURING_DATE_DEFAULT;
        usSRegInBuf[MB_IN_SERIAL_NUMBER_UPPER_W0] = MB_SERIAL_NUMBER_UPPER_W0_DEFAULT;
        usSRegInBuf[MB_IN_SERIAL_NUMBER_UPPER_W1] = MB_SERIAL_NUMBER_UPPER_W1_DEFAULT;
    }

    for (;;) {
        ModbusConfig_t next_cfg;
        VibConfig_t next_vib_cfg;
        VibrationAdapterCommand_t vib_cmd;

        eMBPoll();

        if (ModbusAdapter_CheckConfigUpdate(&modbus_ctx, &cfg, &next_cfg)) {
            cfg = next_cfg;
            if (ModbusModule_UpdateConfig(&cfg)) {
                vTaskDelay(pdMS_TO_TICKS(500));
                NVIC_SystemReset();
            } else {
                ModbusAdapter_InitMap(&modbus_ctx, &cfg);
            }
        }

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
