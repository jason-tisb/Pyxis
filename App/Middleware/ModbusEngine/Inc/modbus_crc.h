#ifndef INC_MODBUS_CRC_H_
#define INC_MODBUS_CRC_H_

#include <stdint.h>

/**
 * @brief Calculate the Modbus RTU CRC-16.
 *
 * @param buffer Pointer to the input bytes. Must not be NULL.
 * @param buffer_length Number of bytes included in the calculation.
 * @return Calculated CRC-16 value.
 *
 * @note Modbus RTU transmits the low CRC byte first, followed by the high byte.
 */
uint16_t modbus_crc16(const uint8_t *buffer, uint16_t buffer_length);

#endif /* INC_MODBUS_CRC_H_ */