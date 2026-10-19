#ifndef MODBUS_AUTOBAUD_SCAN_H
#define MODBUS_AUTOBAUD_SCAN_H

#include <stdbool.h>
#include <stdint.h>

#include "modbus_autobaud_validate.h"

#define MODBUS_AUTOBAUD_PROFILE_COUNT 20U
#define MODBUS_AUTOBAUD_PROFILE_DWELL_MS 1200U

typedef enum {
    MODBUS_AUTOBAUD_SCAN_IDLE = 0,
    MODBUS_AUTOBAUD_SCAN_RUNNING,
    MODBUS_AUTOBAUD_SCAN_LOCKED,
    MODBUS_AUTOBAUD_SCAN_TIMEOUT,
    MODBUS_AUTOBAUD_SCAN_ERROR
} modbus_autobaud_scan_status_t;

typedef struct {
    modbus_autobaud_scan_status_t status;
    uint32_t baud_rate;
    modbus_autobaud_framing_t framing_mode;
    uint8_t profile_index;
    uint32_t elapsed_ms;
    uint32_t profiles_attempted;
    uint32_t unique_profiles_visited;
    uint32_t completed_cycles;
    uint32_t budget_ms;
    uint8_t locked;
    uint8_t recovery_failed;
} modbus_autobaud_scan_result_t;

bool ModbusAutobaudScan_Start(uint32_t budget_ms);
modbus_autobaud_scan_status_t ModbusAutobaudScan_Service(void);
void ModbusAutobaudScan_Stop(void);
bool ModbusAutobaudScan_GetResult(modbus_autobaud_scan_result_t *result);

#endif
