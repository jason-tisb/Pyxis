#ifndef NON_VOLATILE_DATA_H
#define NON_VOLATILE_DATA_H
#include "stm32h5xx_hal.h"
#include <stdint.h>
#include "stm32h5xx_hal_icache.h"

//#define FLASH_SECTOR_SIZE     			(0x2000U)	/* 8 KB per sector on STM32H563 */
#define FLASH_USER_START_ADDR 			(0x081FE000UL)	/* second last sector of 2MB flash */

#define FLASH_ADDR_MODBUS_SETTINGS 		(FLASH_USER_START_ADDR + 0L)

/*+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  |15|14|13|12|11|10|09|08|07|06|05|04|03|02|01|00|
  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  |SB| PAR |   BAUDRATE   |      SLAVE ID         |
  +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+*/
#define MODBUS_SLAVE_ID(VALUE)			(VALUE         & 0x000000FF)		// 8-bit
#define MODBUS_BAUDRATE(VALUE)			((VALUE >> 8)  & 0x0000001F)	// 5-bit
#define MODBUS_PARITY(VALUE)			((VALUE >> 13) & 0x00000003)	// 2-bit
#define MODBUS_STOPBIT(VALUE)			((VALUE >> 15) & 0x00000001)	// 1-bit

void Flash_EraseSector(uint32_t SectorAddress);
void Flash_WriteData(uint32_t startAddress, const uint32_t *data, uint32_t length_words);
uint32_t Flash_ReadData(uint32_t address);

#endif  /* NON_VOLATILE_DATA_H */
