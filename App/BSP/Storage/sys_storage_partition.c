/* ==============================================================================
 * File:   sys_storage_partition.c
 * Desc:   System storage partition settings
 * ============================================================================== */
#include "sys_storage_partition.h"
#include "storage_kv.h"

static const KV_Partition_t sys_partitions[] = {
    { KV_KEY_MODBUS_CFG,     1,  48  },
    { KV_KEY_VIBRATION_CFG,  5,  64  },
    { KV_KEY_MODBUS_STATS,   11, 204 },
    { KV_KEY_VIB_RECORD,     30, 128 },
    { KV_KEY_VIB_BASELINE,   41, 128 },
    { KV_KEY_VIB_TACH,       52, 64  },
};

bool SysStorage_InitPartitions(void) {
    uint16_t partition_count = sizeof(sys_partitions) / sizeof(sys_partitions[0]);
    return Storage_Init(sys_partitions, partition_count);
}
