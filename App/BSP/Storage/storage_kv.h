/* ==============================================================================
 * File:   storage_kv.h
 * Desc:   Persistent key-value storage interface
 * ============================================================================== */

#ifndef APP_STORAGE_KV_H_
#define APP_STORAGE_KV_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KV_KEY_MODBUS_CFG    = 0x01,
    KV_KEY_VIBRATION_CFG = 0x02,
    /* Milestone H1: dual-slot canonical Modbus persistence. */
    KV_KEY_MODBUS_CFG_SLOT_A = 0x03,
    KV_KEY_MODBUS_CFG_SLOT_B = 0x04,
    KV_KEY_MODBUS_CFG_COMMIT_A = 0x05,
    KV_KEY_MODBUS_CFG_COMMIT_B = 0x06,
    KV_KEY_MODBUS_CFG_LEGACY_BLOCK = 0x07,
    KV_KEY_MODBUS_STATS  = 0x11,
    KV_KEY_VIB_RECORD    = 0x12,
    KV_KEY_VIB_BASELINE  = 0x13,
    KV_KEY_VIB_TACH      = 0x14,
} StorageKey_t;

typedef struct {
    StorageKey_t key;
    uint16_t virt_base;
    uint16_t max_size;
} KV_Partition_t;

bool Storage_Init(const KV_Partition_t* user_table, uint16_t count);
bool Storage_Read(StorageKey_t key, void* out_buffer, uint16_t size);
bool Storage_Write(StorageKey_t key, const void* in_data, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif
