#ifndef INC_AUTOBAUD_UART_TRANSACTION_H_
#define INC_AUTOBAUD_UART_TRANSACTION_H_

/* Milestone C: Hardware-independent UART transaction and rollback coordinator.
 * Physical UART operations are supplied by the target board adapter.
 * Never publish an applied/rolled-back result until RX is verified.
 */
#include "autobaud_v2.h"
#include "autobaud_v2_measurement.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  AUTOBAUD_UART_TXN_APPLIED_VERIFIED = 0,
  AUTOBAUD_UART_TXN_ROLLED_BACK_VERIFIED,
  AUTOBAUD_UART_TXN_CAPTURE_RESTORED,
  AUTOBAUD_UART_TXN_INVALID_PROFILE,
  AUTOBAUD_UART_TXN_BUSY,
  AUTOBAUD_UART_TXN_FATAL_RECOVERY
} autobaud_uart_txn_result_t;

typedef enum {
  AUTOBAUD_UART_TXN_PHASE_IDLE = 0,
  AUTOBAUD_UART_TXN_PHASE_CLAIM,
  AUTOBAUD_UART_TXN_PHASE_SNAPSHOT,
  AUTOBAUD_UART_TXN_PHASE_QUIESCE,
  AUTOBAUD_UART_TXN_PHASE_CAPTURE,
  AUTOBAUD_UART_TXN_PHASE_APPLY,
  AUTOBAUD_UART_TXN_PHASE_TIMING,
  AUTOBAUD_UART_TXN_PHASE_RESTART,
  AUTOBAUD_UART_TXN_PHASE_VERIFY,
  AUTOBAUD_UART_TXN_PHASE_ROLLBACK,
  AUTOBAUD_UART_TXN_PHASE_FATAL
} autobaud_uart_txn_phase_t;

typedef struct {
  uint32_t attempts;
  uint32_t successes;
  uint32_t rollbacks;
  uint32_t rollback_successes;
  uint32_t rollback_failures;
  uint32_t captures;
  uint32_t fatal_count;
  uint32_t claim_failures;
  uint32_t quiesce_failures;
  uint32_t apply_failures;
  uint32_t timing_failures;
  uint32_t restart_failures;
  uint32_t verify_failures;
} autobaud_uart_txn_diag_t;

/* Each callback is foreground-only. A release of fatal=true MUST gate
 * Modbus TX and RX callbacks, and preserve a fatal latch on the board.
 * release(success, passive) publishes a verified new RX session atomically.
 */
typedef struct {
  bool (*claim)(void *ctx);
  bool (*snapshot)(void *ctx, autobaud_v2_profile_t *previous);
  /* Whether the previous RX session was also a passive candidate. */
  bool (*previous_passive)(void *ctx);
  bool (*quiesce)(void *ctx);
  bool (*apply)(void *ctx, const autobaud_v2_profile_t *profile);
  bool (*timing)(void *ctx, const autobaud_v2_profile_t *profile);
  bool (*restart)(void *ctx, bool passive);
  bool (*verify)(void *ctx, const autobaud_v2_profile_t *profile,
                 bool passive);
  bool (*capture)(void *ctx, uint32_t timeout_ms,
                  hw_autobaud_measurement_t *out,
                  hw_autobaud_measure_status_t *measurement);
  bool (*capture_released)(void *ctx);
  void (*release)(void *ctx, bool fatal, bool passive);
} autobaud_uart_txn_ops_t;

typedef struct {
  autobaud_uart_txn_ops_t ops;
  void *ctx;
  autobaud_uart_txn_diag_t diag;
  autobaud_uart_txn_phase_t phase;
  autobaud_uart_txn_phase_t failure_phase;
  bool in_progress;
  bool fatal_latched;
} autobaud_uart_txn_t;

bool autobaud_uart_txn_init(autobaud_uart_txn_t *txn,
                           const autobaud_uart_txn_ops_t *ops,
                           void *ctx);

/* Profile switching is provisional; passive=true forbids the normal
 * Modbus request engine from consuming or responding to candidate traffic.
 * Only Milestone E may call the explicit passive frame polling API.
 */
autobaud_uart_txn_result_t autobaud_uart_txn_apply(
    autobaud_uart_txn_t *txn, const autobaud_v2_profile_t *requested,
    bool passive);

/* Capture is run with the UART RX session quiesced. The previous profile
 * and a completely fresh RX session are restored before returning.
 * Capture measurement and recovery outcome remain separate.
 */
autobaud_uart_txn_result_t autobaud_uart_txn_capture(
    autobaud_uart_txn_t *txn, uint32_t timeout_ms,
    hw_autobaud_measurement_t *measurement,
    hw_autobaud_measure_status_t *capture_status);

#endif
