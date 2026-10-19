#include "modbus_autobaud_diag.h"

#include <string.h>

/* One canonical RAM store. The named globals below are aliases in spirit and
 * remain debugger-visible for compatibility with Titania diagnostics. */
volatile uint32_t g_autobaud_measurement_valid_count;
volatile uint32_t g_autobaud_measurement_ambiguous_count;
volatile uint32_t g_autobaud_measurement_timeout_count;
volatile uint32_t g_autobaud_measurement_error_count;
volatile uint32_t g_autobaud_scan_used_count;
volatile uint32_t g_autobaud_scan_lock_count;
volatile uint32_t g_autobaud_measured_lock_count;
volatile uint32_t g_autobaud_total_failure_count;
volatile uint32_t g_autobaud_detected_baud;
volatile uint32_t g_autobaud_confidence;
volatile uint32_t g_autobaud_active;
volatile uint32_t g_autobaud_locked;
volatile uint32_t g_autobaud_measurement_unavailable_count;
volatile uint32_t g_autobaud_capture_overrun_count;
volatile uint32_t g_autobaud_uart_recovery_count;
volatile uint32_t g_autobaud_uart_recovery_failure_count;
volatile uint32_t g_autobaud_crc_reject_count;
volatile uint32_t g_autobaud_validation_timeout_count;
volatile uint32_t g_autobaud_last_error;
volatile uint32_t g_autobaud_last_profile;
volatile uint32_t g_autobaud_elapsed_ms;

static volatile uint32_t diag_sequence;
static modbus_autobaud_diag_t diag;

static void write_begin(void)
{
    diag_sequence++;
}

static void write_end(void)
{
    diag_sequence++;
}

static void sync_globals(void)
{
    g_autobaud_measurement_valid_count = diag.measurement_valid_count;
    g_autobaud_measurement_ambiguous_count = diag.measurement_ambiguous_count;
    g_autobaud_measurement_timeout_count = diag.measurement_timeout_count;
    g_autobaud_measurement_error_count = diag.measurement_error_count;
    g_autobaud_scan_used_count = diag.scan_used_count;
    g_autobaud_scan_lock_count = diag.scan_lock_count;
    g_autobaud_measured_lock_count = diag.measured_lock_count;
    g_autobaud_total_failure_count = diag.total_failure_count;
    g_autobaud_detected_baud = diag.measured_baud;
    g_autobaud_confidence = diag.confidence_permille;
    g_autobaud_active = diag.active;
    g_autobaud_locked = diag.locked;
    g_autobaud_measurement_unavailable_count =
        diag.measurement_unavailable_count;
    g_autobaud_capture_overrun_count = diag.capture_overrun_count;
    g_autobaud_uart_recovery_count = diag.uart_recovery_count;
    g_autobaud_uart_recovery_failure_count =
        diag.uart_recovery_failure_count;
    g_autobaud_crc_reject_count = diag.crc_reject_count;
    g_autobaud_validation_timeout_count = diag.validation_timeout_count;
    g_autobaud_last_error = diag.last_error;
    g_autobaud_last_profile = diag.last_profile;
    g_autobaud_elapsed_ms = diag.elapsed_ms;
}

static void commit(void)
{
    sync_globals();
}

void ModbusAutobaudDiag_Init(void)
{
    write_begin();
    memset(&diag, 0, sizeof(diag));
    diag.outcome = MODBUS_AUTOBAUD_OUTCOME_NONE;
    commit();
    write_end();
}

bool ModbusAutobaudDiag_GetSnapshot(modbus_autobaud_diag_t *snapshot)
{
    uint32_t before;
    uint32_t after;

    if (snapshot == NULL) {
        return false;
    }
    do {
        before = diag_sequence;
        if ((before & 1U) != 0U) {
            continue;
        }
        *snapshot = diag;
        after = diag_sequence;
    } while ((before != after) || ((after & 1U) != 0U));
    return true;
}

void ModbusAutobaudDiag_OnStart(void)
{
    write_begin();
    diag.active = 1U;
    diag.locked = 0U;
    diag.fallback_used = 0U;
    diag.outcome = MODBUS_AUTOBAUD_OUTCOME_RUNNING;
    diag.state = MODBUS_AUTOBAUD_STATE_IDLE;
    diag.last_error = MODBUS_AUTOBAUD_ERROR_NONE;
    diag.elapsed_ms = 0U;
    diag.profiles_attempted = 0U;
    diag.unique_profiles_visited = 0U;
    diag.completed_scan_cycles = 0U;
    diag.scan_elapsed_ms = 0U;
    diag.last_profile = 0U;
    commit();
    write_end();
}

void ModbusAutobaudDiag_RecordMeasurementValid(uint32_t baud,
                                               uint16_t confidence)
{
    write_begin();
    diag.measurement_valid_count++;
    diag.measured_baud = baud;
    diag.confidence_permille = confidence;
    diag.last_error = MODBUS_AUTOBAUD_ERROR_NONE;
    commit();
    write_end();
}

void ModbusAutobaudDiag_RecordMeasurementAmbiguous(void)
{
    write_begin();
    diag.measurement_ambiguous_count++;
    diag.last_error = MODBUS_AUTOBAUD_ERROR_MEASUREMENT_AMBIGUOUS;
    commit();
    write_end();
}

void ModbusAutobaudDiag_RecordMeasurementUnavailable(void)
{
    write_begin();
    diag.measurement_unavailable_count++;
    diag.last_error = MODBUS_AUTOBAUD_ERROR_CAPTURE_UNAVAILABLE;
    commit();
    write_end();
}

void ModbusAutobaudDiag_RecordMeasurementTimeout(void)
{
    write_begin();
    diag.measurement_timeout_count++;
    diag.last_error = MODBUS_AUTOBAUD_ERROR_CAPTURE_TIMEOUT;
    commit();
    write_end();
}

void ModbusAutobaudDiag_RecordMeasurementError(modbus_autobaud_error_t error)
{
    write_begin();
    diag.measurement_error_count++;
    diag.last_error = error;
    commit();
    write_end();
}

void ModbusAutobaudDiag_RecordCaptureOverrun(void)
{
    write_begin();
    diag.capture_overrun_count++;
    diag.last_error = MODBUS_AUTOBAUD_ERROR_CAPTURE_OVERRUN;
    commit();
    write_end();
}

void ModbusAutobaudDiag_RecordValidationTimeout(void)
{
    write_begin();
    diag.validation_timeout_count++;
    diag.last_error = MODBUS_AUTOBAUD_ERROR_VALIDATION_TIMEOUT;
    commit();
    write_end();
}

void ModbusAutobaudDiag_RecordValidationReject(void)
{
    write_begin();
    diag.crc_reject_count++;
    commit();
    write_end();
}

void ModbusAutobaudDiag_RecordUartRecovery(bool success)
{
    write_begin();
    diag.uart_recovery_count++;
    if (!success) {
        diag.uart_recovery_failure_count++;
        diag.last_error = MODBUS_AUTOBAUD_ERROR_UART_ROLLBACK_FAILED;
    }
    commit();
    write_end();
}

void ModbusAutobaudDiag_OnCandidate(uint32_t baud,
                                    modbus_autobaud_framing_t framing)
{
    write_begin();
    diag.active_baud = baud;
    diag.active_framing = (uint8_t)framing;
    commit();
    write_end();
}

void ModbusAutobaudDiag_OnScanStart(void)
{
    write_begin();
    diag.scan_used_count++;
    commit();
    write_end();
}

void ModbusAutobaudDiag_OnScanProgress(
    const modbus_autobaud_scan_result_t *scan)
{
    if (scan == NULL) {
        return;
    }
    write_begin();
    diag.active_baud = scan->baud_rate;
    diag.active_framing = (uint8_t)scan->framing_mode;
    diag.last_profile = scan->profile_index;
    diag.profiles_attempted = scan->profiles_attempted;
    diag.unique_profiles_visited = scan->unique_profiles_visited;
    diag.completed_scan_cycles = scan->completed_cycles;
    diag.scan_elapsed_ms = scan->elapsed_ms;
    commit();
    write_end();
}

void ModbusAutobaudDiag_OnTerminal(
    const modbus_autobaud_startup_result_t *startup)
{
    if (startup == NULL) {
        return;
    }
    write_begin();
    diag.active = 0U;
    diag.state = (uint8_t)startup->state;
    diag.elapsed_ms = startup->elapsed_ms;
    diag.active_baud = startup->baud_rate;
    diag.active_framing = (uint8_t)startup->framing_mode;
    diag.confidence_permille = startup->confidence_permille;
    diag.profiles_attempted = startup->scan_profiles_attempted;
    diag.fallback_used = startup->fallback_used;
    diag.locked = startup->locked;
    diag.last_error = startup->last_error;
    if (startup->status == MODBUS_AUTOBAUD_STARTUP_LOCKED_MEASURED) {
        diag.outcome = MODBUS_AUTOBAUD_OUTCOME_LOCKED_MEASURED;
        diag.measured_lock_count++;
    } else if (startup->status == MODBUS_AUTOBAUD_STARTUP_LOCKED_SCANNED) {
        diag.outcome = MODBUS_AUTOBAUD_OUTCOME_LOCKED_SCANNED;
        diag.scan_lock_count++;
    } else if (startup->status == MODBUS_AUTOBAUD_STARTUP_FALLBACK) {
        diag.outcome = MODBUS_AUTOBAUD_OUTCOME_FALLBACK;
        diag.total_failure_count++;
    } else if (startup->status == MODBUS_AUTOBAUD_STARTUP_ERROR) {
        diag.outcome = MODBUS_AUTOBAUD_OUTCOME_ERROR;
        diag.total_failure_count++;
    }
    commit();
    write_end();
}

void ModbusAutobaudDiag_SetError(modbus_autobaud_error_t error)
{
    write_begin();
    diag.last_error = error;
    commit();
    write_end();
}

void ModbusAutobaudDiag_SetElapsed(uint32_t elapsed_ms)
{
    write_begin();
    diag.elapsed_ms = elapsed_ms;
    commit();
    write_end();
}

void ModbusAutobaudDiag_SetState(uint8_t state)
{
    write_begin();
    diag.state = state;
    commit();
    write_end();
}
