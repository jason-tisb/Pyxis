#include "modbus_autobaud_validate.h"

#include "main.h"
#include "modbus_autobaud_diag.h"
#include "modbus_core.h"
#include "modbus_engine_config.h"
#include "modbus_engine_port.h"
#include "modbus_engine_runtime.h"
#include "modbus_engine_tx.h"

#if MODBUS_ENGINE_ACTIVE

typedef struct {
    uint8_t parity_code;
    uint32_t stop_bits;
} framing_config_t;

static const framing_config_t framing_table[] = {
    { 0U, UART_STOPBITS_1 }, /* 8E1 */
    { 2U, UART_STOPBITS_2 }, /* 8N2 */
    { 1U, UART_STOPBITS_1 }, /* 8O1 */
    { 2U, UART_STOPBITS_1 }  /* 8N1 */
};

static modbus_autobaud_validation_result_t result;
static uint8_t running;
static uint32_t started_tick;
static uint32_t timeout_ms_active;
static uint32_t processed_baseline;
static uint32_t processed_observed;
static uint32_t error_baseline;
static uint32_t previous_baud;
static uint8_t previous_data_bits;
static uint8_t previous_parity;
static uint32_t previous_stop_bits;

static bool valid_framing(modbus_autobaud_framing_t framing)
{
    return ((uint32_t)framing <
            (sizeof(framing_table) / sizeof(framing_table[0])));
}

static void set_result(modbus_autobaud_validation_status_t status)
{
    result.status = status;
    result.processed_at_result =
        ModbusEngineRuntime_GetDiagnostics()->frames_processed;
}

static bool restore_previous_uart(void)
{
    bool ok = ModbusEnginePort_ReconfigureUart(
        previous_baud, previous_data_bits, previous_parity, previous_stop_bits);
    ModbusAutobaudDiag_RecordUartRecovery(ok);
    return ok;
}

bool ModbusAutobaudValidate_StartWithTimeout(
    uint32_t baud_rate, modbus_autobaud_framing_t framing,
    uint32_t timeout_ms)
{
    const ModbusEngineRuntimeDiagnostics_t *diag;

    if (running != 0U || !valid_framing(framing) || baud_rate == 0U ||
        timeout_ms == 0U ||
        ModbusEngineTx_IsBusy()) {
        return false;
    }
    if (!ModbusEnginePort_GetUartConfig(&previous_baud, &previous_data_bits,
                                        &previous_parity, &previous_stop_bits)) {
        set_result(MODBUS_AUTOBAUD_VALIDATE_CONFIG_ERROR);
        return false;
    }

    result.baud_rate = baud_rate;
    result.framing = framing;
    result.generation++;
    result.processed_at_result = 0U;
    diag = ModbusEngineRuntime_GetDiagnostics();
    processed_baseline = diag->frames_processed;
    processed_observed = processed_baseline;
    error_baseline = 0U;
    started_tick = HAL_GetTick();
    timeout_ms_active = timeout_ms;

    if (!ModbusEnginePort_ReconfigureUart(
            baud_rate, 8U, framing_table[framing].parity_code,
            framing_table[framing].stop_bits)) {
        set_result(MODBUS_AUTOBAUD_VALIDATE_CONFIG_ERROR);
        return false;
    }
    running = 1U;
    result.processed_at_start = processed_baseline;
    set_result(MODBUS_AUTOBAUD_VALIDATE_RUNNING);
    return true;
}

modbus_autobaud_validation_status_t ModbusAutobaudValidate_Service(void)
{
    const ModbusEngineRuntimeDiagnostics_t *diag;
    uint32_t processed;

    if (running == 0U) {
        return result.status;
    }

    diag = ModbusEngineRuntime_GetDiagnostics();
    processed = diag->frames_processed;

    /* A frame is eligible only after the normal Modbus task has processed it.
     * This observes CRC, structure and address checks without parsing or
     * responding a second time.  Broadcast is intentionally excluded here. */
    if (processed != processed_observed) {
        processed_observed = processed;
        if ((ModbusEngineRuntime_GetLastResult() == MODBUS_PROC_OK) &&
            ModbusEngineRuntime_LastFrameMatches(
                ModbusEngineRuntime_GetSlaveId())) {
            result.processed_at_result = processed;
            set_result(MODBUS_AUTOBAUD_VALIDATE_VALID);
            running = 0U;
            return result.status;
        }
        ModbusAutobaudDiag_RecordValidationReject();
    }

    /* HAL_UART_ErrorCallback restarts the existing DMA transport.  Remember
     * the event for diagnostics but do not classify a candidate from a single
     * recoverable error. */
    (void)error_baseline;

    if ((uint32_t)(HAL_GetTick() - started_tick) >=
        timeout_ms_active) {
        if (!restore_previous_uart()) {
            set_result(MODBUS_AUTOBAUD_VALIDATE_CONFIG_ERROR);
        } else {
            set_result(MODBUS_AUTOBAUD_VALIDATE_TIMEOUT);
        }
        running = 0U;
    }
    return result.status;
}

bool ModbusAutobaudValidate_Start(uint32_t baud_rate,
                                  modbus_autobaud_framing_t framing)
{
    return ModbusAutobaudValidate_StartWithTimeout(
        baud_rate, framing, MODBUS_AUTOBAUD_FRAMING_TIMEOUT_MS);
}

void ModbusAutobaudValidate_Stop(void)
{
    if (running != 0U) {
        if (!restore_previous_uart()) {
            set_result(MODBUS_AUTOBAUD_VALIDATE_CONFIG_ERROR);
        } else {
            set_result(MODBUS_AUTOBAUD_VALIDATE_IDLE);
        }
        running = 0U;
    }
}

const modbus_autobaud_validation_result_t *
ModbusAutobaudValidate_GetResult(void)
{
    return &result;
}

#else

bool ModbusAutobaudValidate_Start(uint32_t baud_rate,
                                  modbus_autobaud_framing_t framing)
{
    (void)baud_rate;
    (void)framing;
    return false;
}

bool ModbusAutobaudValidate_StartWithTimeout(
    uint32_t baud_rate, modbus_autobaud_framing_t framing,
    uint32_t timeout_ms)
{
    (void)baud_rate;
    (void)framing;
    (void)timeout_ms;
    return false;
}

modbus_autobaud_validation_status_t ModbusAutobaudValidate_Service(void)
{
    return MODBUS_AUTOBAUD_VALIDATE_CONFIG_ERROR;
}

void ModbusAutobaudValidate_Stop(void)
{
}

const modbus_autobaud_validation_result_t *
ModbusAutobaudValidate_GetResult(void)
{
    static const modbus_autobaud_validation_result_t disabled = {
        MODBUS_AUTOBAUD_VALIDATE_CONFIG_ERROR, 0U,
        MODBUS_AUTOBAUD_FRAMING_8E1, 0U, 0U, 0U
    };
    return &disabled;
}

#endif
