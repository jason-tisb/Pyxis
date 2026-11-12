/* Titania-derived Unified Autobaud V2 - Pyxis M2.
 * Side-effect-free any-ID Modbus RTU REQUEST validation.
 * Not connected to the active port or the legacy autobaud engine.
 */
#include "autobaud_validate.h"
#include "modbus_crc.h"

#include <stddef.h>
#include <string.h>

#define FC_READ_COILS          0x01U
#define FC_READ_DISCRETES      0x02U
#define FC_READ_HOLDING        0x03U
#define FC_READ_INPUTS         0x04U
#define FC_WRITE_COIL          0x05U
#define FC_WRITE_REGISTER      0x06U
#define FC_WRITE_COILS         0x0FU
#define FC_WRITE_REGISTERS     0x10U

#define MAX_READ_BITS          2000U
#define MAX_READ_REGISTERS     125U
#define MAX_WRITE_COILS        1968U
#define MAX_WRITE_REGISTERS    123U

/* The fixed-length ADU is address, FC, 4 data bytes and 2 CRC bytes.
 * Multi-write ADU is address, FC, start(2), qty(2), count(1),
 * payload(count), CRC(2).
 */
#define FIXED_REQUEST_LENGTH   8U
#define MULTI_REQUEST_PREFIX   9U

_Static_assert(MULTI_REQUEST_PREFIX +
                   (MAX_WRITE_REGISTERS * 2U) <=
                   AUTOBAUD_VALIDATE_MAX_ADU_LENGTH,
               "FC10 maximum request exceeds RTU ADU");
_Static_assert(MULTI_REQUEST_PREFIX +
                   ((MAX_WRITE_COILS + 7U) / 8U) <=
                   AUTOBAUD_VALIDATE_MAX_ADU_LENGTH,
               "FC0F maximum request exceeds RTU ADU");

static uint16_t read_be16(const uint8_t *bytes)
{
  return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static bool range_valid(uint16_t start, uint16_t quantity)
{
  return (quantity > 0U) &&
         (((uint32_t)start + (uint32_t)quantity) <= 65536U);
}

autobaud_validate_result_t autobaud_validate_request(
    const uint8_t *adu, uint16_t adu_len,
    const autobaud_validate_rx_evidence_t *evidence,
    autobaud_validate_request_info_t *out_info)
{
  uint16_t got_crc;
  uint16_t actual_crc;
  uint16_t start;
  uint16_t field;
  uint16_t max_quantity;
  uint16_t expected_bytes;
  uint8_t fc;
  uint8_t slave;
  bool broadcast;
  bool structure_ok = false;

  if (out_info != NULL) {
    memset(out_info, 0, sizeof(*out_info));
  }

  if ((adu == NULL) || (evidence == NULL)) {
    return AUTOBAUD_VALIDATE_NULL_ARGUMENT;
  }
  if (!evidence->complete_rtu_frame || !evidence->uart_error_free ||
      !evidence->transport_error_free || !evidence->fresh_after_profile_apply) {
    return AUTOBAUD_VALIDATE_UNTRUSTED_RX;
  }
  if ((adu_len < AUTOBAUD_VALIDATE_MIN_ADU_LENGTH) ||
      (adu_len > AUTOBAUD_VALIDATE_MAX_ADU_LENGTH)) {
    return AUTOBAUD_VALIDATE_INVALID_LENGTH;
  }

  got_crc = (uint16_t)adu[adu_len - 2U] |
            ((uint16_t)adu[adu_len - 1U] << 8U);
  actual_crc = modbus_crc16(adu, (uint16_t)(adu_len - 2U));
  if (got_crc != actual_crc) {
    return AUTOBAUD_VALIDATE_INVALID_CRC;
  }

  slave = adu[0];
  fc = adu[1];
  broadcast = (slave == 0U);

  /* Modbus unicast address space is 1..247; address 0 is broadcast.
   * 248..255 are reserved, not independently addressable slaves.
   */
  if (slave > 247U) {
    return AUTOBAUD_VALIDATE_INVALID_SLAVE_ADDRESS;
  }

  start = read_be16(&adu[2]);
  field = read_be16(&adu[4]);

  switch (fc) {
  case FC_READ_COILS:
  case FC_READ_DISCRETES:
  case FC_READ_HOLDING:
  case FC_READ_INPUTS:
    max_quantity = ((fc == FC_READ_COILS) || (fc == FC_READ_DISCRETES))
                       ? MAX_READ_BITS : MAX_READ_REGISTERS;
    structure_ok = !broadcast &&
                   (adu_len == FIXED_REQUEST_LENGTH) &&
                   (field <= max_quantity) && range_valid(start, field);
    break;

  case FC_WRITE_COIL:
    structure_ok = (adu_len == FIXED_REQUEST_LENGTH) &&
                   ((field == 0x0000U) || (field == 0xFF00U));
    break;

  case FC_WRITE_REGISTER:
    structure_ok = (adu_len == FIXED_REQUEST_LENGTH);
    break;

  case FC_WRITE_COILS:
  case FC_WRITE_REGISTERS:
    if (adu_len < MULTI_REQUEST_PREFIX) {
      break;
    }
    max_quantity = (fc == FC_WRITE_COILS)
                       ? MAX_WRITE_COILS : MAX_WRITE_REGISTERS;
    if ((field == 0U) || (field > max_quantity) ||
        !range_valid(start, field)) {
      break;
    }
    expected_bytes = (fc == FC_WRITE_COILS)
                         ? (uint16_t)((field + 7U) / 8U)
                         : (uint16_t)(field * 2U);
    structure_ok = (adu[6] == expected_bytes) &&
                   ((uint32_t)adu_len ==
                    ((uint32_t)MULTI_REQUEST_PREFIX + expected_bytes));
    break;

  default:
    return AUTOBAUD_VALIDATE_UNSUPPORTED_FUNCTION;
  }

  if (!structure_ok) {
    return AUTOBAUD_VALIDATE_INVALID_STRUCTURE;
  }

  if (out_info != NULL) {
    out_info->slave_id = slave;
    out_info->function_code = fc;
    out_info->start_address = start;
    out_info->is_broadcast = broadcast;
    if ((fc == FC_WRITE_COIL) || (fc == FC_WRITE_REGISTER)) {
      out_info->value = field;
    } else {
      out_info->quantity = field;
    }
  }
  return AUTOBAUD_VALIDATE_ACCEPTED;
}
