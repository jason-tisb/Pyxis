#ifndef MODBUS_AUTOBAUD_STARTUP_H
#define MODBUS_AUTOBAUD_STARTUP_H

#include <stdbool.h>
#include <stdint.h>

#include "modbus_autobaud_validate.h"

typedef enum {
    MODBUS_AUTOBAUD_STATE_IDLE = 0,
    MODBUS_AUTOBAUD_STATE_MEASURING,
    MODBUS_AUTOBAUD_STATE_SCORING,
    MODBUS_AUTOBAUD_STATE_VALIDATING,
    MODBUS_AUTOBAUD_STATE_SCANNING,
    MODBUS_AUTOBAUD_STATE_RESTORING,
    MODBUS_AUTOBAUD_STATE_LOCKED,
    MODBUS_AUTOBAUD_STATE_FALLBACK,
    MODBUS_AUTOBAUD_STATE_ERROR
} modbus_autobaud_startup_state_t;

typedef enum {
    MODBUS_AUTOBAUD_STARTUP_IDLE = 0,
    MODBUS_AUTOBAUD_STARTUP_RUNNING,
    MODBUS_AUTOBAUD_STARTUP_LOCKED_MEASURED,
    MODBUS_AUTOBAUD_STARTUP_LOCKED_SCANNED,
    MODBUS_AUTOBAUD_STARTUP_FALLBACK,
    MODBUS_AUTOBAUD_STARTUP_ERROR
} modbus_autobaud_startup_status_t;

typedef struct {
    modbus_autobaud_startup_status_t status;
    modbus_autobaud_startup_state_t state;
    uint32_t baud_rate;
    uint16_t confidence_permille;
    modbus_autobaud_framing_t framing_mode;
    uint8_t lock_source; /* 1=measurement, 2=scanner, 3=fallback */
    uint8_t locked;
    uint8_t fallback_used;
    uint32_t elapsed_ms;
    uint32_t scan_profiles_attempted;
    uint32_t measurement_attempts;
    uint32_t last_error;
} modbus_autobaud_startup_result_t;

/* Dynamic measurement/framing window only. Profile scan follows this window. */
#ifndef MODBUS_AUTOBAUD_TOTAL_TIMEOUT_MS
#define MODBUS_AUTOBAUD_TOTAL_TIMEOUT_MS 60000U
#endif

/* Each dynamic attempt is bounded; there is no attempt-count limit. */
#ifndef MODBUS_AUTOBAUD_MEASURE_TIMEOUT_MS
#define MODBUS_AUTOBAUD_MEASURE_TIMEOUT_MS 1500U
#endif
/* Separate profile-scan budget, starting only after dynamic timeout. */
#ifndef MODBUS_AUTOBAUD_SCAN_ROUND_MS
#define MODBUS_AUTOBAUD_SCAN_ROUND_MS 24000U
#endif
#ifndef MODBUS_AUTOBAUD_RECOVERY_RESERVE_MS
#define MODBUS_AUTOBAUD_RECOVERY_RESERVE_MS 500U
#endif

bool ModbusAutobaudStartup_Start(void);
modbus_autobaud_startup_status_t ModbusAutobaudStartup_Service(void);
void ModbusAutobaudStartup_Stop(void);
bool ModbusAutobaudStartup_GetResult(
    modbus_autobaud_startup_result_t *result);

#endif
