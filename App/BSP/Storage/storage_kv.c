/* --- storage_kv.c --- */
#include "storage_kv.h"
#include "eeprom_emul.h"
#include <string.h>

#include "stm32h5xx_hal.h"

#if (FEATURE_STORAGE_USE_RTOS == 1)
#include "cmsis_os2.h"
#endif

volatile uint8_t ErasingOnGoing = 0;

static const KV_Partition_t* p_sys_partitions = NULL;
static uint16_t sys_partition_count = 0;

#if (FEATURE_STORAGE_USE_RTOS == 1)
static osMutexId_t kv_mutex = NULL;
osSemaphoreId_t kv_erase_sem = NULL;
#endif

static const KV_Partition_t* Get_Partition(StorageKey_t key) {
    if (p_sys_partitions == NULL) return NULL;
    for (uint16_t i = 0; i < sys_partition_count; i++) {
        if (p_sys_partitions[i].key == key) return &p_sys_partitions[i];
    }
    return NULL;
}

static void Wait_For_Erase_Complete(void) {
    while (ErasingOnGoing == 1) {
#if (FEATURE_STORAGE_USE_RTOS == 1)
        osSemaphoreAcquire(kv_erase_sem, 1000);
#else
        __asm("nop");
#endif
    }
}

bool Storage_Init(const KV_Partition_t* partitions, uint16_t count) {
    if (partitions == NULL || count == 0) return false;

    p_sys_partitions = partitions;
    sys_partition_count = count;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t ids_needed_A = (p_sys_partitions[i].max_size + 11) / 12;
        uint16_t end_id_A = p_sys_partitions[i].virt_base + ids_needed_A - 1;

        if (end_id_A > EEPROM_MAX_VIRTUAL_ID) return false;

        for (uint16_t j = i + 1; j < count; j++) {
            uint16_t ids_needed_B = (p_sys_partitions[j].max_size + 11) / 12;
            uint16_t end_id_B = p_sys_partitions[j].virt_base + ids_needed_B - 1;
            if (!(end_id_A < p_sys_partitions[j].virt_base || end_id_B < p_sys_partitions[i].virt_base)) {
                return false;
            }
        }
    }

#if (FEATURE_STORAGE_USE_RTOS == 1)
    if (kv_mutex == NULL) kv_mutex = osMutexNew(NULL);
    if (kv_erase_sem == NULL) kv_erase_sem = osSemaphoreNew(1, 0, NULL);
#endif

    HAL_FLASH_Unlock();
    EE_Status status = EE_Init(EE_CONDITIONAL_ERASE);
    HAL_FLASH_Lock();
    if (status != EE_OK) {
        status = EE_Init(EE_FORCED_ERASE);
    }

    return (status == EE_OK);
}

bool Storage_Write(StorageKey_t key, const void* in_data, uint16_t size) {
    if (in_data == NULL || size == 0) return false;

    const KV_Partition_t* part = Get_Partition(key);
    if (part == NULL || size > part->max_size) return false;

#if (FEATURE_STORAGE_USE_RTOS == 1)
    if (osMutexAcquire(kv_mutex, osWaitForever) != osOK) return false;
#endif

    uint16_t chunks = (size + 11) / 12;
    const uint8_t* p_src = (const uint8_t*)in_data;
    uint64_t tmp_96bit[2];
    bool success = true;

    for (uint16_t i = 0; i < chunks; i++) {
        Wait_For_Erase_Complete();

        memset(tmp_96bit, 0, sizeof(tmp_96bit));
        uint16_t remain = size - (i * 12);
        uint16_t copy_len = (remain >= 12) ? 12 : remain;
        memcpy(tmp_96bit, p_src + (i * 12), copy_len);

        HAL_FLASH_Unlock();
        EE_Status ee_status = EE_WriteVariable96bits(part->virt_base + i, tmp_96bit);
        HAL_FLASH_Lock();
        if ((ee_status & EE_STATUSMASK_ERROR) == EE_STATUSMASK_ERROR) {
            success = false;
            break;
        }

        if ((ee_status & EE_STATUSMASK_CLEANUP) == EE_STATUSMASK_CLEANUP) {
            ErasingOnGoing = 1;
            HAL_FLASH_Unlock();
            if (EE_CleanUp_IT() != EE_OK) {
                ErasingOnGoing = 0;
            }
            HAL_FLASH_Lock();
        }
    }

#if (FEATURE_STORAGE_USE_RTOS == 1)
    osMutexRelease(kv_mutex);
#endif
    return success;
}

bool Storage_Read(StorageKey_t key, void* out_buffer, uint16_t size) {
    if (out_buffer == NULL || size == 0) return false;

    const KV_Partition_t* part = Get_Partition(key);
    if (part == NULL || size > part->max_size) return false;

#if (FEATURE_STORAGE_USE_RTOS == 1)
    if (osMutexAcquire(kv_mutex, osWaitForever) != osOK) return false;
#endif

    uint16_t chunks = (size + 11) / 12;
    uint8_t* p_dest = (uint8_t*)out_buffer;
    uint64_t tmp_96bit[2];
    bool success = true;

    for (uint16_t i = 0; i < chunks; i++) {
        Wait_For_Erase_Complete();

        if (EE_ReadVariable96bits(part->virt_base + i, tmp_96bit) == EE_OK) {
            uint16_t remain = size - (i * 12);
            uint16_t copy_len = (remain >= 12) ? 12 : remain;
            memcpy(p_dest + (i * 12), tmp_96bit, copy_len);
        } else {
            success = false;
            break;
        }
    }

#if (FEATURE_STORAGE_USE_RTOS == 1)
    osMutexRelease(kv_mutex);
#endif
    return success;
}
