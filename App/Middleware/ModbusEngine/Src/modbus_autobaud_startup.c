#include "modbus_autobaud_startup.h"

#include <string.h>

#include "main.h"
#include "modbus_autobaud_capture.h"
#include "modbus_autobaud_diag.h"
#include "modbus_autobaud_scan.h"
#include "modbus_autobaud_score.h"
#include "modbus_engine_config.h"
#include "modbus_engine_port.h"
#include "modbus_engine_runtime.h"
#include "modbus_engine_tx.h"

#if MODBUS_ENGINE_ACTIVE

static modbus_autobaud_startup_result_t result;
static uint8_t running;
static uint32_t started_tick;
static uint32_t retry_after_tick;
static uint32_t measured_baud;
static uint8_t measurement_attempt;
static uint8_t framing_index;
static modbus_autobaud_score_result_t score_result;
static const modbus_autobaud_framing_t framing_order[4] = {
    MODBUS_AUTOBAUD_FRAMING_8N1,
    MODBUS_AUTOBAUD_FRAMING_8E1,
    MODBUS_AUTOBAUD_FRAMING_8N2,
    MODBUS_AUTOBAUD_FRAMING_8O1
};

static uint32_t elapsed_ms(void)
{
    return (uint32_t)(HAL_GetTick() - started_tick);
}

static uint32_t remaining_for_work(void)
{
    uint32_t elapsed = elapsed_ms();
    if (elapsed >= MODBUS_AUTOBAUD_TOTAL_TIMEOUT_MS) {
        return 0U;
    }
    elapsed = MODBUS_AUTOBAUD_TOTAL_TIMEOUT_MS - elapsed;
    return (elapsed > MODBUS_AUTOBAUD_RECOVERY_RESERVE_MS) ?
           elapsed - MODBUS_AUTOBAUD_RECOVERY_RESERVE_MS : 0U;
}

static void update_time(void)
{
    result.elapsed_ms = elapsed_ms();
    ModbusAutobaudDiag_SetElapsed(result.elapsed_ms);
    ModbusAutobaudDiag_SetState((uint8_t)result.state);
}

static bool apply_fallback(void)
{
    result.state = MODBUS_AUTOBAUD_STATE_RESTORING;
    /* Capture_Stop also re-enables UART3.  Only call it when this coordinator
     * actually owns an active TIM2 capture; this keeps fallback idempotent and
     * avoids disturbing an already-restored RX transport. */
    if (ModbusAutobaudCapture_IsActive()) {
        if (!ModbusAutobaudCapture_Stop()) {
            result.last_error = 3U;
            ModbusAutobaudDiag_SetError(
                MODBUS_AUTOBAUD_ERROR_UART_ROLLBACK_FAILED);
        }
    }
    ModbusAutobaudValidate_Stop();
    ModbusAutobaudScan_Stop();
    if (!ModbusEnginePort_ReconfigureUart(
            MODBUS_ENGINE_FIXED_BAUD_RATE, 8U, 2U, UART_STOPBITS_1)) {
        result.last_error = 1U;
        result.state = MODBUS_AUTOBAUD_STATE_ERROR;
        result.status = MODBUS_AUTOBAUD_STARTUP_ERROR;
        result.fallback_used = 0U;
        running = 0U;
        update_time();
        ModbusAutobaudDiag_SetError(MODBUS_AUTOBAUD_ERROR_FALLBACK_FAILED);
        ModbusAutobaudDiag_OnTerminal(&result);
        return false;
    }
    /* Preserve the resolved user/default slave ID during UART fallback. */
    result.baud_rate = MODBUS_ENGINE_FIXED_BAUD_RATE;
    result.framing_mode = MODBUS_AUTOBAUD_FRAMING_8N1;
    result.lock_source = 3U;
    result.locked = 0U;
    result.fallback_used = 1U;
    result.state = MODBUS_AUTOBAUD_STATE_FALLBACK;
    result.status = MODBUS_AUTOBAUD_STARTUP_FALLBACK;
    running = 0U;
    update_time();
    ModbusAutobaudDiag_OnTerminal(&result);
    return true;
}

static bool start_measurement(void)
{
    if (remaining_for_work() == 0U) {
        return false;
    }
    if (!ModbusAutobaudCapture_Start()) {
        return false;
    }
    result.measurement_attempts = (uint32_t)measurement_attempt + 1U;
    result.state = MODBUS_AUTOBAUD_STATE_MEASURING;
    return true;
}

static bool start_validation(void)
{
    uint32_t timeout = remaining_for_work();
    if (timeout == 0U) {
        return false;
    }
    if (timeout > MODBUS_AUTOBAUD_FRAMING_TIMEOUT_MS) {
        timeout = MODBUS_AUTOBAUD_FRAMING_TIMEOUT_MS;
    }
    if (!ModbusAutobaudValidate_StartWithTimeout(
            measured_baud, framing_order[framing_index], timeout)) {
        return false;
    }
    result.state = MODBUS_AUTOBAUD_STATE_VALIDATING;
    result.baud_rate = measured_baud;
    result.framing_mode = framing_order[framing_index];
    ModbusAutobaudDiag_OnCandidate(
        measured_baud, framing_order[framing_index]);
    return true;
}

static bool start_scanner(void)
{
    uint32_t budget = remaining_for_work();
    if (budget == 0U || !ModbusAutobaudScan_Start(budget)) {
        return false;
    }
    result.state = MODBUS_AUTOBAUD_STATE_SCANNING;
    ModbusAutobaudDiag_OnScanStart();
    return true;
}

bool ModbusAutobaudStartup_Start(void)
{
    if (running != 0U || ModbusEngineTx_IsBusy()) {
        return false;
    }
    result = (modbus_autobaud_startup_result_t){
        MODBUS_AUTOBAUD_STARTUP_RUNNING,
        MODBUS_AUTOBAUD_STATE_IDLE, 0U, 0U,
        MODBUS_AUTOBAUD_FRAMING_8E1, 0U, 0U, 0U, 0U, 0U, 0U
    };
    started_tick = HAL_GetTick();
    measurement_attempt = 0U;
    framing_index = 0U;
    retry_after_tick = 0U;
    running = 1U;
    ModbusAutobaudDiag_OnStart();
    if (!start_measurement()) {
        (void)apply_fallback();
    }
    return true;
}

modbus_autobaud_startup_status_t ModbusAutobaudStartup_Service(void)
{
    modbus_autobaud_capture_result_t capture_result;
    modbus_autobaud_validation_status_t validation;
    modbus_autobaud_scan_result_t scan_result;
    uint32_t capture_timeout;

    if (running == 0U) {
        return result.status;
    }
    update_time();
    if (remaining_for_work() == 0U) {
        (void)apply_fallback();
        return result.status;
    }

    switch (result.state) {
    case MODBUS_AUTOBAUD_STATE_MEASURING:
        capture_timeout = remaining_for_work();
        if (capture_timeout > MODBUS_AUTOBAUD_MEASURE_TIMEOUT_MS) {
            capture_timeout = MODBUS_AUTOBAUD_MEASURE_TIMEOUT_MS;
        }
        if (!ModbusAutobaudCapture_Service(capture_timeout)) {
            break;
        }
        bool score_ok = false;
        bool measurement_accepted = false;
        memset(&score_result, 0, sizeof(score_result));
        if (!ModbusAutobaudCapture_GetResult(&capture_result)) {
            result.last_error = MODBUS_AUTOBAUD_ERROR_CAPTURE_UNAVAILABLE;
            ModbusAutobaudDiag_RecordMeasurementUnavailable();
        } else if (capture_result.overrun != 0U) {
            result.last_error = MODBUS_AUTOBAUD_ERROR_CAPTURE_OVERRUN;
            ModbusAutobaudDiag_RecordCaptureOverrun();
            ModbusAutobaudDiag_RecordMeasurementError(
                MODBUS_AUTOBAUD_ERROR_CAPTURE_OVERRUN);
        } else if (capture_result.timer_frequency_hz == 0U) {
            result.last_error = MODBUS_AUTOBAUD_ERROR_TIMER_CLOCK_INVALID;
            ModbusAutobaudDiag_RecordMeasurementUnavailable();
            ModbusAutobaudDiag_RecordMeasurementError(
                MODBUS_AUTOBAUD_ERROR_TIMER_CLOCK_INVALID);
        } else if (ModbusAutobaudScore_Evaluate(
                       capture_result.interval_ticks,
                       capture_result.interval_count,
                       capture_result.timer_frequency_hz,
                       &score_result) == MODBUS_AUTOBAUD_SCORE_ERROR) {
            result.last_error = MODBUS_AUTOBAUD_ERROR_MEASUREMENT_AMBIGUOUS;
            ModbusAutobaudDiag_RecordMeasurementError(
                MODBUS_AUTOBAUD_ERROR_MEASUREMENT_AMBIGUOUS);
        } else {
            score_ok = true;
            measurement_accepted = ModbusAutobaudScore_IsAccepted(
                &score_result);
            if (measurement_accepted) {
                ModbusAutobaudDiag_RecordMeasurementValid(
                    score_result.baud_rate,
                    score_result.confidence_permille);
            } else if ((capture_result.interval_count < 8U) ||
                       (capture_result.timed_out != 0U)) {
                ModbusAutobaudDiag_RecordMeasurementTimeout();
            } else {
                ModbusAutobaudDiag_RecordMeasurementAmbiguous();
            }
        }
        result.confidence_permille = score_result.confidence_permille;
        if (score_ok && measurement_accepted) {
            measured_baud = score_result.baud_rate;
            framing_index = 0U;
            if (!start_validation()) {
                (void)start_scanner();
            }
        } else if (++measurement_attempt < MODBUS_AUTOBAUD_MEASURE_ATTEMPTS) {
            retry_after_tick = HAL_GetTick() + 20U;
            result.state = MODBUS_AUTOBAUD_STATE_SCORING;
        } else if (!start_scanner()) {
            (void)apply_fallback();
        }
        break;

    case MODBUS_AUTOBAUD_STATE_SCORING:
        if ((int32_t)(HAL_GetTick() - retry_after_tick) >= 0) {
            if (!start_measurement()) {
                (void)apply_fallback();
            }
        }
        break;

    case MODBUS_AUTOBAUD_STATE_VALIDATING:
        validation = ModbusAutobaudValidate_Service();
        if (validation == MODBUS_AUTOBAUD_VALIDATE_VALID) {
            result.status = MODBUS_AUTOBAUD_STARTUP_LOCKED_MEASURED;
            result.state = MODBUS_AUTOBAUD_STATE_LOCKED;
            result.lock_source = 1U;
            result.locked = 1U;
            running = 0U;
            update_time();
            ModbusAutobaudDiag_OnTerminal(&result);
        } else if (validation == MODBUS_AUTOBAUD_VALIDATE_TIMEOUT) {
            ModbusAutobaudDiag_RecordValidationTimeout();
            if (++framing_index < 4U) {
                if (!start_validation()) {
                    (void)start_scanner();
                }
            } else if (!start_scanner()) {
                (void)apply_fallback();
            }
        } else if (validation == MODBUS_AUTOBAUD_VALIDATE_CONFIG_ERROR ||
                   validation == MODBUS_AUTOBAUD_VALIDATE_RX_ERROR) {
            if (!start_scanner()) {
                (void)apply_fallback();
            }
        }
        break;

    case MODBUS_AUTOBAUD_STATE_SCANNING:
    {
        modbus_autobaud_scan_status_t scan_status =
            ModbusAutobaudScan_Service();
        (void)ModbusAutobaudScan_GetResult(&scan_result);
        ModbusAutobaudDiag_OnScanProgress(&scan_result);
        switch (scan_status) {
        case MODBUS_AUTOBAUD_SCAN_LOCKED:
            result.status = MODBUS_AUTOBAUD_STARTUP_LOCKED_SCANNED;
            result.state = MODBUS_AUTOBAUD_STATE_LOCKED;
            result.baud_rate = scan_result.baud_rate;
            result.framing_mode = scan_result.framing_mode;
            result.lock_source = 2U;
            result.locked = 1U;
            result.scan_profiles_attempted = scan_result.profiles_attempted;
            running = 0U;
            update_time();
            ModbusAutobaudDiag_OnTerminal(&result);
            break;
        case MODBUS_AUTOBAUD_SCAN_ERROR:
            ModbusAutobaudDiag_SetError(
                MODBUS_AUTOBAUD_ERROR_UART_CONFIG_FAILED);
            (void)apply_fallback();
            break;
        case MODBUS_AUTOBAUD_SCAN_TIMEOUT:
            ModbusAutobaudDiag_SetError(
                MODBUS_AUTOBAUD_ERROR_SCAN_BUDGET_EXHAUSTED);
            result.scan_profiles_attempted = scan_result.profiles_attempted;
            (void)apply_fallback();
            break;
        default:
            break;
        }
        break;
    }

    default:
        (void)apply_fallback();
        break;
    }
    update_time();
    return result.status;
}

void ModbusAutobaudStartup_Stop(void)
{
    if (running != 0U) {
        (void)apply_fallback();
    }
}

bool ModbusAutobaudStartup_GetResult(
    modbus_autobaud_startup_result_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = result;
    return true;
}

#else

bool ModbusAutobaudStartup_Start(void) { return false; }
modbus_autobaud_startup_status_t ModbusAutobaudStartup_Service(void)
{ return MODBUS_AUTOBAUD_STARTUP_ERROR; }
void ModbusAutobaudStartup_Stop(void) { }
bool ModbusAutobaudStartup_GetResult(modbus_autobaud_startup_result_t *out)
{
    if (out == NULL) return false;
    *out = (modbus_autobaud_startup_result_t){
        MODBUS_AUTOBAUD_STARTUP_ERROR, MODBUS_AUTOBAUD_STATE_ERROR,
        0U, 0U, MODBUS_AUTOBAUD_FRAMING_8N1, 0U, 0U, 0U,
        0U, 0U, 0U
    };
    return true;
}

#endif
