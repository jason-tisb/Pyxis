#ifndef MODBUS_AUTOBAUD_DIAG_H
#define MODBUS_AUTOBAUD_DIAG_H

#include <stdbool.h>
#include <stdint.h>

#include "modbus_autobaud_scan.h"
#include "modbus_autobaud_startup.h"

typedef enum {
    MODBUS_AUTOBAUD_OUTCOME_NONE = 0,
    MODBUS_AUTOBAUD_OUTCOME_RUNNING,
    MODBUS_AUTOBAUD_OUTCOME_LOCKED_MEASURED,
    MODBUS_AUTOBAUD_OUTCOME_LOCKED_SCANNED,
    MODBUS_AUTOBAUD_OUTCOME_FALLBACK,
    MODBUS_AUTOBAUD_OUTCOME_ERROR
} modbus_autobaud_outcome_t;

typedef enum {
    MODBUS_AUTOBAUD_ERROR_NONE = 0,
    MODBUS_AUTOBAUD_ERROR_CAPTURE_UNAVAILABLE,
    MODBUS_AUTOBAUD_ERROR_CAPTURE_TIMEOUT,
    MODBUS_AUTOBAUD_ERROR_CAPTURE_OVERRUN,
    MODBUS_AUTOBAUD_ERROR_TIMER_CLOCK_INVALID,
    MODBUS_AUTOBAUD_ERROR_MEASUREMENT_AMBIGUOUS,
    MODBUS_AUTOBAUD_ERROR_VALIDATION_TIMEOUT,
    MODBUS_AUTOBAUD_ERROR_UART_CONFIG_FAILED,
    MODBUS_AUTOBAUD_ERROR_UART_ROLLBACK_FAILED,
    MODBUS_AUTOBAUD_ERROR_RX_RESTART_FAILED,
    MODBUS_AUTOBAUD_ERROR_SCAN_BUDGET_EXHAUSTED,
    MODBUS_AUTOBAUD_ERROR_GLOBAL_TIMEOUT,
    MODBUS_AUTOBAUD_ERROR_FALLBACK_FAILED
} modbus_autobaud_error_t;

typedef struct {
    uint32_t measurement_valid_count;
    uint32_t measurement_ambiguous_count;
    uint32_t measurement_unavailable_count;
    uint32_t measurement_timeout_count;
    uint32_t measurement_error_count;
    uint32_t scan_used_count;
    uint32_t scan_lock_count;
    uint32_t measured_lock_count;
    uint32_t total_failure_count;
    uint32_t capture_overrun_count;
    uint32_t uart_recovery_count;
    uint32_t uart_recovery_failure_count;
    uint32_t crc_reject_count;
    uint32_t validation_timeout_count;
    uint32_t measured_baud;
    uint32_t active_baud;
    uint16_t confidence_permille;
    uint8_t active_framing;
    uint8_t last_profile;
    uint32_t elapsed_ms;
    uint32_t profiles_attempted;
    uint32_t unique_profiles_visited;
    uint32_t completed_scan_cycles;
    uint32_t scan_elapsed_ms;
    uint32_t last_error;
    modbus_autobaud_outcome_t outcome;
    uint8_t state;
    uint8_t active;
    uint8_t locked;
    uint8_t fallback_used;
} modbus_autobaud_diag_t;

/* Debugger-visible compatibility counters. All are RAM-only and wrap naturally. */
extern volatile uint32_t g_autobaud_measurement_valid_count;
extern volatile uint32_t g_autobaud_measurement_ambiguous_count;
extern volatile uint32_t g_autobaud_measurement_timeout_count;
extern volatile uint32_t g_autobaud_measurement_error_count;
extern volatile uint32_t g_autobaud_scan_used_count;
extern volatile uint32_t g_autobaud_scan_lock_count;
extern volatile uint32_t g_autobaud_measured_lock_count;
extern volatile uint32_t g_autobaud_total_failure_count;
extern volatile uint32_t g_autobaud_detected_baud;
extern volatile uint32_t g_autobaud_confidence;
extern volatile uint32_t g_autobaud_active;
extern volatile uint32_t g_autobaud_locked;
extern volatile uint32_t g_autobaud_measurement_unavailable_count;
extern volatile uint32_t g_autobaud_capture_overrun_count;
extern volatile uint32_t g_autobaud_uart_recovery_count;
extern volatile uint32_t g_autobaud_uart_recovery_failure_count;
extern volatile uint32_t g_autobaud_crc_reject_count;
extern volatile uint32_t g_autobaud_validation_timeout_count;
extern volatile uint32_t g_autobaud_last_error;
extern volatile uint32_t g_autobaud_last_profile;
extern volatile uint32_t g_autobaud_elapsed_ms;

void ModbusAutobaudDiag_Init(void);
bool ModbusAutobaudDiag_GetSnapshot(modbus_autobaud_diag_t *snapshot);

void ModbusAutobaudDiag_OnStart(void);
void ModbusAutobaudDiag_RecordMeasurementValid(uint32_t baud,
                                               uint16_t confidence);
void ModbusAutobaudDiag_RecordMeasurementAmbiguous(void);
void ModbusAutobaudDiag_RecordMeasurementUnavailable(void);
void ModbusAutobaudDiag_RecordMeasurementTimeout(void);
void ModbusAutobaudDiag_RecordMeasurementError(modbus_autobaud_error_t error);
void ModbusAutobaudDiag_RecordCaptureOverrun(void);
void ModbusAutobaudDiag_RecordValidationTimeout(void);
void ModbusAutobaudDiag_RecordValidationReject(void);
void ModbusAutobaudDiag_RecordUartRecovery(bool success);
void ModbusAutobaudDiag_OnCandidate(uint32_t baud,
                                    modbus_autobaud_framing_t framing);
void ModbusAutobaudDiag_OnScanStart(void);
void ModbusAutobaudDiag_OnScanProgress(
    const modbus_autobaud_scan_result_t *scan);
void ModbusAutobaudDiag_OnTerminal(
    const modbus_autobaud_startup_result_t *startup);
void ModbusAutobaudDiag_SetError(modbus_autobaud_error_t error);
void ModbusAutobaudDiag_SetElapsed(uint32_t elapsed_ms);
void ModbusAutobaudDiag_SetState(uint8_t state);

#endif
