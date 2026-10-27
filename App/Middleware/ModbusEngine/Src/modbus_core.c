#include "modbus_core.h"
#include "modbus_crc.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MODBUS_FC_READ_COILS 0x01U
#define MODBUS_FC_READ_DISCRETE_INPUTS 0x02U
#define MODBUS_FC_READ_HOLDING_REGISTERS 0x03U
#define MODBUS_FC_READ_INPUT_REGISTERS 0x04U
#define MODBUS_FC_WRITE_SINGLE_COIL 0x05U
#define MODBUS_FC_WRITE_SINGLE_REGISTER 0x06U
#define MODBUS_FC_WRITE_MULTIPLE_COILS 0x0FU
#define MODBUS_FC_WRITE_MULTIPLE_REGS 0x10U

#define MODBUS_MAX_READ_BITS 2000U
#define MODBUS_MAX_READ_REGISTERS 125U
#define MODBUS_MAX_WRITE_COILS 1968U
#define MODBUS_MAX_WRITE_REGISTERS 123U

#define MODBUS_FIXED_REQUEST_LENGTH 8U
#define MODBUS_EXCEPTION_RESPONSE_LENGTH 5U
#define MODBUS_WRITE_RESPONSE_LENGTH 8U

/*
 * Shared transaction/read staging storage.
 *
 * This module is intentionally non-reentrant. Only one call to
 * modbus_core_process_frame() may execute at a time.
 *
 * Multiple Modbus instances or concurrent tasks require either:
 * - moving these buffers into modbus_core_ctx_t, or
 * - protecting the complete processing call with a mutex.
 */
static uint16_t s_register_write_values[MODBUS_MAX_WRITE_REGISTERS];
static uint16_t s_register_read_values[MODBUS_MAX_READ_REGISTERS];
static uint8_t s_bit_read_values[(MODBUS_MAX_READ_BITS + 7U) / 8U];

_Static_assert((MODBUS_MAX_WRITE_REGISTERS * 2U) <= UINT8_MAX,
               "FC10 byte count must fit in one byte");

_Static_assert(((MODBUS_MAX_WRITE_COILS + 7U) / 8U) <= UINT8_MAX,
               "FC0F byte count must fit in one byte");

_Static_assert((5U + ((MODBUS_MAX_READ_BITS + 7U) / 8U)) <= UINT8_MAX,
               "FC01/02 response exceeds Modbus RTU ADU size");

_Static_assert((5U + (MODBUS_MAX_READ_REGISTERS * 2U)) <= UINT8_MAX,
               "FC03/04 response exceeds Modbus RTU ADU size");

/* -------------------------------------------------------------------------- */
/* Private declarations                                                       */
/* -------------------------------------------------------------------------- */

static uint16_t modbus_core_read_be16(const uint8_t *data);

static void modbus_core_set_no_response(uint16_t *tx_len, bool *need_reply);

static bool modbus_core_tx_capacity_valid(const modbus_core_ctx_t *ctx,
                                          uint16_t required_length);

static bool modbus_core_address_range_valid(uint16_t start_address,
                                            uint16_t quantity);

static bool modbus_core_parse_header(const uint8_t *rx, uint16_t rx_len,
                                     modbus_core_frame_meta_t *meta);

static bool modbus_core_parse_address_quantity(const uint8_t *rx,
                                               uint16_t rx_len,
                                               uint16_t *address,
                                               uint16_t *quantity);

static bool modbus_core_parse_address_value(const uint8_t *rx, uint16_t rx_len,
                                            uint16_t *address, uint16_t *value);

static void modbus_core_populate_meta_payload(modbus_core_frame_meta_t *meta,
                                              const uint8_t *rx,
                                              uint16_t rx_len);

static uint8_t
modbus_core_access_to_exception(modbus_core_access_result_t result);

static modbus_core_process_result_t
modbus_core_build_exception(modbus_core_ctx_t *ctx, uint8_t slave_id,
                            uint8_t function_code, uint8_t exception_code,
                            uint8_t *tx, uint16_t *tx_len, bool *need_reply);

static modbus_core_access_result_t modbus_core_read(modbus_core_ctx_t *ctx,
                                                    uint16_t address,
                                                    modbus_core_reg_type_t type,
                                                    modbus_core_reg_info_t *out,
                                                    uint16_t *cursor_hint);

static modbus_core_access_result_t
modbus_core_write(modbus_core_ctx_t *ctx, uint16_t address,
                  modbus_core_reg_type_t type, uint16_t value);

static bool modbus_core_value_in_range(const modbus_core_reg_info_t *info,
                                       uint16_t value);

static modbus_core_process_result_t
modbus_core_handle_access_error(modbus_core_ctx_t *ctx, bool is_broadcast,
                                uint8_t slave_id, uint8_t function_code,
                                modbus_core_access_result_t result, uint8_t *tx,
                                uint16_t *tx_len, bool *need_reply);

static modbus_core_process_result_t
modbus_core_fc01_02_read_bits(modbus_core_ctx_t *ctx, bool is_broadcast,
                              const uint8_t *rx, uint16_t rx_len, uint8_t *tx,
                              uint16_t *tx_len, bool *need_reply);

static modbus_core_process_result_t modbus_core_fc03_04_read_registers(
    modbus_core_ctx_t *ctx, bool is_broadcast, const uint8_t *rx,
    uint16_t rx_len, uint8_t *tx, uint16_t *tx_len, bool *need_reply);

static modbus_core_process_result_t modbus_core_fc05_write_single_coil(
    modbus_core_ctx_t *ctx, bool is_broadcast, const uint8_t *rx,
    uint16_t rx_len, uint8_t *tx, uint16_t *tx_len, bool *need_reply);

static modbus_core_process_result_t modbus_core_fc06_write_single_register(
    modbus_core_ctx_t *ctx, bool is_broadcast, const uint8_t *rx,
    uint16_t rx_len, uint8_t *tx, uint16_t *tx_len, bool *need_reply);

static modbus_core_process_result_t modbus_core_fc0f_write_multiple_coils(
    modbus_core_ctx_t *ctx, bool is_broadcast, const uint8_t *rx,
    uint16_t rx_len, uint8_t *tx, uint16_t *tx_len, bool *need_reply);

static modbus_core_process_result_t modbus_core_fc10_write_multiple_registers(
    modbus_core_ctx_t *ctx, bool is_broadcast, const uint8_t *rx,
    uint16_t rx_len, uint8_t *tx, uint16_t *tx_len, bool *need_reply);

/* -------------------------------------------------------------------------- */
/* Private helpers                                                            */
/* -------------------------------------------------------------------------- */
static modbus_core_process_result_t
modbus_core_build_write_response(const modbus_core_ctx_t *ctx,
                                 const uint8_t *rx, uint8_t *tx,
                                 uint16_t *tx_len, bool *need_reply) {
  uint16_t crc;

  if ((ctx == NULL) || (rx == NULL) || (tx == NULL) || (tx_len == NULL) ||
      (need_reply == NULL)) {
    return MODBUS_PROC_DROP;
  }

  if (!modbus_core_tx_capacity_valid(ctx, MODBUS_WRITE_RESPONSE_LENGTH)) {
    return MODBUS_PROC_DROP;
  }

  memmove(tx, rx, 6U);

  crc = modbus_crc16(tx, 6U);

  tx[6] = (uint8_t)(crc & 0x00FFU);
  tx[7] = (uint8_t)(crc >> 8U);

  *tx_len = MODBUS_WRITE_RESPONSE_LENGTH;
  *need_reply = true;

  return MODBUS_PROC_OK;
}

static uint16_t modbus_core_read_be16(const uint8_t *data) {
  if (data == NULL) {
    return 0U;
  }

  return (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
}

static void modbus_core_set_no_response(uint16_t *tx_len, bool *need_reply) {
  if (tx_len != NULL) {
    *tx_len = 0U;
  }

  if (need_reply != NULL) {
    *need_reply = false;
  }
}

static bool modbus_core_tx_capacity_valid(const modbus_core_ctx_t *ctx,
                                          uint16_t required_length) {
  return (ctx != NULL) && (required_length <= ctx->tx_max_len);
}

static bool modbus_core_address_range_valid(uint16_t start_address,
                                            uint16_t quantity) {
  if (quantity == 0U) {
    return false;
  }

  return start_address <= (uint16_t)(UINT16_MAX - (quantity - 1U));
}

static bool modbus_core_parse_header(const uint8_t *rx, uint16_t rx_len,
                                     modbus_core_frame_meta_t *meta) {
  if ((rx == NULL) || (meta == NULL) || (rx_len < 2U)) {
    return false;
  }

  meta->slave_id = rx[0];
  meta->func_code = rx[1];
  meta->is_broadcast = (rx[0] == 0U);

  return true;
}

static bool modbus_core_parse_address_quantity(const uint8_t *rx,
                                               uint16_t rx_len,
                                               uint16_t *address,
                                               uint16_t *quantity) {
  if ((rx == NULL) || (address == NULL) || (quantity == NULL) ||
      (rx_len < 6U)) {
    return false;
  }

  *address = modbus_core_read_be16(&rx[2]);
  *quantity = modbus_core_read_be16(&rx[4]);

  return true;
}

static bool modbus_core_parse_address_value(const uint8_t *rx, uint16_t rx_len,
                                            uint16_t *address,
                                            uint16_t *value) {
  if ((rx == NULL) || (address == NULL) || (value == NULL) || (rx_len < 6U)) {
    return false;
  }

  *address = modbus_core_read_be16(&rx[2]);
  *value = modbus_core_read_be16(&rx[4]);

  return true;
}

static void modbus_core_populate_meta_payload(modbus_core_frame_meta_t *meta,
                                              const uint8_t *rx,
                                              uint16_t rx_len) {
  uint16_t address;
  uint16_t field;

  if ((meta == NULL) || (rx == NULL)) {
    return;
  }

  switch (meta->func_code) {
  case MODBUS_FC_READ_COILS:
  case MODBUS_FC_READ_DISCRETE_INPUTS:
  case MODBUS_FC_READ_HOLDING_REGISTERS:
  case MODBUS_FC_READ_INPUT_REGISTERS:
  case MODBUS_FC_WRITE_MULTIPLE_COILS:
  case MODBUS_FC_WRITE_MULTIPLE_REGS:
    if (modbus_core_parse_address_quantity(rx, rx_len, &address, &field)) {
      meta->data_addr = address;
      meta->quantity = field;
    }
    break;

  case MODBUS_FC_WRITE_SINGLE_COIL:
  case MODBUS_FC_WRITE_SINGLE_REGISTER:
    if (modbus_core_parse_address_value(rx, rx_len, &address, &field)) {
      meta->data_addr = address;
      meta->data_val = field;
    }
    break;

  default:
    break;
  }
}

static uint8_t
modbus_core_access_to_exception(modbus_core_access_result_t result) {
  switch (result) {
  case MODBUS_ACCESS_ILLEGAL_ADDRESS:
    return MODBUS_EX_ILLEGAL_ADDRESS;

  case MODBUS_ACCESS_ILLEGAL_VALUE:
    return MODBUS_EX_ILLEGAL_VALUE;

  case MODBUS_ACCESS_DEVICE_BUSY:
    return MODBUS_EX_DEVICE_BUSY;

  case MODBUS_ACCESS_INVALID_CONTEXT:
  case MODBUS_ACCESS_DEVICE_FAILURE:
  default:
    return MODBUS_EX_DEVICE_FAILURE;
  }
}

static modbus_core_process_result_t
modbus_core_build_exception(modbus_core_ctx_t *ctx, uint8_t slave_id,
                            uint8_t function_code, uint8_t exception_code,
                            uint8_t *tx, uint16_t *tx_len, bool *need_reply) {
  uint16_t crc;

  if ((ctx == NULL) || (tx == NULL) || (tx_len == NULL) ||
      (need_reply == NULL)) {
    return MODBUS_PROC_DROP;
  }

  modbus_core_set_no_response(tx_len, need_reply);

  if (!modbus_core_tx_capacity_valid(ctx, MODBUS_EXCEPTION_RESPONSE_LENGTH)) {
    return MODBUS_PROC_DROP;
  }

  tx[0] = slave_id;
  tx[1] = (uint8_t)(function_code | 0x80U);
  tx[2] = exception_code;

  crc = modbus_crc16(tx, 3U);

  tx[3] = (uint8_t)(crc & 0x00FFU);
  tx[4] = (uint8_t)(crc >> 8U);

  *tx_len = MODBUS_EXCEPTION_RESPONSE_LENGTH;
  *need_reply = true;

  return MODBUS_PROC_OK;
}

static modbus_core_access_result_t modbus_core_read(modbus_core_ctx_t *ctx,
                                                    uint16_t address,
                                                    modbus_core_reg_type_t type,
                                                    modbus_core_reg_info_t *out,
                                                    uint16_t *cursor_hint) {
#ifndef NDEBUG
  assert(ctx != NULL);
  assert(out != NULL);
#endif

  if ((ctx == NULL) || (ctx->read_reg == NULL) || (out == NULL)) {
    return MODBUS_ACCESS_INVALID_CONTEXT;
  }

  return ctx->read_reg(address, type, out, cursor_hint);
}

static modbus_core_access_result_t
modbus_core_write(modbus_core_ctx_t *ctx, uint16_t address,
                  modbus_core_reg_type_t type, uint16_t value) {
#ifndef NDEBUG
  assert(ctx != NULL);
#endif

  if ((ctx == NULL) || (ctx->write_reg == NULL)) {
    return MODBUS_ACCESS_INVALID_CONTEXT;
  }

  return ctx->write_reg(address, type, value);
}

static bool modbus_core_value_in_range(const modbus_core_reg_info_t *info,
                                       uint16_t value) {
  if (info == NULL) {
    return false;
  }

  if (!info->has_range) {
    return true;
  }

  return (value >= info->min) && (value <= info->max);
}

static modbus_core_process_result_t
modbus_core_handle_access_error(modbus_core_ctx_t *ctx, bool is_broadcast,
                                uint8_t slave_id, uint8_t function_code,
                                modbus_core_access_result_t result, uint8_t *tx,
                                uint16_t *tx_len, bool *need_reply) {
  if (is_broadcast) {
    modbus_core_set_no_response(tx_len, need_reply);
    return MODBUS_PROC_NO_RESPONSE;
  }

  return modbus_core_build_exception(ctx, slave_id, function_code,
                                     modbus_core_access_to_exception(result),
                                     tx, tx_len, need_reply);
}

static modbus_core_process_result_t modbus_core_handle_malformed_request(
    modbus_core_ctx_t *ctx, bool is_broadcast, uint8_t slave_id,
    uint8_t function_code, uint8_t *tx, uint16_t *tx_len, bool *need_reply) {
  if ((ctx == NULL) || (tx == NULL) || (tx_len == NULL) ||
      (need_reply == NULL)) {
    return MODBUS_PROC_DROP;
  }

  if (is_broadcast) {
    modbus_core_set_no_response(tx_len, need_reply);
    return MODBUS_PROC_NO_RESPONSE;
  }

  return modbus_core_build_exception(ctx, slave_id, function_code,
                                     MODBUS_EX_ILLEGAL_VALUE, tx, tx_len,
                                     need_reply);
}

static modbus_core_access_result_t
modbus_core_read_writable(modbus_core_ctx_t *ctx, uint16_t address,
                          modbus_core_reg_type_t type,
                          modbus_core_reg_info_t *info, uint16_t *cursor_hint) {
  modbus_core_access_result_t result;

  result = modbus_core_read(ctx, address, type, info, cursor_hint);
  if (result != MODBUS_ACCESS_OK) {
    return result;
  }

  if (!info->writable) {
    return MODBUS_ACCESS_ILLEGAL_ADDRESS;
  }

  return MODBUS_ACCESS_OK;
}

/* -------------------------------------------------------------------------- */
/* Public frame processor                                                     */
/* -------------------------------------------------------------------------- */

modbus_core_process_result_t
modbus_core_process_frame(modbus_core_ctx_t *ctx, const uint8_t *rx,
                          uint16_t rx_len, uint8_t *tx, uint16_t *tx_len,
                          bool *need_reply, modbus_core_frame_meta_t *meta) {
  uint16_t received_crc;
  uint16_t calculated_crc;

  if ((ctx == NULL) || (rx == NULL) || (tx == NULL) || (tx_len == NULL) ||
      (need_reply == NULL) || (meta == NULL)) {
    return MODBUS_PROC_DROP;
  }

  modbus_core_set_no_response(tx_len, need_reply);

  memset(meta, 0, sizeof(*meta));

  if (rx_len < 4U) {
    return MODBUS_PROC_DROP;
  }

  received_crc = (uint16_t)rx[rx_len - 2U] | ((uint16_t)rx[rx_len - 1U] << 8U);

  calculated_crc = modbus_crc16(rx, (uint16_t)(rx_len - 2U));

  if (received_crc != calculated_crc) {
    return MODBUS_PROC_DROP;
  }

  if (!modbus_core_parse_header(rx, rx_len, meta)) {
    return MODBUS_PROC_DROP;
  }

  modbus_core_populate_meta_payload(meta, rx, rx_len);

  if ((!meta->is_broadcast) && (meta->slave_id != ctx->slave_id)) {
    return MODBUS_PROC_DROP;
  }

  switch (meta->func_code) {
  case MODBUS_FC_READ_COILS:
  case MODBUS_FC_READ_DISCRETE_INPUTS:
    return modbus_core_fc01_02_read_bits(ctx, meta->is_broadcast, rx, rx_len,
                                         tx, tx_len, need_reply);

  case MODBUS_FC_READ_HOLDING_REGISTERS:
  case MODBUS_FC_READ_INPUT_REGISTERS:
    return modbus_core_fc03_04_read_registers(ctx, meta->is_broadcast, rx,
                                              rx_len, tx, tx_len, need_reply);

  case MODBUS_FC_WRITE_SINGLE_COIL:
    return modbus_core_fc05_write_single_coil(ctx, meta->is_broadcast, rx,
                                              rx_len, tx, tx_len, need_reply);

  case MODBUS_FC_WRITE_SINGLE_REGISTER:
    return modbus_core_fc06_write_single_register(
        ctx, meta->is_broadcast, rx, rx_len, tx, tx_len, need_reply);

  case MODBUS_FC_WRITE_MULTIPLE_COILS:
    return modbus_core_fc0f_write_multiple_coils(
        ctx, meta->is_broadcast, rx, rx_len, tx, tx_len, need_reply);

  case MODBUS_FC_WRITE_MULTIPLE_REGS:
    return modbus_core_fc10_write_multiple_registers(
        ctx, meta->is_broadcast, rx, rx_len, tx, tx_len, need_reply);

  default:
    if (meta->is_broadcast) {
      return MODBUS_PROC_NO_RESPONSE;
    }

    return modbus_core_build_exception(ctx, meta->slave_id, meta->func_code,
                                       MODBUS_EX_ILLEGAL_FUNCTION, tx, tx_len,
                                       need_reply);
  }
}

/* -------------------------------------------------------------------------- */
/* FC01 / FC02                                                                */
/* -------------------------------------------------------------------------- */
/*
 * Pack requested bit i into:
 * packed_values[i / 8] bit (i % 8)
 *
 * The first requested bit is the least-significant bit of packed_values[0].
 * Unused high bits in the final byte must be zero.
 */
static modbus_core_process_result_t
modbus_core_fc01_02_read_bits(modbus_core_ctx_t *ctx, bool is_broadcast,
                              const uint8_t *rx, uint16_t rx_len, uint8_t *tx,
                              uint16_t *tx_len, bool *need_reply) {
  const uint8_t slave_id = rx[0];
  const uint8_t function_code = rx[1];

  uint8_t byte_count;
  uint16_t start_address;
  uint16_t quantity;
  uint16_t cursor_hint = 0U;
  uint16_t output_index;
  uint16_t crc;
  modbus_core_reg_type_t type;

  if (rx_len != MODBUS_FIXED_REQUEST_LENGTH) {
    return modbus_core_handle_malformed_request(
        ctx, is_broadcast, slave_id, function_code, tx, tx_len, need_reply);
  }

  if (!modbus_core_parse_address_quantity(rx, rx_len, &start_address,
                                          &quantity)) {
    return MODBUS_PROC_DROP;
  }

  if ((quantity == 0U) || (quantity > MODBUS_MAX_READ_BITS)) {
    if (is_broadcast) {
      return MODBUS_PROC_NO_RESPONSE;
    }

    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_ILLEGAL_VALUE, tx, tx_len,
                                       need_reply);
  }

  if (!modbus_core_address_range_valid(start_address, quantity)) {
    if (is_broadcast) {
      return MODBUS_PROC_NO_RESPONSE;
    }

    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_ILLEGAL_ADDRESS, tx, tx_len,
                                       need_reply);
  }

  if (is_broadcast) {
    return MODBUS_PROC_NO_RESPONSE;
  }

  byte_count = (uint8_t)((quantity + 7U) / 8U);

  if (!modbus_core_tx_capacity_valid(ctx, (uint16_t)(5U + byte_count))) {
    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_DEVICE_FAILURE, tx, tx_len,
                                       need_reply);
  }

  tx[0] = slave_id;
  tx[1] = function_code;
  tx[2] = byte_count;

  type = (function_code == MODBUS_FC_READ_COILS) ? MODBUS_REG_COIL
                                                 : MODBUS_REG_DISCRETE;

  if (ctx->read_multiple_bits != NULL) {
    modbus_core_access_result_t result;

    memset(s_bit_read_values, 0, byte_count);

    result = ctx->read_multiple_bits(start_address, type, s_bit_read_values,
                                     quantity);

    if (result != MODBUS_ACCESS_OK) {
      return modbus_core_handle_access_error(
          ctx, false, slave_id, function_code, result, tx, tx_len, need_reply);
    }

    if ((quantity % 8U) != 0U) {
      const uint8_t valid_bits = (uint8_t)(quantity % 8U);
      const uint8_t valid_mask = (uint8_t)((1U << valid_bits) - 1U);

      s_bit_read_values[byte_count - 1U] &= valid_mask;
    }

    memcpy(&tx[3], s_bit_read_values, byte_count);
  } else {
    memset(&tx[3], 0, byte_count);

    for (uint16_t i = 0U; i < quantity; i++) {
      uint16_t address;
      modbus_core_reg_info_t info;
      modbus_core_access_result_t result;

      address = (uint16_t)(start_address + i);

      result = modbus_core_read(ctx, address, type, &info, &cursor_hint);

      if (result != MODBUS_ACCESS_OK) {
        return modbus_core_handle_access_error(ctx, false, slave_id,
                                               function_code, result, tx,
                                               tx_len, need_reply);
      }

      if (info.value != 0U) {
        tx[3U + (i / 8U)] |= (uint8_t)(1U << (i % 8U));
      }
    }
  }

  output_index = (uint16_t)(3U + byte_count);

  crc = modbus_crc16(tx, output_index);

  tx[output_index++] = (uint8_t)(crc & 0x00FFU);

  tx[output_index++] = (uint8_t)(crc >> 8U);

  *tx_len = output_index;
  *need_reply = true;

  return MODBUS_PROC_OK;
}

/* -------------------------------------------------------------------------- */
/* FC03 / FC04                                                                */
/* -------------------------------------------------------------------------- */

static modbus_core_process_result_t modbus_core_fc03_04_read_registers(
    modbus_core_ctx_t *ctx, bool is_broadcast, const uint8_t *rx,
    uint16_t rx_len, uint8_t *tx, uint16_t *tx_len, bool *need_reply) {
  const uint8_t slave_id = rx[0];
  const uint8_t function_code = rx[1];

  uint16_t start_address;
  uint16_t quantity;
  uint16_t cursor_hint = 0U;
  uint16_t output_index;
  uint16_t crc;
  modbus_core_reg_type_t type;

  if (rx_len != MODBUS_FIXED_REQUEST_LENGTH) {
    return modbus_core_handle_malformed_request(
        ctx, is_broadcast, slave_id, function_code, tx, tx_len, need_reply);
  }

  if (!modbus_core_parse_address_quantity(rx, rx_len, &start_address,
                                          &quantity)) {
    return MODBUS_PROC_DROP;
  }

  if ((quantity == 0U) || (quantity > MODBUS_MAX_READ_REGISTERS)) {
    if (is_broadcast) {
      return MODBUS_PROC_NO_RESPONSE;
    }

    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_ILLEGAL_VALUE, tx, tx_len,
                                       need_reply);
  }

  if (!modbus_core_address_range_valid(start_address, quantity)) {
    if (is_broadcast) {
      return MODBUS_PROC_NO_RESPONSE;
    }

    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_ILLEGAL_ADDRESS, tx, tx_len,
                                       need_reply);
  }

  if (is_broadcast) {
    return MODBUS_PROC_NO_RESPONSE;
  }

  if (!modbus_core_tx_capacity_valid(ctx, (uint16_t)(5U + (quantity * 2U)))) {
    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_DEVICE_FAILURE, tx, tx_len,
                                       need_reply);
  }

  tx[0] = slave_id;
  tx[1] = function_code;
  tx[2] = (uint8_t)(quantity * 2U);

  output_index = 3U;

  type = (function_code == MODBUS_FC_READ_HOLDING_REGISTERS)
             ? MODBUS_REG_HOLDING
             : MODBUS_REG_INPUT;

  if (ctx->read_multiple_registers != NULL) {
    modbus_core_access_result_t result;

    result = ctx->read_multiple_registers(start_address, type,
                                          s_register_read_values, quantity);

    if (result != MODBUS_ACCESS_OK) {
      return modbus_core_handle_access_error(
          ctx, false, slave_id, function_code, result, tx, tx_len, need_reply);
    }

    for (uint16_t i = 0U; i < quantity; i++) {
      tx[output_index++] = (uint8_t)(s_register_read_values[i] >> 8U);

      tx[output_index++] = (uint8_t)(s_register_read_values[i] & 0x00FFU);
    }
  } else {
    for (uint16_t i = 0U; i < quantity; i++) {
      uint16_t address;
      modbus_core_reg_info_t info;
      modbus_core_access_result_t result;

      address = (uint16_t)(start_address + i);

      result = modbus_core_read(ctx, address, type, &info, &cursor_hint);

      if (result != MODBUS_ACCESS_OK) {
        return modbus_core_handle_access_error(ctx, false, slave_id,
                                               function_code, result, tx,
                                               tx_len, need_reply);
      }

      tx[output_index++] = (uint8_t)(info.value >> 8U);

      tx[output_index++] = (uint8_t)(info.value & 0x00FFU);
    }
  }

  crc = modbus_crc16(tx, output_index);

  tx[output_index++] = (uint8_t)(crc & 0x00FFU);

  tx[output_index++] = (uint8_t)(crc >> 8U);

  *tx_len = output_index;
  *need_reply = true;

  return MODBUS_PROC_OK;
}

/* -------------------------------------------------------------------------- */
/* FC05                                                                       */
/* -------------------------------------------------------------------------- */

static modbus_core_process_result_t modbus_core_fc05_write_single_coil(
    modbus_core_ctx_t *ctx, bool is_broadcast, const uint8_t *rx,
    uint16_t rx_len, uint8_t *tx, uint16_t *tx_len, bool *need_reply) {
  const uint8_t slave_id = rx[0];
  const uint8_t function_code = MODBUS_FC_WRITE_SINGLE_COIL;

  uint16_t cursor_hint = 0U;
  uint16_t address;
  uint16_t request_value;
  uint16_t internal_value;

  modbus_core_reg_info_t info;
  modbus_core_access_result_t result;

  if (rx_len != MODBUS_FIXED_REQUEST_LENGTH) {
    return modbus_core_handle_malformed_request(
        ctx, is_broadcast, slave_id, function_code, tx, tx_len, need_reply);
  }

  if ((!is_broadcast) &&
      !modbus_core_tx_capacity_valid(ctx, MODBUS_WRITE_RESPONSE_LENGTH)) {
    return MODBUS_PROC_DROP;
  }

  if (!modbus_core_parse_address_value(rx, rx_len, &address, &request_value)) {
    return MODBUS_PROC_DROP;
  }

  if ((request_value != 0xFF00U) && (request_value != 0x0000U)) {
    if (is_broadcast) {
      return MODBUS_PROC_NO_RESPONSE;
    }

    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_ILLEGAL_VALUE, tx, tx_len,
                                       need_reply);
  }

  internal_value = (request_value == 0xFF00U) ? 1U : 0U;

  result = modbus_core_read_writable(ctx, address, MODBUS_REG_COIL, &info,
                                     &cursor_hint);

  if (result != MODBUS_ACCESS_OK) {
    return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                           function_code, result, tx, tx_len,
                                           need_reply);
  }

  if (ctx->validate_write != NULL) {
    result =
        ctx->validate_write(address, MODBUS_REG_COIL, internal_value, &info);

    if (result != MODBUS_ACCESS_OK) {
      return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                             function_code, result, tx, tx_len,
                                             need_reply);
    }
  }

  result = modbus_core_write(ctx, address, MODBUS_REG_COIL, internal_value);

  if (result != MODBUS_ACCESS_OK) {
    return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                           function_code, result, tx, tx_len,
                                           need_reply);
  }

  if (ctx->on_write_applied != NULL) {
    ctx->on_write_applied(address, MODBUS_REG_COIL, info.value, internal_value);
  }

  if (is_broadcast) {
    return MODBUS_PROC_NO_RESPONSE;
  }

  return modbus_core_build_write_response(ctx, rx, tx, tx_len, need_reply);
}

/* -------------------------------------------------------------------------- */
/* FC06                                                                       */
/* -------------------------------------------------------------------------- */

static modbus_core_process_result_t modbus_core_fc06_write_single_register(
    modbus_core_ctx_t *ctx, bool is_broadcast, const uint8_t *rx,
    uint16_t rx_len, uint8_t *tx, uint16_t *tx_len, bool *need_reply) {
  const uint8_t slave_id = rx[0];
  const uint8_t function_code = MODBUS_FC_WRITE_SINGLE_REGISTER;

  uint16_t cursor_hint = 0U;
  uint16_t address;
  uint16_t value;

  modbus_core_reg_info_t info;
  modbus_core_access_result_t result;

  if (rx_len != MODBUS_FIXED_REQUEST_LENGTH) {
    return modbus_core_handle_malformed_request(
        ctx, is_broadcast, slave_id, function_code, tx, tx_len, need_reply);
  }

  if ((!is_broadcast) &&
      !modbus_core_tx_capacity_valid(ctx, MODBUS_WRITE_RESPONSE_LENGTH)) {
    return MODBUS_PROC_DROP;
  }

  if (!modbus_core_parse_address_value(rx, rx_len, &address, &value)) {
    return MODBUS_PROC_DROP;
  }

  result = modbus_core_read_writable(ctx, address, MODBUS_REG_HOLDING, &info,
                                     &cursor_hint);

  if (result != MODBUS_ACCESS_OK) {
    return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                           function_code, result, tx, tx_len,
                                           need_reply);
  }

  if (!modbus_core_value_in_range(&info, value)) {
    return modbus_core_handle_access_error(
        ctx, is_broadcast, slave_id, function_code, MODBUS_ACCESS_ILLEGAL_VALUE,
        tx, tx_len, need_reply);
  }

  if (ctx->validate_write != NULL) {
    result = ctx->validate_write(address, MODBUS_REG_HOLDING, value, &info);

    if (result != MODBUS_ACCESS_OK) {
      return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                             function_code, result, tx, tx_len,
                                             need_reply);
    }
  }

  result = modbus_core_write(ctx, address, MODBUS_REG_HOLDING, value);

  if (result != MODBUS_ACCESS_OK) {
    return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                           function_code, result, tx, tx_len,
                                           need_reply);
  }

  if (ctx->on_write_applied != NULL) {
    ctx->on_write_applied(address, MODBUS_REG_HOLDING, info.value, value);
  }

  if (is_broadcast) {
    return MODBUS_PROC_NO_RESPONSE;
  }

  return modbus_core_build_write_response(ctx, rx, tx, tx_len, need_reply);
}

/* -------------------------------------------------------------------------- */
/* FC0F — Transactional multiple-coil write                                   */
/* -------------------------------------------------------------------------- */

static modbus_core_process_result_t modbus_core_fc0f_write_multiple_coils(
    modbus_core_ctx_t *ctx, bool is_broadcast, const uint8_t *rx,
    uint16_t rx_len, uint8_t *tx, uint16_t *tx_len, bool *need_reply) {
  const uint8_t slave_id = rx[0];
  const uint8_t function_code = MODBUS_FC_WRITE_MULTIPLE_COILS;

  uint8_t byte_count;
  uint16_t start_address;
  uint16_t quantity;
  uint16_t cursor_hint = 0U;

  const uint8_t *packed_values;

  if (rx_len < 9U) {
    return modbus_core_handle_malformed_request(
        ctx, is_broadcast, slave_id, function_code, tx, tx_len, need_reply);
  }

  if (!modbus_core_parse_address_quantity(rx, rx_len, &start_address,
                                          &quantity)) {
    return MODBUS_PROC_DROP;
  }

  byte_count = rx[6];

  if ((quantity == 0U) || (quantity > MODBUS_MAX_WRITE_COILS) ||
      (byte_count != (uint8_t)((quantity + 7U) / 8U))) {
    if (is_broadcast) {
      return MODBUS_PROC_NO_RESPONSE;
    }

    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_ILLEGAL_VALUE, tx, tx_len,
                                       need_reply);
  }

  if (!modbus_core_address_range_valid(start_address, quantity)) {
    if (is_broadcast) {
      return MODBUS_PROC_NO_RESPONSE;
    }

    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_ILLEGAL_ADDRESS, tx, tx_len,
                                       need_reply);
  }

  if (rx_len != (uint16_t)(9U + byte_count)) {
    return modbus_core_handle_malformed_request(
        ctx, is_broadcast, slave_id, function_code, tx, tx_len, need_reply);
  }

  if ((!is_broadcast) &&
      !modbus_core_tx_capacity_valid(ctx, MODBUS_WRITE_RESPONSE_LENGTH)) {
    return MODBUS_PROC_DROP;
  }

  if (ctx->write_multiple_coils == NULL) {
    return modbus_core_handle_access_error(
        ctx, is_broadcast, slave_id, function_code,
        MODBUS_ACCESS_INVALID_CONTEXT, tx, tx_len, need_reply);
  }

  packed_values = &rx[7];

  /*
   * Validate every target coil before transaction validation and commit.
   */
  for (uint16_t i = 0U; i < quantity; i++) {
    uint16_t address;
    uint16_t value;
    modbus_core_reg_info_t info;
    modbus_core_access_result_t result;

    address = (uint16_t)(start_address + i);

    value = (uint16_t)((packed_values[i / 8U] >> (i % 8U)) & 0x01U);

    result = modbus_core_read_writable(ctx, address, MODBUS_REG_COIL, &info,
                                       &cursor_hint);

    if (result != MODBUS_ACCESS_OK) {
      return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                             function_code, result, tx, tx_len,
                                             need_reply);
    }

    if (ctx->validate_write != NULL) {
      result = ctx->validate_write(address, MODBUS_REG_COIL, value, &info);

      if (result != MODBUS_ACCESS_OK) {
        return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                               function_code, result, tx,
                                               tx_len, need_reply);
      }
    }
  }

  if (ctx->validate_multiple_coils != NULL) {
    modbus_core_access_result_t result;

    result =
        ctx->validate_multiple_coils(start_address, packed_values, quantity);

    if (result != MODBUS_ACCESS_OK) {
      return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                             function_code, result, tx, tx_len,
                                             need_reply);
    }
  }

  /*
   * Atomic application commit.
   *
   * The callback must either commit every requested coil or none.
   */
  {
    modbus_core_access_result_t result;

    result = ctx->write_multiple_coils(start_address, packed_values, quantity);

    if (result != MODBUS_ACCESS_OK) {
      return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                             function_code, result, tx, tx_len,
                                             need_reply);
    }
  }

  if (ctx->on_write_range_applied != NULL) {
    ctx->on_write_range_applied(start_address, MODBUS_REG_COIL, quantity);
  }

  if (is_broadcast) {
    return MODBUS_PROC_NO_RESPONSE;
  }

  return modbus_core_build_write_response(ctx, rx, tx, tx_len, need_reply);
}

/* -------------------------------------------------------------------------- */
/* FC10 — Transactional multiple-register write                               */
/* -------------------------------------------------------------------------- */

static modbus_core_process_result_t modbus_core_fc10_write_multiple_registers(
    modbus_core_ctx_t *ctx, bool is_broadcast, const uint8_t *rx,
    uint16_t rx_len, uint8_t *tx, uint16_t *tx_len, bool *need_reply) {
  const uint8_t slave_id = rx[0];
  const uint8_t function_code = MODBUS_FC_WRITE_MULTIPLE_REGS;

  uint8_t byte_count;
  uint16_t start_address;
  uint16_t quantity;
  uint16_t cursor_hint = 0U;

  if (rx_len < 9U) {
    return modbus_core_handle_malformed_request(
        ctx, is_broadcast, slave_id, function_code, tx, tx_len, need_reply);
  }

  if (!modbus_core_parse_address_quantity(rx, rx_len, &start_address,
                                          &quantity)) {
    return MODBUS_PROC_DROP;
  }

  byte_count = rx[6];

  if ((quantity == 0U) || (quantity > MODBUS_MAX_WRITE_REGISTERS) ||
      (byte_count != (uint8_t)(quantity * 2U))) {
    if (is_broadcast) {
      return MODBUS_PROC_NO_RESPONSE;
    }

    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_ILLEGAL_VALUE, tx, tx_len,
                                       need_reply);
  }

  if (!modbus_core_address_range_valid(start_address, quantity)) {
    if (is_broadcast) {
      return MODBUS_PROC_NO_RESPONSE;
    }

    return modbus_core_build_exception(ctx, slave_id, function_code,
                                       MODBUS_EX_ILLEGAL_ADDRESS, tx, tx_len,
                                       need_reply);
  }

  if (rx_len != (uint16_t)(9U + byte_count)) {
    return modbus_core_handle_malformed_request(
        ctx, is_broadcast, slave_id, function_code, tx, tx_len, need_reply);
  }

  if ((!is_broadcast) &&
      !modbus_core_tx_capacity_valid(ctx, MODBUS_WRITE_RESPONSE_LENGTH)) {
    return MODBUS_PROC_DROP;
  }

  if (ctx->write_multiple_registers == NULL) {
    return modbus_core_handle_access_error(
        ctx, is_broadcast, slave_id, function_code,
        MODBUS_ACCESS_INVALID_CONTEXT, tx, tx_len, need_reply);
  }

  /*
   * Decode the complete request into the staging array.
   */
  for (uint16_t i = 0U; i < quantity; i++) {
    s_register_write_values[i] = modbus_core_read_be16(&rx[7U + (i * 2U)]);
  }

  /*
   * Phase A: validate every target address and access permission first.
   * This gives FC16 deterministic ILLEGAL_ADDRESS precedence over any value
   * error later in the request.
   */
  for (uint16_t i = 0U; i < quantity; i++) {
    uint16_t address;
    modbus_core_reg_info_t info;
    modbus_core_access_result_t result;

    address = (uint16_t)(start_address + i);

    result = modbus_core_read_writable(ctx, address, MODBUS_REG_HOLDING, &info,
                                       &cursor_hint);

    if (result != MODBUS_ACCESS_OK) {
      return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                             function_code, result, tx, tx_len,
                                             need_reply);
    }

  }

  /*
   * Phase B: only after all addresses are valid, validate every supplied
   * value against its exact register metadata and application policy.
   */
  for (uint16_t i = 0U; i < quantity; i++) {
    uint16_t address = (uint16_t)(start_address + i);
    uint16_t value = s_register_write_values[i];
    modbus_core_reg_info_t info;
    modbus_core_access_result_t result;

    result = modbus_core_read_writable(ctx, address, MODBUS_REG_HOLDING, &info,
                                       &cursor_hint);
    if (result != MODBUS_ACCESS_OK) {
      /* Defensive only; Phase A already checked every address. */
      return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                             function_code, result, tx, tx_len,
                                             need_reply);
    }
    if (!modbus_core_value_in_range(&info, value)) {
      return modbus_core_handle_access_error(
          ctx, is_broadcast, slave_id, function_code,
          MODBUS_ACCESS_ILLEGAL_VALUE, tx, tx_len, need_reply);
    }
    if (ctx->validate_write != NULL) {
      result = ctx->validate_write(address, MODBUS_REG_HOLDING, value, &info);
      if (result != MODBUS_ACCESS_OK) {
        return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                               function_code, result, tx,
                                               tx_len, need_reply);
      }
    }
  }

  /*
   * Validate relationships across the complete proposed range.
   */
  if (ctx->validate_multiple_registers != NULL) {
    modbus_core_access_result_t result;

    result = ctx->validate_multiple_registers(
        start_address, s_register_write_values, quantity);

    if (result != MODBUS_ACCESS_OK) {
      return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                             function_code, result, tx, tx_len,
                                             need_reply);
    }
  }

  /*
   * Atomic application commit.
   *
   * The callback must either commit every requested register or none.
   */
  {
    modbus_core_access_result_t result;

    result = ctx->write_multiple_registers(start_address,
                                           s_register_write_values, quantity);

    if (result != MODBUS_ACCESS_OK) {
      return modbus_core_handle_access_error(ctx, is_broadcast, slave_id,
                                             function_code, result, tx, tx_len,
                                             need_reply);
    }
  }

  if (ctx->on_write_range_applied != NULL) {
    ctx->on_write_range_applied(start_address, MODBUS_REG_HOLDING, quantity);
  }

  if (is_broadcast) {
    return MODBUS_PROC_NO_RESPONSE;
  }

  return modbus_core_build_write_response(ctx, rx, tx, tx_len, need_reply);
}
