#ifndef INC_MODBUS_CORE_H_
#define INC_MODBUS_CORE_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  MODBUS_REG_COIL = 0x01,
  MODBUS_REG_DISCRETE = 0x02,
  MODBUS_REG_HOLDING = 0x03,
  MODBUS_REG_INPUT = 0x04
} modbus_core_reg_type_t;

typedef struct {
  uint16_t value;
  uint16_t min;
  uint16_t max;

  bool writable;
  bool has_range;
} modbus_core_reg_info_t;

typedef enum {
  MODBUS_EX_NONE = 0x00,
  MODBUS_EX_ILLEGAL_FUNCTION = 0x01,
  MODBUS_EX_ILLEGAL_ADDRESS = 0x02,
  MODBUS_EX_ILLEGAL_VALUE = 0x03,
  MODBUS_EX_DEVICE_FAILURE = 0x04,
  MODBUS_EX_DEVICE_BUSY = 0x06
} modbus_core_exception_t;

typedef enum {
  MODBUS_ACCESS_OK = 0,
  MODBUS_ACCESS_ILLEGAL_ADDRESS = 1,
  MODBUS_ACCESS_ILLEGAL_VALUE = 2,
  MODBUS_ACCESS_DEVICE_FAILURE = 3,
  MODBUS_ACCESS_DEVICE_BUSY = 4,
  MODBUS_ACCESS_INVALID_CONTEXT = 5
} modbus_core_access_result_t;

typedef struct {
  uint8_t slave_id;
  uint8_t func_code;

  uint16_t data_addr;
  uint16_t data_val;
  uint16_t quantity;

  bool is_broadcast;
} modbus_core_frame_meta_t;

/*
 * Read one register-map entry.
 *
 * cursor_hint may be used by the register-map implementation to accelerate
 * sequential accesses. The callback may ignore it.
 */
typedef modbus_core_access_result_t (*modbus_core_read_reg_fn_t)(
    uint16_t addr, modbus_core_reg_type_t type, modbus_core_reg_info_t *out,
    uint16_t *cursor_hint);

/*
 * Write one coil or holding register.
 *
 * Used by FC05 and FC06.
 */
typedef modbus_core_access_result_t (*modbus_core_write_reg_fn_t)(
    uint16_t addr, modbus_core_reg_type_t type, uint16_t value);

/*
 * Validate one proposed write against the current register-map entry.
 */
typedef modbus_core_access_result_t (*modbus_core_validate_write_fn_t)(
    uint16_t addr, modbus_core_reg_type_t type, uint16_t value,
    const modbus_core_reg_info_t *current);

/*
 * Validate one complete FC0F coil transaction.
 *
 * packed_values uses standard Modbus coil packing:
 * bit 0 of packed_values[0] corresponds to start_addr.
 */
typedef modbus_core_access_result_t (*modbus_core_validate_multiple_coils_fn_t)(
    uint16_t start_addr, const uint8_t *packed_values, uint16_t quantity);

/*
 * Validate one complete FC10 register transaction.
 */
typedef modbus_core_access_result_t (
    *modbus_core_validate_multiple_registers_fn_t)(uint16_t start_addr,
                                                   const uint16_t *values,
                                                   uint16_t quantity);

/*
 * Accept one complete FC0F write transaction.
 *
 * Contract:
 * - Return MODBUS_ACCESS_OK only if the complete requested transaction
 *   was accepted/staged successfully.
 * - On any error, no partial transaction may become externally visible.
 * - The callback may stage the transaction for a higher layer to commit
 *   after response transmission.
 */
typedef modbus_core_access_result_t (*modbus_core_write_multiple_coils_fn_t)(
    uint16_t start_addr, const uint8_t *packed_values, uint16_t quantity);

/*
 * Accept one complete FC10 write transaction.
 *
 * Contract:
 * - Return MODBUS_ACCESS_OK only if the complete requested transaction
 *   was accepted/staged successfully.
 * - On any error, no partial transaction may become externally visible.
 * - The callback may stage the transaction for a higher layer to commit
 *   after response transmission.
 */
typedef modbus_core_access_result_t (
    *modbus_core_write_multiple_registers_fn_t)(uint16_t start_addr,
                                                const uint16_t *values,
                                                uint16_t quantity);

typedef void (*modbus_core_write_applied_fn_t)(uint16_t addr,
                                               modbus_core_reg_type_t type,
                                               uint16_t old_value,
                                               uint16_t new_value);

typedef modbus_core_access_result_t (*modbus_core_read_multiple_registers_fn_t)(
    uint16_t start_addr, modbus_core_reg_type_t type, uint16_t *values,
    uint16_t quantity);

/*
 * Read a contiguous bit range as packed Modbus bytes.
 *
 * packed_values must be large enough for (quantity + 7) / 8 bytes.
 * The callback may ignore cursor_hint-style optimization and should return
 * a snapshot-consistent packed view when possible.
 */
typedef modbus_core_access_result_t (*modbus_core_read_multiple_bits_fn_t)(
    uint16_t start_addr, modbus_core_reg_type_t type, uint8_t *packed_values,
    uint16_t quantity);

/*
 * Called once after a successful FC0F or FC10 transaction.
 */
typedef void (*modbus_core_write_range_applied_fn_t)(
    uint16_t start_addr, modbus_core_reg_type_t type, uint16_t quantity);

typedef struct {
  uint8_t slave_id;
  uint16_t tx_max_len;

  modbus_core_read_reg_fn_t read_reg;
  modbus_core_write_reg_fn_t write_reg;
  modbus_core_validate_write_fn_t validate_write;

  modbus_core_validate_multiple_coils_fn_t validate_multiple_coils;

  modbus_core_validate_multiple_registers_fn_t validate_multiple_registers;

  modbus_core_write_multiple_coils_fn_t write_multiple_coils;

  modbus_core_write_multiple_registers_fn_t write_multiple_registers;

  modbus_core_write_applied_fn_t on_write_applied;

  modbus_core_write_range_applied_fn_t on_write_range_applied;

  modbus_core_read_multiple_registers_fn_t read_multiple_registers;

  modbus_core_read_multiple_bits_fn_t read_multiple_bits;
} modbus_core_ctx_t;

typedef enum {
  MODBUS_PROC_OK = 0,
  MODBUS_PROC_NO_RESPONSE = 1,
  MODBUS_PROC_DROP = 2
} modbus_core_process_result_t;

/*
 * Process one complete Modbus RTU frame.
 *
 * Reentrancy contract:
 * - This module is non-reentrant.
 * - Only one modbus_core_process_frame() call may execute at a time.
 * - Concurrent instances/tasks require external serialization or a future
 *   context-owned staging-buffer implementation.
 *
 * tx must point to writable storage of at least ctx->tx_max_len bytes.
 */
modbus_core_process_result_t
modbus_core_process_frame(modbus_core_ctx_t *ctx, const uint8_t *rx,
                          uint16_t rx_len, uint8_t *tx, uint16_t *tx_len,
                          bool *need_reply, modbus_core_frame_meta_t *meta);

#endif /* INC_MODBUS_CORE_H_ */
