/* Pyxis Unified Autobaud V2: portable profile/result contracts.
 * Derived from Titania feature/unified-autobaud at ea9ecbbd2b596f7.
 * No HAL, STM32 peripheral, board, Modbus application or RTOS dependency.
 */
#ifndef PYXIS_AUTOBAUD_V2_H
#define PYXIS_AUTOBAUD_V2_H

#include <stdbool.h>
#include <stdint.h>

/* Match Titania's numeric values to allow a staged source-level import.
 * These are NOT Pyxis's persisted parity-register codes. */
typedef enum {
  AUTOBAUD_UART_MODE_8N1 = 0,
  AUTOBAUD_UART_MODE_8E1 = 1,
  AUTOBAUD_UART_MODE_8O1 = 2,
  AUTOBAUD_UART_MODE_8N2 = 3
} autobaud_uart_mode_t;

#define AUTOBAUD_V2_BAUD_COUNT         5U
#define AUTOBAUD_V2_FRAMING_COUNT      4U
#define AUTOBAUD_V2_SCAN_PROFILE_COUNT 20U

typedef enum {
  AUTOBAUD_V2_STATE_IDLE = 0,
  AUTOBAUD_V2_STATE_CAPTURE,
  AUTOBAUD_V2_STATE_SCORE,
  AUTOBAUD_V2_STATE_VALIDATE,
  AUTOBAUD_V2_STATE_RETRY,
  AUTOBAUD_V2_STATE_SCAN,
  AUTOBAUD_V2_STATE_LOCKED,
  AUTOBAUD_V2_STATE_FALLBACK,
  AUTOBAUD_V2_STATE_ERROR
} autobaud_v2_state_t;

typedef enum {
  AUTOBAUD_V2_OUTCOME_NONE = 0,
  AUTOBAUD_V2_OUTCOME_FIXED_CONFIGURED,
  AUTOBAUD_V2_OUTCOME_LOCKED_MEASURED,
  AUTOBAUD_V2_OUTCOME_LOCKED_SCANNED,
  AUTOBAUD_V2_OUTCOME_FALLBACK_ACTIVE,
  AUTOBAUD_V2_OUTCOME_FATAL_ERROR,
  AUTOBAUD_V2_OUTCOME_CANCELLED_RESTORED
} autobaud_v2_outcome_t;

typedef enum {
  AUTOBAUD_V2_LOCK_SOURCE_NONE = 0,
  AUTOBAUD_V2_LOCK_SOURCE_MEASUREMENT,
  AUTOBAUD_V2_LOCK_SOURCE_PROFILE_SCAN
} autobaud_v2_lock_source_t;

typedef enum {
  AUTOBAUD_V2_RX_OWNER_NONE = 0,
  AUTOBAUD_V2_RX_OWNER_SERIAL,
  AUTOBAUD_V2_RX_OWNER_EDGE_CAPTURE
} autobaud_v2_rx_owner_t;

typedef struct {
  uint32_t baud_rate;
  autobaud_uart_mode_t framing;
} autobaud_v2_profile_t;

typedef struct {
  autobaud_v2_state_t state;
  autobaud_v2_outcome_t outcome;
  autobaud_v2_lock_source_t lock_source;
  autobaud_v2_profile_t active_profile;
  uint32_t elapsed_ms;
  uint32_t measurement_attempts;
  uint16_t confidence_permille;
  bool locked;
  bool fallback_used;
} autobaud_v2_result_t;

/* Pure profile checks: no hardware interaction. */
bool autobaud_v2_baud_supported(uint32_t baud_rate);
bool autobaud_v2_framing_supported(autobaud_uart_mode_t framing);
bool autobaud_v2_profile_supported(const autobaud_v2_profile_t *profile);
bool autobaud_v2_state_is_terminal(autobaud_v2_state_t state);
void autobaud_v2_result_reset(autobaud_v2_result_t *result);

#endif /* PYXIS_AUTOBAUD_V2_H */
