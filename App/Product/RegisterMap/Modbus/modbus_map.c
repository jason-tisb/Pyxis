#include "modbus_map.h"

#include <stddef.h>
#include <string.h>

#define MAP_ENTRY(id, name, address, count, minimum, maximum, default_value, type, access, persistence, domain, apply_policy) \
    { id, name, address, count, minimum, maximum, default_value, type, access, persistence, domain, apply_policy },

static const modbus_map_entry_t map_entries[] = {
    PYXIS_MODBUS_HOLDING_LIST(MAP_ENTRY)
    PYXIS_MODBUS_INPUT_LIST(MAP_ENTRY)
};

#define MAP_ENTRY_COUNT (sizeof(map_entries) / sizeof(map_entries[0]))

static bool map_type_is_valid(modbus_map_register_type_t type)
{
    return (type == MODBUS_MAP_REGISTER_HOLDING) ||
           (type == MODBUS_MAP_REGISTER_INPUT);
}

static bool map_access_is_valid(modbus_map_access_t access)
{
    return (access == MODBUS_MAP_ACCESS_READ_ONLY) ||
           (access == MODBUS_MAP_ACCESS_READ_WRITE);
}

static bool map_persistence_is_valid(modbus_map_persistence_t persistence)
{
    return (persistence == MODBUS_MAP_PERSIST_VOLATILE) ||
           (persistence == MODBUS_MAP_PERSISTENT);
}

static bool map_apply_policy_is_valid(modbus_map_apply_policy_t policy)
{
    return (policy == MODBUS_APPLY_IMMEDIATE) ||
           (policy == MODBUS_APPLY_ON_BOOT);
}

static bool map_domain_is_valid(modbus_map_domain_t domain)
{
    return ((uint32_t)domain <=
            (uint32_t)MODBUS_MAP_DOMAIN_SYSTEM_COMMAND);
}

const modbus_map_entry_t *ModbusMap_Entries(void)
{
    return map_entries;
}

uint16_t ModbusMap_Count(void)
{
    return (uint16_t)MAP_ENTRY_COUNT;
}

const modbus_map_entry_t *ModbusMap_Find(
    modbus_map_register_type_t type, uint16_t address)
{
    size_t i;

    for (i = 0U; i < MAP_ENTRY_COUNT; ++i) {
        const modbus_map_entry_t *entry = &map_entries[i];
        uint32_t start = (uint32_t)entry->start_address;
        uint32_t end = start + (uint32_t)entry->count;

        if ((entry->type == type) &&
            ((uint32_t)address >= start) &&
            ((uint32_t)address < end)) {
            return entry;
        }
    }
    return NULL;
}

bool ModbusMap_Resolve(
    modbus_map_register_type_t type, uint16_t address,
    const modbus_map_entry_t **entry, uint16_t *word_offset)
{
    const modbus_map_entry_t *resolved;

    if (entry != NULL) {
        *entry = NULL;
    }
    if (word_offset != NULL) {
        *word_offset = 0U;
    }
    if ((entry == NULL) || (word_offset == NULL)) {
        return false;
    }

    resolved = ModbusMap_Find(type, address);
    if (resolved == NULL) {
        return false;
    }
    *entry = resolved;
    *word_offset = (uint16_t)((uint32_t)address -
                              (uint32_t)resolved->start_address);
    return true;
}

bool ModbusMap_IsAddressRangeValid(
    uint16_t start_address, uint16_t count)
{
    return (count != 0U) &&
           (((uint32_t)start_address + (uint32_t)count) <= 0x10000UL);
}

const modbus_map_entry_t *ModbusMap_FindById(pyxis_modbus_reg_id_t id)
{
    size_t i;

    if ((uint32_t)id >= (uint32_t)PYXIS_REG_COUNT) {
        return NULL;
    }
    for (i = 0U; i < MAP_ENTRY_COUNT; ++i) {
        if (map_entries[i].id == id) {
            return &map_entries[i];
        }
    }
    return NULL;
}

bool ModbusMap_ValidateValue(
    const modbus_map_entry_t *entry, uint16_t value)
{
    if (entry == NULL) {
        return false;
    }
    if ((value < entry->minimum) || (value > entry->maximum)) {
        return false;
    }

    switch (entry->domain) {
    case MODBUS_MAP_DOMAIN_NUMERIC:
        return true;
    case MODBUS_MAP_DOMAIN_BOOLEAN:
        return value <= 1U;
    case MODBUS_MAP_DOMAIN_SLAVE_ID:
        return (value >= 1U) && (value <= 247U);
    case MODBUS_MAP_DOMAIN_BAUD_SELECTOR:
        return value <= 4U;
    case MODBUS_MAP_DOMAIN_FRAME_SELECTOR:
        return value <= 3U;
    case MODBUS_MAP_DOMAIN_MACHINE_PROFILE:
        return value <= 5U;
    case MODBUS_MAP_DOMAIN_BASELINE_COMMAND:
        return value <= 2U;
    case MODBUS_MAP_DOMAIN_SYSTEM_COMMAND:
        return (value == PYXIS_SYSTEM_COMMAND_NONE) ||
               (value == PYXIS_SYSTEM_COMMAND_RESTART) ||
               (value == PYXIS_SYSTEM_COMMAND_FACTORY_RESET);
    default:
        return false;
    }
}

bool ModbusMap_ValidateIntegrity(void)
{
    size_t i;
    size_t j;

    if (MAP_ENTRY_COUNT != (size_t)PYXIS_REG_COUNT) {
        return false;
    }

    for (i = 0U; i < MAP_ENTRY_COUNT; ++i) {
        const modbus_map_entry_t *a = &map_entries[i];
        uint32_t a_start = (uint32_t)a->start_address;
        uint32_t a_end = a_start + (uint32_t)a->count;

        if ((uint32_t)a->id >= (uint32_t)PYXIS_REG_COUNT ||
            (a->name == NULL) || (a->name[0] == '\0') ||
            !ModbusMap_IsAddressRangeValid(a->start_address, a->count) ||
            !map_type_is_valid(a->type) ||
            !map_access_is_valid(a->access) ||
            !map_persistence_is_valid(a->persistence) ||
            !map_apply_policy_is_valid(a->apply_policy) ||
            !map_domain_is_valid(a->domain) ||
            ((a->type == MODBUS_MAP_REGISTER_INPUT) &&
             (a->access != MODBUS_MAP_ACCESS_READ_ONLY)) ||
            !ModbusMap_ValidateValue(a, a->default_value)) {
            return false;
        }

        if ((a->apply_policy == MODBUS_APPLY_ON_BOOT) &&
            (a->access == MODBUS_MAP_ACCESS_READ_WRITE) &&
            (a->persistence != MODBUS_MAP_PERSISTENT)) {
            return false;
        }

        for (j = i + 1U; j < MAP_ENTRY_COUNT; ++j) {
            const modbus_map_entry_t *b = &map_entries[j];
            uint32_t b_start = (uint32_t)b->start_address;
            uint32_t b_end = b_start + (uint32_t)b->count;

            if ((a->id == b->id) ||
                ((a->name != NULL) && (b->name != NULL) &&
                 (a->name[0] != '\0') && (b->name[0] != '\0') &&
                 (strcmp(a->name, b->name) == 0))) {
                return false;
            }
            if ((a->type == b->type) &&
                (a_start < b_end) && (b_start < a_end)) {
                return false;
            }
        }
    }
    return true;
}
