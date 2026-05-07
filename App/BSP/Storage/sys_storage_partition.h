/* ==============================================================================
 * File:   sys_storage_partition.h
 * Desc:   System-level storage partition setup
 * ============================================================================== */
#ifndef SYS_STORAGE_PARTITION_H_
#define SYS_STORAGE_PARTITION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

bool SysStorage_InitPartitions(void);

#ifdef __cplusplus
}
#endif

#endif /* SYS_STORAGE_PARTITION_H_ */
