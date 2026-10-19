#ifndef MODBUS_AUTOBAUD_VALIDATE_H
#define MODBUS_AUTOBAUD_VALIDATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MODBUS_AUTOBAUD_FRAMING_8E1 = 0,
    MODBUS_AUTOBAUD_FRAMING_8N2,
    MODBUS_AUTOBAUD_FRAMING_8O1,
    MODBUS_AUTOBAUD_FRAMING_8N1
} modbus_autobaud_framing_t;

typedef enum {
    MODBUS_AUTOBAUD_VALIDATE_IDLE = 0,
    MODBUS_AUTOBAUD_VALIDATE_RUNNING,
    MODBUS_AUTOBAUD_VALIDATE_VALID,
    MODBUS_AUTOBAUD_VALIDATE_TIMEOUT,
    MODBUS_AUTOBAUD_VALIDATE_RX_ERROR,
    MODBUS_AUTOBAUD_VALIDATE_CONFIG_ERROR
} modbus_autobaud_validation_status_t;

typedef struct {
    modbus_autobaud_validation_status_t status;
    uint32_t baud_rate;
    modbus_autobaud_framing_t framing;
    uint32_t generation;
    uint32_t processed_at_start;
    uint32_t processed_at_result;
} modbus_autobaud_validation_result_t;

#ifndef MODBUS_AUTOBAUD_FRAMING_TIMEOUT_MS
#define MODBUS_AUTOBAUD_FRAMING_TIMEOUT_MS 2000U
#endif

bool ModbusAutobaudValidate_Start(uint32_t baud_rate,
                                  modbus_autobaud_framing_t framing);
bool ModbusAutobaudValidate_StartWithTimeout(
    uint32_t baud_rate, modbus_autobaud_framing_t framing,
    uint32_t timeout_ms);
modbus_autobaud_validation_status_t ModbusAutobaudValidate_Service(void);
void ModbusAutobaudValidate_Stop(void);
const modbus_autobaud_validation_result_t *
ModbusAutobaudValidate_GetResult(void);

#endif
