#ifndef AUTOBAUD_ACTIVITY_H
#define AUTOBAUD_ACTIVITY_H
#include <stdbool.h>
#include <stdint.h>
/* Software-only evidence filter. This never changes UART or Modbus state. */
typedef struct {
  uint32_t first_ms;
  uint8_t pending_events;
} autobaud_activity_t;
#define AUTOBAUD_ACTIVITY_CORRELATION_MS 60000U
void autobaud_activity_reset(autobaud_activity_t *state);
bool autobaud_activity_observe(autobaud_activity_t *state, uint32_t now_ms,
                               uint32_t new_rx_bytes, uint32_t error_events);
#endif
