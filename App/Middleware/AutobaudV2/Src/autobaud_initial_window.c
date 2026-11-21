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
  /*
   * M9 on-demand contract:
   * - First activity (bytes, UART errors or an invalid complete request)
   *   ARMS the detector; it never starts capture by itself.
   * - Allow 500 ms for an initial CRC-valid 9600/8E1 request to lock.
   * - After grace, require NEW qualifying RX activity to start capture.
   *   An elapsed timer or a stale invalid-frame flag is not evidence.
   *
   * The first frame is at most 256 bytes on the initial 9600-baud
   * listener, below the 500-ms grace. A later RX burst is thus distinct
   * from that initial request at the supported line rates.
   */
  if (!window->first_rx_seen) {
    if (bytes != 0U || errors != 0U || window->invalid_frame_seen) {
      window->first_rx_seen = true;
      window->first_rx_ms = now_ms;
    }
    window->invalid_frame_seen = false;
    return false;
  }
  window->invalid_frame_seen = false;
  if ((uint32_t)(now_ms - window->first_rx_ms) <
      AUTOBAUD_INITIAL_GRACE_MS)
    return false;
  return autobaud_activity_observe(&window->error_activity, now_ms,
                                   bytes, errors);
}
