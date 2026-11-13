#include "autobaud_initial_window.h"
#include <stddef.h>

void autobaud_initial_window_reset(autobaud_initial_window_t *window,
                                   uint32_t total_bytes,
                                   uint32_t total_errors)
{
  if (window == NULL) return;
  autobaud_activity_reset(&window->error_activity);
  window->baseline_bytes = total_bytes;
  window->baseline_errors = total_errors;
  window->first_rx_ms = 0U;
  window->first_rx_seen = false;
  window->invalid_frame_seen = false;
}

void autobaud_initial_window_invalid_frame(autobaud_initial_window_t *window)
{
  if (window != NULL) window->invalid_frame_seen = true;
}

bool autobaud_initial_window_trigger(autobaud_initial_window_t *window,
                                     uint32_t now_ms,
                                     uint32_t total_bytes,
                                     uint32_t total_errors)
{
  uint32_t bytes, errors;
  if (window == NULL) return false;
  bytes = total_bytes - window->baseline_bytes;
  errors = total_errors - window->baseline_errors;
  window->baseline_bytes = total_bytes;
  window->baseline_errors = total_errors;
  if (window->invalid_frame_seen) {
    window->invalid_frame_seen = false;
    return true;
  }
  /* Titania's initial grace intentionally counts UART errors before
   * expiry, but defers a pure byte trigger to allow the first CRC-correct
   * request to finish and lock at the default listener. */
  if (autobaud_activity_observe(&window->error_activity, now_ms,
                                0U, errors)) return true;
  if (bytes != 0U && !window->first_rx_seen) {
    window->first_rx_seen = true;
    window->first_rx_ms = now_ms;
  }
  return window->first_rx_seen &&
         (uint32_t)(now_ms - window->first_rx_ms) >=
             AUTOBAUD_INITIAL_GRACE_MS;
}
