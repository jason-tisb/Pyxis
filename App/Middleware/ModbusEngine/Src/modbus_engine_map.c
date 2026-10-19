#include "modbus_engine_map.h"
#include "modbus_engine_config.h"

#include <stddef.h>
#include <string.h>

#define MAP_ENTRY(name, address, count, minimum, maximum, default_value, type, writable) \
    { name, address, count, minimum, maximum, default_value, type, writable },

static const modbus_engine_map_entry_t map_entries[] = {
    MODBUS_ENGINE_MAP_HOLDING_LIST(MAP_ENTRY)
    MODBUS_ENGINE_MAP_INPUT_LIST(MAP_ENTRY)
};

#define MAP_ENTRY_COUNT (sizeof(map_entries) / sizeof(map_entries[0]))

#if MODBUS_ENGINE_ACTIVE
static uint16_t input_storage[MODBUS_ENGINE_INPUT_STORAGE_SIZE];
static uint16_t holding_storage[MODBUS_ENGINE_HOLDING_STORAGE_SIZE];
#else
#include "user_mb_app.h"
extern USHORT usSRegInBuf[S_REG_INPUT_NREGS];
extern USHORT usSRegHoldBuf[S_REG_HOLDING_NREGS];
#endif

void ModbusEngineMap_Init(void)
{
#if MODBUS_ENGINE_ACTIVE
    size_t i;

    memset(input_storage, 0, sizeof(input_storage));
    memset(holding_storage, 0, sizeof(holding_storage));
    for (i = 0U; i < MAP_ENTRY_COUNT; ++i) {
        const modbus_engine_map_entry_t *entry = &map_entries[i];
        uint16_t *storage = (entry->type == MODBUS_REG_INPUT) ?
            input_storage : holding_storage;
        uint16_t storage_size = (entry->type == MODBUS_REG_INPUT) ?
            MODBUS_ENGINE_INPUT_STORAGE_SIZE : MODBUS_ENGINE_HOLDING_STORAGE_SIZE;
        uint16_t offset;

        for (offset = 0U; (offset < entry->count) &&
             ((uint32_t)entry->start_address + offset < storage_size); ++offset) {
            storage[entry->start_address + offset] = entry->default_value;
        }
    }
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
    return (uint16_t)MAP_ENTRY_COUNT;
}

const modbus_engine_map_entry_t *ModbusEngineMap_Entries(void)
{
    return map_entries;
}

const modbus_engine_map_entry_t *ModbusEngineMap_Find(
    modbus_core_reg_type_t type, uint16_t address)
{
    size_t i;

    for (i = 0U; i < MAP_ENTRY_COUNT; ++i) {
        const modbus_engine_map_entry_t *entry = &map_entries[i];
        uint32_t end = (uint32_t)entry->start_address + entry->count;

        if ((entry->type == type) &&
            ((uint32_t)address >= entry->start_address) &&
            ((uint32_t)address < end)) {
            return entry;
        }
    }
    return NULL;
}

bool ModbusEngineMap_Validate(void)
{
    size_t i;
    size_t j;

    for (i = 0U; i < MAP_ENTRY_COUNT; ++i) {
        const modbus_engine_map_entry_t *a = &map_entries[i];
        uint32_t a_end = (uint32_t)a->start_address + a->count;

        if ((a->count == 0U) || (a->minimum > a->maximum) ||
            (a->default_value < a->minimum) ||
            (a->default_value > a->maximum) || (a_end > 65536U)) {
            return false;
        }
        if ((a->type == MODBUS_REG_INPUT) && a->writable) {
            return false;
        }
        for (j = i + 1U; j < MAP_ENTRY_COUNT; ++j) {
            const modbus_engine_map_entry_t *b = &map_entries[j];
            uint32_t b_end = (uint32_t)b->start_address + b->count;

            if ((a->type == b->type) && (a->start_address < b_end) &&
                (b->start_address < a_end)) {
                return false;
            }
        }
    }
    return true;
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
        *writable = entry->writable;
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
    const modbus_engine_map_entry_t *entry = ModbusEngineMap_Find(type, address);

    if ((entry == NULL) || !entry->writable) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
    if ((value < entry->minimum) || (value > entry->maximum)) {
        return MODBUS_ACCESS_ILLEGAL_VALUE;
    }
    return MODBUS_ACCESS_OK;
}

modbus_core_access_result_t ModbusEngineMap_ValidateRange(
    modbus_core_reg_type_t type, uint16_t start_address,
    const uint16_t *values, uint16_t quantity)
{
    uint16_t i;

    if ((values == NULL) || (quantity == 0U)) {
        return MODBUS_ACCESS_ILLEGAL_VALUE;
    }
    for (i = 0U; i < quantity; ++i) {
        modbus_core_access_result_t result = ModbusEngineMap_ValidateWrite(
            type, (uint16_t)(start_address + i), values[i]);
        if (result != MODBUS_ACCESS_OK) {
            return result;
        }
    }
    return MODBUS_ACCESS_OK;
}
