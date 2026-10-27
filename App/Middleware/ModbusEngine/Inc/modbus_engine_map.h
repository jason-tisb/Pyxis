#ifndef MODBUS_ENGINE_MAP_H
#define MODBUS_ENGINE_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "modbus_core.h"
#include "modbus_map.h"
#include "modbus_register_map.h"

#define MODBUS_ENGINE_INPUT_STORAGE_SIZE   PYXIS_MODBUS_INPUT_STORAGE_SIZE
#define MODBUS_ENGINE_HOLDING_STORAGE_SIZE PYXIS_MODBUS_HOLDING_STORAGE_SIZE

/* The engine consumes the canonical table directly; no metadata copy exists. */
typedef modbus_map_entry_t modbus_engine_map_entry_t;

#define MODBUS_ENGINE_MAP_HOLDING_LIST(X) PYXIS_MODBUS_HOLDING_LIST(X)
#define MODBUS_ENGINE_MAP_INPUT_LIST(X) PYXIS_MODBUS_INPUT_LIST(X)

uint16_t ModbusEngineMap_Count(void);
const modbus_engine_map_entry_t *ModbusEngineMap_Entries(void);
void ModbusEngineMap_Init(void);
/* Restore every canonical Holding-register word to its map default. */
bool ModbusEngineMap_RestoreHoldingDefaults(void);
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

/* Validate numeric coverage and access only; values are not inspected. */
modbus_core_access_result_t ModbusEngineMap_ValidateRange(
    modbus_core_reg_type_t type, uint16_t start_address,
    uint16_t quantity, modbus_map_access_t required_access);

/* Validate a complete Holding-register write, with deterministic precedence. */
modbus_core_access_result_t ModbusEngineMap_ValidateWriteRange(
    uint16_t start_address, const uint16_t *values, uint16_t quantity);

#endif /* MODBUS_ENGINE_MAP_H */
