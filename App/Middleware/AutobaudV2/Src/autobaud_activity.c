#include "autobaud_activity.h"
#include <stddef.h>
void autobaud_activity_reset(autobaud_activity_t *state)
{
  if (state == NULL) return;
  state->pending_events = 0U;
  state->first_ms = 0U;
}
bool autobaud_activity_observe(autobaud_activity_t *state, uint32_t now_ms,
                               uint32_t new_rx_bytes, uint32_t error_events)
{
  if (state == NULL) return false;
  /* Several received bytes in one poll indicate real traffic. An isolated
   * UART noise/error event is insufficient to initiate a 12s capture. */
  if (new_rx_bytes >= 3U || error_events >= 2U) {
    autobaud_activity_reset(state);
    return true;
  }
  if (new_rx_bytes == 0U && error_events == 0U) return false;
  if (state->pending_events == 0U ||
      (uint32_t)(now_ms - state->first_ms) >
          AUTOBAUD_ACTIVITY_CORRELATION_MS) {
    state->pending_events = 1U;
    state->first_ms = now_ms;
    return false;
  }
  autobaud_activity_reset(state);
  return true;
}
