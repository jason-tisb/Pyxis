#ifndef MODBUS_ENGINE_MAP_H
#define MODBUS_ENGINE_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "modbus_core.h"
#include "modbus_register_map.h"

#define MODBUS_ENGINE_INPUT_STORAGE_SIZE  11600U
#define MODBUS_ENGINE_HOLDING_STORAGE_SIZE 11800U

typedef struct {
    const char *name;
    uint16_t start_address;
    uint16_t count;
    uint16_t minimum;
    uint16_t maximum;
    uint16_t default_value;
    modbus_core_reg_type_t type;
    bool writable;
} modbus_engine_map_entry_t;

/*
 * Canonical PiLoT-V map definition.  Keep this in the header deliberately:
 * different consumers can expand the same list into tables, validation data,
 * documentation, or tests without duplicating register definitions.
 */
#define MODBUS_ENGINE_MAP_HOLDING_LIST(X) \
    X("slave_id", MB_REG_SLAVE_ID, 1U, 1U, 247U, 1U, MODBUS_REG_HOLDING, true) \
    X("baudrate", MB_REG_BAUDRATE, 1U, 0U, 4U, 1U, MODBUS_REG_HOLDING, true) \
    X("parity", MB_REG_PARITY, 1U, 0U, 3U, 0U, MODBUS_REG_HOLDING, true) \
    X("response_delay", MB_REG_RESPONSE_DELAY, 1U, 0U, 65535U, 0U, MODBUS_REG_HOLDING, true) \
    X("machine_profile", MB_REG_MACHINE_PROFILE, 1U, 0U, 5U, 0U, MODBUS_REG_HOLDING, true) \
    X("baseline_command", MB_REG_BASELINE_COMMAND, 1U, 0U, 2U, 0U, MODBUS_REG_HOLDING, true) \
    X("tach_enable", MB_REG_TACH_ENABLE, 1U, 0U, 1U, 0U, MODBUS_REG_HOLDING, true) \
    X("tach_source", MB_REG_TACH_SOURCE, 1U, 0U, 65535U, 0U, MODBUS_REG_HOLDING, true) \
    X("gear_ratio", MB_REG_GEAR_RATIO, 1U, 1U, 5000U, 100U, MODBUS_REG_HOLDING, true) \
    X("base_frequency", MB_REG_BASE_FREQ_HZ, 1U, 1U, 65535U, 1U, MODBUS_REG_HOLDING, true) \
    X("bearing_ball_count", MB_REG_BEARING_BALL_COUNT, 1U, 3U, 32U, 3U, MODBUS_REG_HOLDING, true) \
    X("bearing_ball_size", MB_REG_BEARING_BALL_SIZE, 1U, 1U, 5000U, 1U, MODBUS_REG_HOLDING, true) \
    X("bearing_diameter", MB_REG_BEARING_DIAMETER, 1U, 1U, 7200U, 1U, MODBUS_REG_HOLDING, true) \
    X("bearing_alpha", MB_REG_BEARING_ALPHA_ANGLE, 1U, 0U, 900U, 0U, MODBUS_REG_HOLDING, true) \
    X("filter_high_pass", MB_REG_FILTER_HIGH_PASS, 1U, 1U, 8000U, 1U, MODBUS_REG_HOLDING, true) \
    X("filter_low_pass", MB_REG_FILTER_LOW_PASS, 1U, 1U, 8000U, 1U, MODBUS_REG_HOLDING, true) \
    X("refresh", MB_REG_REFRESH, 1U, 0U, 1U, 0U, MODBUS_REG_HOLDING, true) \
    X("gear_teeth", MB_REG_GEAR_TEETH_NUMBER, 1U, 1U, 1024U, 1U, MODBUS_REG_HOLDING, true) \
    X("remapping", MB_REG_REMAPPING_ADDR_START, MB_REG_REMAPPING_ADDR_COUNT, 0U, 65535U, 0U, MODBUS_REG_HOLDING, true)

#define MODBUS_ENGINE_MAP_INPUT_LIST(X) \
    X("identity", MB_IN_DEVICE_CODE, 6U, 0U, 65535U, 0U, MODBUS_REG_INPUT, false) \
    X("temperature", MB_REG_TEMPERATURE, 2U, 0U, 65535U, 0U, MODBUS_REG_INPUT, false) \
    X("x_vibration", MB_REG_X_ACC_RMS, 50U, 0U, 65535U, 0U, MODBUS_REG_INPUT, false) \
    X("y_vibration", MB_REG_Y_ACC_RMS, 50U, 0U, 65535U, 0U, MODBUS_REG_INPUT, false) \
    X("z_vibration", MB_REG_Z_ACC_RMS, 50U, 0U, 65535U, 0U, MODBUS_REG_INPUT, false) \
    X("analytics", MB_REG_TIME_PREV_W0, 30U, 0U, 65535U, 0U, MODBUS_REG_INPUT, false)

uint16_t ModbusEngineMap_Count(void);
const modbus_engine_map_entry_t *ModbusEngineMap_Entries(void);
void ModbusEngineMap_Init(void);
uint16_t *ModbusEngineMap_InputStorage(void);
uint16_t *ModbusEngineMap_HoldingStorage(void);

const modbus_engine_map_entry_t *ModbusEngineMap_Find(
    modbus_core_reg_type_t type, uint16_t address);

bool ModbusEngineMap_Validate(void);

modbus_core_access_result_t ModbusEngineMap_Read(
    modbus_core_reg_type_t type, uint16_t address, uint16_t *value,
    bool *writable, uint16_t *minimum, uint16_t *maximum);

modbus_core_access_result_t ModbusEngineMap_ValidateWrite(
    modbus_core_reg_type_t type, uint16_t address, uint16_t value);

modbus_core_access_result_t ModbusEngineMap_ValidateRange(
    modbus_core_reg_type_t type, uint16_t start_address,
    const uint16_t *values, uint16_t quantity);

#endif /* MODBUS_ENGINE_MAP_H */
