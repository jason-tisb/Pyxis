#include "modbus_engine_map.h"
#include "modbus_engine_config.h"

#include <stddef.h>
#include <string.h>

#if !MODBUS_ENGINE_ACTIVE
#include "user_mb_app.h"
extern USHORT usSRegInBuf[S_REG_INPUT_NREGS];
extern USHORT usSRegHoldBuf[S_REG_HOLDING_NREGS];
#else
static uint16_t input_storage[MODBUS_ENGINE_INPUT_STORAGE_SIZE];
static uint16_t holding_storage[MODBUS_ENGINE_HOLDING_STORAGE_SIZE];
#endif

static bool engine_type_to_map_type(
    modbus_core_reg_type_t type, modbus_map_register_type_t *map_type)
{
    if (map_type == NULL) {
        return false;
    }
    if (type == MODBUS_REG_HOLDING) {
        *map_type = MODBUS_MAP_REGISTER_HOLDING;
        return true;
    }
    if (type == MODBUS_REG_INPUT) {
        *map_type = MODBUS_MAP_REGISTER_INPUT;
        return true;
    }
    return false;
}

void ModbusEngineMap_Init(void)
{
#if MODBUS_ENGINE_ACTIVE
    const modbus_map_entry_t *entries = ModbusMap_Entries();
    uint16_t entry_count = ModbusMap_Count();
    uint16_t i;

    memset(input_storage, 0, sizeof(input_storage));
    memset(holding_storage, 0, sizeof(holding_storage));
    for (i = 0U; i < entry_count; ++i) {
        const modbus_map_entry_t *entry = &entries[i];
        uint16_t *storage = (entry->type == MODBUS_MAP_REGISTER_INPUT) ?
            input_storage : holding_storage;
        uint32_t storage_size = (entry->type == MODBUS_MAP_REGISTER_INPUT) ?
            MODBUS_ENGINE_INPUT_STORAGE_SIZE : MODBUS_ENGINE_HOLDING_STORAGE_SIZE;
        uint16_t offset;

        for (offset = 0U; (offset < entry->count) &&
             ((uint32_t)entry->start_address + offset < storage_size); ++offset) {
            storage[entry->start_address + offset] = entry->default_value;
        }
    }
#endif
}

bool ModbusEngineMap_RestoreHoldingDefaults(void)
{
#if MODBUS_ENGINE_ACTIVE
    const modbus_map_entry_t *entries = ModbusMap_Entries();
    uint16_t entry_count = ModbusMap_Count();
    uint16_t *holding = holding_storage;
    uint16_t i;

    if ((entries == NULL) || (holding == NULL)) {
        return false;
    }
    for (i = 0U; i < entry_count; ++i) {
        const modbus_map_entry_t *entry = &entries[i];
        uint16_t offset;

        if (entry->type != MODBUS_MAP_REGISTER_HOLDING) {
            continue;
        }
        if ((uint32_t)entry->start_address + entry->count >
            MODBUS_ENGINE_HOLDING_STORAGE_SIZE) {
            return false;
        }
        for (offset = 0U; offset < entry->count; ++offset) {
            holding[entry->start_address + offset] = entry->default_value;
        }
    }
    return true;
#else
    return false;
#endif
}

uint16_t *ModbusEngineMap_InputStorage(void)
{
#if MODBUS_ENGINE_ACTIVE
    return input_storage;
#else
    return usSRegInBuf;
#endif
}

uint16_t *ModbusEngineMap_HoldingStorage(void)
{
#if MODBUS_ENGINE_ACTIVE
    return holding_storage;
#else
    return usSRegHoldBuf;
#endif
}

uint16_t ModbusEngineMap_Count(void)
{
    return ModbusMap_Count();
}

const modbus_engine_map_entry_t *ModbusEngineMap_Entries(void)
{
    return ModbusMap_Entries();
}

const modbus_engine_map_entry_t *ModbusEngineMap_Find(
    modbus_core_reg_type_t type, uint16_t address)
{
    modbus_map_register_type_t map_type;

    if (!engine_type_to_map_type(type, &map_type)) {
        return NULL;
    }
    return ModbusMap_Find(map_type, address);
}

bool ModbusEngineMap_Validate(void)
{
    return ModbusMap_ValidateIntegrity();
}

modbus_core_access_result_t ModbusEngineMap_Read(
    modbus_core_reg_type_t type, uint16_t address, uint16_t *value,
    bool *writable, uint16_t *minimum, uint16_t *maximum)
{
    const modbus_engine_map_entry_t *entry = ModbusEngineMap_Find(type, address);

    if ((entry == NULL) || (value == NULL)) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
    if (writable != NULL) {
        *writable = (entry->access == MODBUS_MAP_ACCESS_READ_WRITE);
    }
    if (minimum != NULL) {
        *minimum = entry->minimum;
    }
    if (maximum != NULL) {
        *maximum = entry->maximum;
    }
    return MODBUS_ACCESS_OK;
}

modbus_core_access_result_t ModbusEngineMap_ValidateWrite(
    modbus_core_reg_type_t type, uint16_t address, uint16_t value)
{
    modbus_map_register_type_t map_type;
    const modbus_engine_map_entry_t *entry;
    uint16_t word_offset;

    if (!engine_type_to_map_type(type, &map_type) ||
        (map_type != MODBUS_MAP_REGISTER_HOLDING)) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
    if (!ModbusMap_Resolve(map_type, address, &entry, &word_offset) ||
        (entry->access != MODBUS_MAP_ACCESS_READ_WRITE)) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
    (void)word_offset;
    if (!ModbusMap_ValidateValue(entry, value)) {
        return MODBUS_ACCESS_ILLEGAL_VALUE;
    }
    return MODBUS_ACCESS_OK;
}

modbus_core_access_result_t ModbusEngineMap_ValidateRange(
    modbus_core_reg_type_t type, uint16_t start_address,
    uint16_t quantity, modbus_map_access_t required_access)
{
    modbus_map_register_type_t map_type;
    uint32_t start = (uint32_t)start_address;
    uint16_t i;

    if ((quantity == 0U) || !engine_type_to_map_type(type, &map_type) ||
        ((required_access != MODBUS_MAP_ACCESS_READ_ONLY) &&
         (required_access != MODBUS_MAP_ACCESS_READ_WRITE))) {
        return MODBUS_ACCESS_ILLEGAL_VALUE;
    }
    if (!ModbusMap_IsAddressRangeValid(start_address, quantity)) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
    for (i = 0U; i < quantity; ++i) {
        const modbus_map_entry_t *entry;
        uint16_t word_offset;

        if (!ModbusMap_Resolve(map_type, (uint16_t)(start + (uint32_t)i),
                               &entry, &word_offset)) {
            return MODBUS_ACCESS_ILLEGAL_ADDRESS;
        }
        (void)word_offset;

        if ((entry == NULL) || (entry->access != required_access)) {
            return MODBUS_ACCESS_ILLEGAL_ADDRESS;
        }
    }
    return MODBUS_ACCESS_OK;
}

modbus_core_access_result_t ModbusEngineMap_ValidateWriteRange(
    uint16_t start_address, const uint16_t *values, uint16_t quantity)
{
    modbus_core_access_result_t result;
    modbus_map_register_type_t map_type = MODBUS_MAP_REGISTER_HOLDING;
    uint32_t start = (uint32_t)start_address;
    uint16_t i;

    if ((values == NULL) || (quantity == 0U)) {
        return MODBUS_ACCESS_ILLEGAL_VALUE;
    }

    /* Phase A: address coverage and access have priority over value errors. */
    result = ModbusEngineMap_ValidateRange(
        MODBUS_REG_HOLDING, start_address, quantity,
        MODBUS_MAP_ACCESS_READ_WRITE);
    if (result != MODBUS_ACCESS_OK) {
        return result;
    }

    /* Phase B: validate each value against its exact containing entry. */
    for (i = 0U; i < quantity; ++i) {
        const modbus_map_entry_t *entry;
        uint16_t word_offset;

        if (!ModbusMap_Resolve(map_type, (uint16_t)(start + (uint32_t)i),
                               &entry, &word_offset)) {
            /* This should be unreachable after Phase A; retain safe mapping. */
            return MODBUS_ACCESS_ILLEGAL_ADDRESS;
        }
        (void)word_offset;
        if (!ModbusMap_ValidateValue(entry, values[i])) {
            return MODBUS_ACCESS_ILLEGAL_VALUE;
        }
    }
    return MODBUS_ACCESS_OK;
}
