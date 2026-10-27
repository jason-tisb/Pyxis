/* ==============================================================================
 * File:   sys_storage_partition.c
 * Desc:   System storage partition settings
 * ============================================================================== */
#include "sys_storage_partition.h"
#include "storage_kv.h"

static const KV_Partition_t sys_partitions[] = {
    { KV_KEY_MODBUS_CFG,     1,  48  },
    { KV_KEY_VIBRATION_CFG,  5,  64  },
    /* H1 body slots are deliberately separate from the legacy key.  The
     * commit words are separate 12-byte records so the persistence manager
     * can make the logical commit operation backend-granularity-safe. */
    { KV_KEY_MODBUS_CFG_SLOT_A, 60, 640 },
    { KV_KEY_MODBUS_CFG_SLOT_B, 115, 640 },
    { KV_KEY_MODBUS_CFG_COMMIT_A, 170, 12 },
    { KV_KEY_MODBUS_CFG_COMMIT_B, 172, 12 },
    { KV_KEY_MODBUS_CFG_LEGACY_BLOCK, 174, 4 },
    { KV_KEY_MODBUS_STATS,   11, 204 },
    { KV_KEY_VIB_RECORD,     30, 128 },
    { KV_KEY_VIB_BASELINE,   41, 128 },
    { KV_KEY_VIB_TACH,       52, 64  },
};

bool SysStorage_InitPartitions(void) {
    uint16_t partition_count = sizeof(sys_partitions) / sizeof(sys_partitions[0]);
    return Storage_Init(sys_partitions, partition_count);
}
