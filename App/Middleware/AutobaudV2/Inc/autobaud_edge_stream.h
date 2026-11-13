/* Platform-independent, single-producer edge timestamp buffer.
 * Only an ISR calls on_edge(); only task context calls reset/snapshot,
 * and snapshot requires the capture interrupt to have been stopped first.
 */
#ifndef PYXIS_AUTOBAUD_EDGE_STREAM_H
#define PYXIS_AUTOBAUD_EDGE_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#define AUTOBAUD_EDGE_MAX_INTERVALS 64U
#define AUTOBAUD_EDGE_MIN_TICKS 4U

typedef struct {
  volatile uint32_t intervals[AUTOBAUD_EDGE_MAX_INTERVALS];
  volatile uint32_t previous_tick;
  volatile uint32_t last_edge_ms;
  volatile uint32_t edge_count;
  volatile uint16_t interval_count;
  volatile uint8_t have_previous;
  volatile uint8_t integrity_error;
} autobaud_edge_stream_t;

/* Task-only; must not race an active ISR. */
void autobaud_edge_stream_reset(autobaud_edge_stream_t *stream);

/* O(1) ISR call. Unexpected timer events/CC overcapture are fatal to the
 * measurement, not necessarily to the restorable UART receive path. */
void autobaud_edge_stream_on_edge(autobaud_edge_stream_t *stream,
                                 uint32_t captured_tick,
                                 uint32_t now_ms,
                                 bool integrity_error);

/* A burst is ready after enough samples and the stated quiet interval,
 * or when the fixed buffer fills. No busy loop or delay in this function. */
bool autobaud_edge_stream_ready(const autobaud_edge_stream_t *stream,
                                uint32_t now_ms, uint32_t quiet_ms);

/* Task-only, after disabling and stopping capture interrupt. */
bool autobaud_edge_stream_snapshot(const autobaud_edge_stream_t *stream,
                                  uint32_t *intervals, uint16_t capacity,
                                  uint16_t *count);

#endif /* PYXIS_AUTOBAUD_EDGE_STREAM_H */
