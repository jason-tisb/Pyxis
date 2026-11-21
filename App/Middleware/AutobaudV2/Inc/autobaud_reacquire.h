/* M9: portable, passive post-lock UART error qualification.
 * No activity at startup and no reacquisition on master silence/CRC errors.
 * Actual UART ownership and verified rollback belong to the board adapter.
 */
#ifndef PYXIS_AUTOBAUD_REACQUIRE_H
#define PYXIS_AUTOBAUD_REACQUIRE_H
#include <stdbool.h>
#include <stdint.h>
#define AUTOBAUD_REACQUIRE_WINDOW_MS       30000U
#define AUTOBAUD_REACQUIRE_BURST_GAP_MS    50U
#define AUTOBAUD_REACQUIRE_MIN_ERRORS      8U
#define AUTOBAUD_REACQUIRE_MIN_BURSTS      2U

typedef struct {
  uint32_t baseline;
  uint32_t window_start_ms;
  uint32_t last_burst_ms;
  uint32_t errors_in_window;
  uint8_t burst_count;
  bool initialized;
} autobaud_reacquire_t;

/* Reset on initial lock, verified normal exchange or aborted rearm. */
void autobaud_reacquire_reset(autobaud_reacquire_t *ctx,
                             uint32_t total_framing_errors);
/* Returns true only after repeated FE/PE evidence within the window.
 * Caller must still acquire and verify UART ownership before switching.
 */
bool autobaud_reacquire_observe(autobaud_reacquire_t *ctx,
                               uint32_t now_ms,
                               uint32_t total_framing_errors);
#endif
