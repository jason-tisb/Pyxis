#include "modbus_crc.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Calculate the Modbus RTU CRC-16.
 *
 * @param buffer Pointer to the data bytes.
 * @param buffer_length Number of bytes included in the CRC calculation.
 *
 * @return Calculated CRC-16 value.
 *
 * @note buffer must not be NULL.
 * @note Modbus RTU transmits the low CRC byte first, followed by the high byte.
 */
uint16_t modbus_crc16(
    const uint8_t *buffer,
    uint16_t buffer_length)
{
    uint16_t crc = 0xFFFFU;

    if (buffer == NULL) {
        assert(false);
        return 0U;
    }

    for (uint16_t position = 0U;
         position < buffer_length;
         position++) {

        crc ^= (uint16_t)buffer[position];

        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)(
                    (crc >> 1U) ^ 0xA001U);
            } else {
                crc >>= 1U;
            }
        }
    }

    return crc;
}