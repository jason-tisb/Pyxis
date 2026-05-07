#include "NonVolatileData.h"
#include "stm32h5xx_hal.h"
#include "stm32h5xx_hal_flash.h"
#include "stm32h5xx_hal_flash_ex.h"

#include "NonVolatileData.h"


/* Erase one 8 KB flash sector */
void Flash_EraseSector(uint32_t SectorAddress)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef EraseInit = {0};
    uint32_t SectorError = 0;

    EraseInit.TypeErase  = FLASH_TYPEERASE_SECTORS;
    EraseInit.Banks      = FLASH_BANK_1;
    EraseInit.Sector     = (SectorAddress - FLASH_BASE) / FLASH_SECTOR_SIZE;
    EraseInit.NbSectors  = 1;

    if (HAL_FLASHEx_Erase(&EraseInit, &SectorError) != HAL_OK)
    {
        uint32_t err = HAL_FLASH_GetError();
        (void)err; // for debugging
    }

    while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY));  // critical
    HAL_FLASH_Lock();
}

/* Write 32-bit words using 128-bit programming granularity */
void Flash_WriteData(uint32_t startAddress, const uint32_t *data, uint32_t length_words)
{
    HAL_FLASH_Unlock();

    for (uint32_t i = 0; i < length_words; i += 4)
    {
        /* Pack 4 words (16 bytes) into a temporary 16-byte buffer in RAM */
        uint64_t quadword[2];
        quadword[0] = ((uint64_t)data[i + 1] << 32) | data[i];
        quadword[1] = ((uint64_t)data[i + 3] << 32) | data[i + 2];

        /* Pass the RAM address of the 16-byte block */
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, startAddress + i * 4, (uint32_t)quadword) != HAL_OK)
        {
            uint32_t err = HAL_FLASH_GetError();
            (void)err;
            break;
        }

        while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY));
    }

    /* === Flush caches and make sure flash really idle === */
    while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY));

    HAL_ICACHE_Disable();     // disables and flushes the cache
    HAL_ICACHE_Enable();      // re-enables a clean cache

    HAL_FLASH_Lock();
}

/* Simple read */
uint32_t Flash_ReadData(uint32_t address)
{
	HAL_FLASH_Unlock();
    uint32_t d = *(volatile uint32_t *)address;
    HAL_FLASH_Lock();
    return d;
}

