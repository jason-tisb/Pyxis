#include "modbus_persistence.h"

#include <stddef.h>
#include <string.h>

#include "modbus_engine_map.h"
#include "modbus_map.h"
#include "modbus_register_map.h"

#if !MODBUS_G_TEST_HOOKS
#include "storage_kv.h"
#endif

static uint16_t *s_holding_storage;
static uint16_t s_holding_word_count;
static modbus_persistent_payload_t s_verified_payload;
static bool s_verified_valid;
static bool s_dirty;
static uint8_t s_verified_slot;
static uint32_t s_verified_sequence;
static modbus_persistence_status_t s_last_status = MODBUS_PERSISTENCE_NO_VALID_RECORD;

#define MODBUS_LEGACY_BLOCK_MARKER 0x4C424C31UL
#if MODBUS_G_TEST_HOOKS
static bool s_test_legacy_blocked;
static modbus_persistence_test_result_t s_test_result;
#endif

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    size_t i;
    for (i = 0U; i < length; ++i) {
        uint32_t bit;
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return crc;
}

static uint32_t record_crc(const modbus_persistent_record_t *record)
{
    return crc32_update(0xFFFFFFFFUL, (const uint8_t *)record,
                        offsetof(modbus_persistent_record_t, crc32)) ^
           0xFFFFFFFFUL;
}

static bool sequence_is_newer(uint32_t lhs, uint32_t rhs)
{
    /* RFC1982-style serial arithmetic; the half-range case is unreachable
     * for normal sequential saves and is intentionally treated as older. */
    return ((int32_t)(lhs - rhs) > 0);
}

static bool entry_is_persistent(const modbus_map_entry_t *entry)
{
    return (entry != NULL) &&
           (entry->type == MODBUS_MAP_REGISTER_HOLDING) &&
           (entry->persistence == MODBUS_MAP_PERSISTENT);
}

static bool payload_index_for_address(uint16_t address, uint16_t *index)
{
    const modbus_map_entry_t *entries = ModbusMap_Entries();
    uint16_t count = ModbusMap_Count();
    uint16_t cursor = 0U;
    uint16_t i;

    if (index != NULL) {
        *index = 0U;
    }
    if (index == NULL) {
        return false;
    }
    for (i = 0U; i < count; ++i) {
        const modbus_map_entry_t *entry = &entries[i];
        uint16_t offset;
        if (!entry_is_persistent(entry)) {
            continue;
        }
        for (offset = 0U; offset < entry->count; ++offset) {
            if ((uint32_t)entry->start_address + offset == address) {
                *index = cursor;
                return true;
            }
            ++cursor;
        }
    }
    return false;
}

static bool validate_payload(const modbus_persistent_payload_t *payload)
{
    const modbus_map_entry_t *entries = ModbusMap_Entries();
    uint16_t map_count = ModbusMap_Count();
    uint16_t cursor = 0U;
    uint16_t i;

    if ((payload == NULL) || (payload->word_count > MODBUS_PERSISTENCE_MAX_WORDS)) {
        return false;
    }
    for (i = 0U; i < map_count; ++i) {
        const modbus_map_entry_t *entry = &entries[i];
        uint16_t offset;
        if (!entry_is_persistent(entry)) {
            continue;
        }
        for (offset = 0U; offset < entry->count; ++offset) {
            if (cursor >= payload->word_count ||
                !ModbusMap_ValidateValue(entry, payload->words[cursor])) {
                return false;
            }
            ++cursor;
        }
    }
    return cursor == payload->word_count;
}

void ModbusPersistence_BindHoldingStorage(uint16_t *holding_storage,
                                          uint16_t holding_word_count)
{
    s_holding_storage = holding_storage;
    s_holding_word_count = holding_word_count;
}

bool ModbusPersistence_BuildShadow(modbus_persistent_payload_t *out_payload)
{
    const modbus_map_entry_t *entries = ModbusMap_Entries();
    uint16_t map_count = ModbusMap_Count();
    uint16_t cursor = 0U;
    uint16_t i;

    if (out_payload == NULL) {
        return false;
    }
    memset(out_payload, 0, sizeof(*out_payload));
    for (i = 0U; i < map_count; ++i) {
        const modbus_map_entry_t *entry = &entries[i];
        uint16_t offset;
        if (!entry_is_persistent(entry)) {
            continue;
        }
        for (offset = 0U; offset < entry->count; ++offset) {
            if (cursor >= MODBUS_PERSISTENCE_MAX_WORDS) {
                return false;
            }
            if ((s_holding_storage != NULL) &&
                ((uint32_t)entry->start_address + offset < s_holding_word_count)) {
                out_payload->words[cursor] =
                    s_holding_storage[entry->start_address + offset];
            } else {
                out_payload->words[cursor] = entry->default_value;
            }
            ++cursor;
        }
    }
    out_payload->word_count = cursor;
    return validate_payload(out_payload);
}

static bool build_default_shadow(modbus_persistent_payload_t *out_payload)
{
    const modbus_map_entry_t *entries = ModbusMap_Entries();
    uint16_t map_count = ModbusMap_Count();
    uint16_t cursor = 0U;
    uint16_t i;

    if (out_payload == NULL) {
        return false;
    }
    memset(out_payload, 0, sizeof(*out_payload));
    for (i = 0U; i < map_count; ++i) {
        const modbus_map_entry_t *entry = &entries[i];
        uint16_t offset;
        if (!entry_is_persistent(entry)) {
            continue;
        }
        for (offset = 0U; offset < entry->count; ++offset) {
            if (cursor >= MODBUS_PERSISTENCE_MAX_WORDS) {
                return false;
            }
            out_payload->words[cursor++] = entry->default_value;
        }
    }
    out_payload->word_count = cursor;
    return validate_payload(out_payload);
}

bool ModbusPersistence_ApplyShadow(const modbus_persistent_payload_t *payload)
{
    const modbus_map_entry_t *entries = ModbusMap_Entries();
    uint16_t map_count = ModbusMap_Count();
    uint16_t cursor = 0U;
    uint16_t i;

    if ((s_holding_storage == NULL) || !validate_payload(payload)) {
        return false;
    }
    /* Prove the complete destination range before publishing any word. */
    for (i = 0U; i < map_count; ++i) {
        const modbus_map_entry_t *entry = &entries[i];
        if (entry_is_persistent(entry) &&
            ((uint32_t)entry->start_address + entry->count > s_holding_word_count)) {
            return false;
        }
    }
    for (i = 0U; i < map_count; ++i) {
        const modbus_map_entry_t *entry = &entries[i];
        uint16_t offset;
        if (!entry_is_persistent(entry)) {
            continue;
        }
        for (offset = 0U; offset < entry->count; ++offset) {
            if ((uint32_t)entry->start_address + offset >= s_holding_word_count) {
                return false;
            }
            s_holding_storage[entry->start_address + offset] = payload->words[cursor++];
        }
    }
    return true;
}

static void payload_override_comm(modbus_persistent_payload_t *payload,
                                  const ModbusConfig_t *config)
{
    uint16_t index;
    if (payload == NULL || config == NULL) {
        return;
    }
    if (payload_index_for_address(PYXIS_REG_SLAVE_ID_ADDR, &index)) {
        payload->words[index] = config->slave_id;
    }
    if (payload_index_for_address(PYXIS_REG_BAUD_SELECTOR_ADDR, &index)) {
        uint16_t code = 4U;
        switch (config->baud_rate) {
        case 0U: code = 0U; break;
        case 9600U: code = 1U; break;
        case 19200U: code = 2U; break;
        case 38400U: code = 3U; break;
        case 115200U: code = 4U; break;
        default: break;
        }
        payload->words[index] = code;
    }
    if (payload_index_for_address(PYXIS_REG_FRAME_SELECTOR_ADDR, &index)) {
        payload->words[index] = config->parity;
    }
}

#if !MODBUS_G_TEST_HOOKS

static StorageKey_t body_key(uint8_t slot)
{
    return (slot == 0U) ? KV_KEY_MODBUS_CFG_SLOT_A : KV_KEY_MODBUS_CFG_SLOT_B;
}

static StorageKey_t commit_key(uint8_t slot)
{
    return (slot == 0U) ? KV_KEY_MODBUS_CFG_COMMIT_A : KV_KEY_MODBUS_CFG_COMMIT_B;
}

static bool read_slot(uint8_t slot, modbus_persistent_record_t *record)
{
    uint32_t marker = 0U;
    if (record == NULL ||
        !Storage_Read(body_key(slot), record, (uint16_t)sizeof(*record)) ||
        !Storage_Read(commit_key(slot), &marker, (uint16_t)sizeof(marker))) {
        return false;
    }
    if ((marker != MODBUS_PERSISTENCE_COMMIT_MARKER) ||
        (record->magic != MODBUS_PERSISTENCE_MAGIC) ||
        (record->format_version != MODBUS_PERSISTENCE_FORMAT_VERSION) ||
        (record->payload_length != sizeof(record->payload)) ||
        !validate_payload(&record->payload) ||
        (record->crc32 != record_crc(record))) {
        return false;
    }
    return true;
}

static bool write_slot(uint8_t slot, const modbus_persistent_record_t *record)
{
    uint32_t marker = 0U;
    modbus_persistent_record_t verify;
    uint32_t committed;

    /* Clear the inactive slot's logical commit before touching its body. */
    if (!Storage_Write(commit_key(slot), &marker, (uint16_t)sizeof(marker)) ||
        !Storage_Write(body_key(slot), record, (uint16_t)sizeof(*record)) ||
        !Storage_Read(body_key(slot), &verify, (uint16_t)sizeof(verify)) ||
        (memcmp(&verify, record, sizeof(verify)) != 0) ||
        !Storage_Write(commit_key(slot), &(uint32_t){MODBUS_PERSISTENCE_COMMIT_MARKER},
                       (uint16_t)sizeof(uint32_t)) ||
        !Storage_Read(commit_key(slot), &committed, (uint16_t)sizeof(committed)) ||
        (committed != MODBUS_PERSISTENCE_COMMIT_MARKER)) {
        return false;
    }
    return read_slot(slot, &verify);
}

#else

static modbus_persistent_record_t s_test_records[2];
static bool s_test_committed[2];
static uint32_t s_test_save_count;

static bool read_slot(uint8_t slot, modbus_persistent_record_t *record)
{
    if ((slot > 1U) || (record == NULL) || !s_test_committed[slot]) {
        return false;
    }
    *record = s_test_records[slot];
    if ((s_test_result == MODBUS_PERSIST_TEST_INVALID_STORED_DATA) ||
        (s_test_result == MODBUS_PERSIST_TEST_CORRUPT_CRC)) {
        record->crc32 ^= 1U;
    }
    if (s_test_result == MODBUS_PERSIST_TEST_INVALID_VERSION) {
        record->format_version++;
    }
    return (record->magic == MODBUS_PERSISTENCE_MAGIC) &&
           (record->format_version == MODBUS_PERSISTENCE_FORMAT_VERSION) &&
           (record->payload_length == sizeof(record->payload)) &&
           validate_payload(&record->payload) &&
           (record->crc32 == record_crc(record));
}

static bool write_slot(uint8_t slot, const modbus_persistent_record_t *record)
{
    if ((slot > 1U) || (record == NULL) ||
        (s_test_result == MODBUS_PERSIST_TEST_ERASE_FAIL) ||
        (s_test_result == MODBUS_PERSIST_TEST_WRITE_FAIL) ||
        (s_test_result == MODBUS_PERSIST_TEST_VERIFY_FAIL) ||
        (s_test_result == MODBUS_PERSIST_TEST_COMMIT_FAIL) ||
        (s_test_result == MODBUS_PERSIST_TEST_POWER_LOSS)) {
        return false;
    }
    s_test_records[slot] = *record;
    s_test_committed[slot] = true;
    return true;
}

#endif

static bool choose_record(modbus_persistent_record_t *record, uint8_t *slot)
{
    modbus_persistent_record_t a;
    modbus_persistent_record_t b;
    bool valid_a = read_slot(0U, &a);
    bool valid_b = read_slot(1U, &b);

    if (!valid_a && !valid_b) {
        return false;
    }
    if (valid_a && (!valid_b || sequence_is_newer(a.sequence, b.sequence))) {
        *record = a;
        *slot = 0U;
    } else {
        /* Equal sequences deterministically select slot B only when B is the
         * sole valid choice; otherwise slot A wins. */
        *record = b;
        *slot = 1U;
    }
    if (valid_a && valid_b && (a.sequence == b.sequence)) {
        *record = a;
        *slot = 0U;
    }
    return true;
}

bool ModbusPersistence_IsLegacyConfigBlocked(void)
{
#if MODBUS_G_TEST_HOOKS
    return s_test_legacy_blocked;
#else
    uint32_t marker = 0U;
    return Storage_Read(KV_KEY_MODBUS_CFG_LEGACY_BLOCK,
                        &marker, (uint16_t)sizeof(marker)) &&
           (marker == MODBUS_LEGACY_BLOCK_MARKER);
#endif
}

bool ModbusPersistence_BlockLegacyConfig(void)
{
#if MODBUS_G_TEST_HOOKS
    if (s_test_result == MODBUS_PERSIST_TEST_LEGACY_BLOCK_FAIL) {
        return false;
    }
    s_test_legacy_blocked = true;
    return true;
#else
    uint32_t marker = MODBUS_LEGACY_BLOCK_MARKER;
    uint32_t verify = 0U;
    if (!Storage_Write(KV_KEY_MODBUS_CFG_LEGACY_BLOCK,
                       &marker, (uint16_t)sizeof(marker)) ||
        !Storage_Read(KV_KEY_MODBUS_CFG_LEGACY_BLOCK,
                      &verify, (uint16_t)sizeof(verify))) {
        return false;
    }
    return verify == MODBUS_LEGACY_BLOCK_MARKER;
#endif
}

bool ModbusPersistence_LoadConfig(ModbusConfig_t *out_config)
{
    modbus_persistent_record_t record;
    uint8_t slot;
    uint16_t index;

    if (out_config == NULL) {
        s_last_status = MODBUS_PERSISTENCE_INVALID_ARGUMENT;
        return false;
    }
    if (!choose_record(&record, &slot)) {
#if !MODBUS_G_TEST_HOOKS
        /* Preserve devices written by the pre-H1 single-key backend.  The
         * legacy record is treated as an unverified migration source: it is
         * returned for this boot and becomes H1-managed on the next save. */
        if (!ModbusPersistence_IsLegacyConfigBlocked() &&
            Storage_Read(KV_KEY_MODBUS_CFG, out_config,
                         (uint16_t)sizeof(*out_config)) &&
            ModbusModule_ValidateConfig(out_config)) {
            s_verified_valid = false;
            s_dirty = true;
            s_last_status = MODBUS_PERSISTENCE_NO_VALID_RECORD;
            return true;
        }
#endif
        s_verified_valid = false;
        s_dirty = true;
        s_last_status = MODBUS_PERSISTENCE_NO_VALID_RECORD;
        return false;
    }
    if (s_holding_storage != NULL) {
        if (!ModbusPersistence_ApplyShadow(&record.payload)) {
            s_verified_valid = false;
            s_dirty = true;
            s_last_status = MODBUS_PERSISTENCE_INVALID_PAYLOAD;
            return false;
        }
    }
    memset(out_config, 0, sizeof(*out_config));
    if (!payload_index_for_address(PYXIS_REG_SLAVE_ID_ADDR, &index)) return false;
    out_config->slave_id = (uint8_t)record.payload.words[index];
    if (!payload_index_for_address(PYXIS_REG_BAUD_SELECTOR_ADDR, &index)) return false;
    switch (record.payload.words[index]) {
    case 0U: out_config->baud_rate = 0U; break;
    case 1U: out_config->baud_rate = 9600U; break;
    case 2U: out_config->baud_rate = 19200U; break;
    case 3U: out_config->baud_rate = 38400U; break;
    default: out_config->baud_rate = 115200U; break;
    }
    if (!payload_index_for_address(PYXIS_REG_FRAME_SELECTOR_ADDR, &index)) return false;
    out_config->parity = (uint8_t)record.payload.words[index];
    s_verified_payload = record.payload;
    s_verified_valid = true;
    s_dirty = false;
    s_verified_slot = slot;
    s_verified_sequence = record.sequence;
    s_last_status = MODBUS_PERSISTENCE_OK;
    return true;
}

static bool save_payload(const modbus_persistent_payload_t *payload)
{
    modbus_persistent_record_t record;
    uint8_t slot;

    if (payload == NULL) {
        s_last_status = MODBUS_PERSISTENCE_INVALID_PAYLOAD;
        s_dirty = true;
        return false;
    }
    if (!validate_payload(payload)) {
        s_last_status = MODBUS_PERSISTENCE_INVALID_PAYLOAD;
        s_dirty = true;
        return false;
    }
    if (s_verified_valid &&
        (memcmp(payload, &s_verified_payload, sizeof(*payload)) == 0)) {
        s_dirty = false;
        s_last_status = MODBUS_PERSISTENCE_UNCHANGED;
        return true;
    }

    memset(&record, 0, sizeof(record));
    record.magic = MODBUS_PERSISTENCE_MAGIC;
    record.format_version = MODBUS_PERSISTENCE_FORMAT_VERSION;
    record.payload_length = sizeof(record.payload);
    record.sequence = s_verified_valid ? (s_verified_sequence + 1U) : 1U;
    record.payload = *payload;
    record.crc32 = record_crc(&record);
    slot = s_verified_valid ? (uint8_t)(s_verified_slot ^ 1U) : 0U;
#if MODBUS_G_TEST_HOOKS
    ++s_test_save_count;
#endif
    if (!write_slot(slot, &record)) {
        s_dirty = true;
        s_last_status = MODBUS_PERSISTENCE_STORAGE_ERROR;
        return false;
    }
    s_verified_payload = *payload;
    s_verified_valid = true;
    s_dirty = false;
    s_verified_slot = slot;
    s_verified_sequence = record.sequence;
    s_last_status = MODBUS_PERSISTENCE_OK;
    return true;
}

bool ModbusPersistence_SaveConfig(const ModbusConfig_t *config)
{
    modbus_persistent_payload_t payload;

    if (config == NULL || !ModbusPersistence_BuildShadow(&payload)) {
        s_last_status = MODBUS_PERSISTENCE_INVALID_PAYLOAD;
        s_dirty = true;
        return false;
    }
    payload_override_comm(&payload, config);
    return save_payload(&payload);
}

bool ModbusPersistence_SaveFactoryDefaults(const ModbusConfig_t *config)
{
    modbus_persistent_payload_t payload;

    if (config == NULL || !build_default_shadow(&payload)) {
        s_last_status = MODBUS_PERSISTENCE_INVALID_PAYLOAD;
        s_dirty = true;
        return false;
    }
    payload_override_comm(&payload, config);
    return save_payload(&payload);
}

bool ModbusPersistence_IsDirty(void)
{
    return s_dirty;
}

bool ModbusPersistence_HasVerifiedRecord(void)
{
    return s_verified_valid;
}

uint32_t ModbusPersistence_GetVerifiedSequence(void)
{
    return s_verified_sequence;
}

modbus_persistence_status_t ModbusPersistence_GetLastStatus(void)
{
    return s_last_status;
}

#if MODBUS_G_TEST_HOOKS

void ModbusPersistence_TestReset(void)
{
    memset(s_test_records, 0, sizeof(s_test_records));
    memset(s_test_committed, 0, sizeof(s_test_committed));
    memset(&s_verified_payload, 0, sizeof(s_verified_payload));
    s_test_save_count = 0U;
    s_test_result = MODBUS_PERSIST_TEST_SUCCESS;
    s_test_legacy_blocked = false;
    s_verified_valid = false;
    s_dirty = false;
    s_verified_slot = 0U;
    s_verified_sequence = 0U;
    s_last_status = MODBUS_PERSISTENCE_NO_VALID_RECORD;
}

void ModbusPersistence_TestSetResult(modbus_persistence_test_result_t result)
{
    s_test_result = result;
}

bool ModbusPersistence_TestSetStoredConfig(const ModbusConfig_t *config)
{
    return ModbusPersistence_SaveConfig(config);
}

bool ModbusPersistence_TestGetStoredConfig(ModbusConfig_t *out_config)
{
    return ModbusPersistence_LoadConfig(out_config);
}

uint32_t ModbusPersistence_TestGetSaveCount(void)
{
    return s_test_save_count;
}

bool ModbusPersistence_TestSimulateBoot(ModbusConfig_t *loaded_config)
{
    s_verified_valid = false;
    s_dirty = false;
    return ModbusPersistence_LoadConfig(loaded_config);
}

bool ModbusPersistence_TestGetSlot(uint8_t slot,
                                   modbus_persistent_record_t *record)
{
    if ((record == NULL) || (slot > 1U) || !s_test_committed[slot]) return false;
    *record = s_test_records[slot];
    return true;
}

bool ModbusPersistence_TestSetSlot(uint8_t slot,
                                   const modbus_persistent_record_t *record,
                                   bool committed)
{
    if ((record == NULL) || (slot > 1U)) return false;
    s_test_records[slot] = *record;
    s_test_committed[slot] = committed;
    return true;
}

#endif
