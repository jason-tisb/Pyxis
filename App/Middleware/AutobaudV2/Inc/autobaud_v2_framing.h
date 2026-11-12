#ifndef INC_AUTOBAUD_V2_FRAMING_H_
#define INC_AUTOBAUD_V2_FRAMING_H_

/* Portable framing contract imported in Pyxis M3; implementation in M6.\n * HAL-independent, nonblocking passive RTU framing validator.
 * No Modbus request processing, register callbacks, transmit or output APIs.
 */
#include "autobaud_v2.h"
#include "autobaud_validate.h"
#include "autobaud_v2_evidence.h"
#include "autobaud_uart_transaction.h"
#include <stdbool.h>
#include <stdint.h>

#define AUTOBAUD_E_MAX_RX_RECOVERIES 2U

typedef enum {
  AUTOBAUD_E_RUNNING = 0,
  AUTOBAUD_E_LOCKED,
  AUTOBAUD_E_NO_VALID_REQUEST,
  AUTOBAUD_E_INVALID_MEASUREMENT,
  AUTOBAUD_E_BUDGET_EXPIRED,
  AUTOBAUD_E_BUSY,
  AUTOBAUD_E_RESTORE_FAILED,
  AUTOBAUD_E_FATAL_RECOVERY
} autobaud_e_status_t;

typedef enum {
  AUTOBAUD_E_STATE_IDLE = 0,
  AUTOBAUD_E_STATE_APPLY,
  AUTOBAUD_E_STATE_LISTEN,
  AUTOBAUD_E_STATE_TERMINAL
} autobaud_e_state_t;

typedef struct {
  uint32_t candidates;
  uint32_t candidate_timeouts;
  uint32_t frames_checked;
  uint32_t frames_rejected;
  uint32_t valid_requests;
  uint32_t crc_rejections;
  uint32_t untrusted_rejections;
  uint32_t rx_faults;
  uint32_t recovery_attempts;
  uint32_t recovery_failures;
  uint32_t invalid_session_frames;
  uint32_t lock_handoff_failures;
  uint32_t restore_failures;
} autobaud_e_diagnostics_t;

typedef struct {
  uint32_t (*now_ms)(void *user);
  bool (*fatal)(void *user);
  /* Snapshot is read-only and must verify the previous UART profile. */
  bool (*snapshot)(void *user, autobaud_v2_profile_t *profile,
                   bool *was_passive);
  autobaud_uart_txn_result_t (*apply)(void *user,
                                      const autobaud_v2_profile_t *profile,
                                      bool passive);
  bool (*get_session)(void *user, uint32_t *session_id);
  autobaud_e_poll_t (*take_frame)(void *user, uint8_t *out,
                                  uint16_t max_len, uint16_t *len,
                                  autobaud_e_frame_meta_t *meta);
  /* Must reverify current UART/DMA/RTO config and the exact session. */
  bool (*verify)(void *user, const autobaud_v2_profile_t *profile,
                 uint32_t session_id, uint32_t error_generation);
  /* Atomic handoff; the evidence frame has already been consumed. */
  bool (*accept)(void *user, const autobaud_v2_profile_t *profile,
                 const autobaud_e_frame_meta_t *meta);
  /* Latch communication fault and disable normal Modbus processing. */
  void (*latch_fatal)(void *user);
  void *user;
} autobaud_e_ops_t;

typedef struct {
  autobaud_e_ops_t ops;
  autobaud_e_diagnostics_t diag;
  autobaud_e_state_t state;
  autobaud_e_status_t status;
  autobaud_v2_profile_t previous;
  autobaud_v2_profile_t active;
  hw_autobaud_measurement_t measurement;
  uint32_t start_ms;
  uint32_t candidate_start_ms;
  uint32_t budget_ms;
  uint32_t candidate_timeout_ms;
  uint32_t session_id;
  uint8_t candidate_index;
  uint8_t rx_recoveries;
  uint8_t observed_slave_id;
  uint8_t observed_function_code;
  bool previous_passive;
  bool stop_bits_independently_verified;
} autobaud_e_ctx_t;

/* ctx must be zero-initialized before its first start; it can be reused
 * after entering TERMINAL. start() never overwrites a running transaction.
 */
bool autobaud_e_start(autobaud_e_ctx_t *ctx, const autobaud_e_ops_t *ops,
                      const hw_autobaud_measurement_t *measurement,
                      hw_autobaud_measure_status_t measurement_status,
                      uint32_t budget_ms);

/* Foreground-only: one bounded operation per call; never in an ISR. */
autobaud_e_status_t autobaud_e_service(autobaud_e_ctx_t *ctx);

/* Explicit cancellation restores original UART profile if safe. */
autobaud_e_status_t autobaud_e_cancel(autobaud_e_ctx_t *ctx);

/* Alternative timing policy for event-driven periodic masters.
 * Legacy E/F.1 uses the 2-second candidate dwell unchanged. */
bool autobaud_e_start_dwell(autobaud_e_ctx_t *ctx, const autobaud_e_ops_t *ops,
                           const hw_autobaud_measurement_t *measurement,
                           hw_autobaud_measure_status_t measurement_status,
                           uint32_t budget_ms, uint32_t candidate_timeout_ms);
#endif
