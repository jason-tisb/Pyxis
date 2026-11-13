#include "autobaud_edge_stream.h"
#include <stddef.h>

void autobaud_edge_stream_reset(autobaud_edge_stream_t *stream)
{
  if (stream == NULL) return;
  for (uint16_t i = 0U; i < AUTOBAUD_EDGE_MAX_INTERVALS; ++i)
    stream->intervals[i] = 0U;
  stream->previous_tick = 0U;
  stream->last_edge_ms = 0U;
  stream->edge_count = 0U;
  stream->interval_count = 0U;
  stream->have_previous = 0U;
  stream->integrity_error = 0U;
}

void autobaud_edge_stream_on_edge(autobaud_edge_stream_t *stream,
                                 uint32_t captured_tick,
                                 uint32_t now_ms,
                                 bool integrity_error)
{
  uint32_t delta;
  uint16_t count;
  if (stream == NULL) return;
  if (integrity_error) {
    stream->integrity_error = 1U;
    return;
  }
  if (stream->integrity_error != 0U) return;
  if (stream->edge_count != UINT32_MAX) ++stream->edge_count;
  stream->last_edge_ms = now_ms;
  if (stream->have_previous == 0U) {
    stream->previous_tick = captured_tick;
    stream->have_previous = 1U;
    return;
  }
  delta = captured_tick - stream->previous_tick; /* 32-bit modulo */
  stream->previous_tick = captured_tick;
  if (delta < AUTOBAUD_EDGE_MIN_TICKS) return;
  count = stream->interval_count;
  if (count >= AUTOBAUD_EDGE_MAX_INTERVALS) return;
  stream->intervals[count] = delta;
  stream->interval_count = (uint16_t)(count + 1U);
}

bool autobaud_edge_stream_ready(const autobaud_edge_stream_t *stream,
                                uint32_t now_ms, uint32_t quiet_ms)
{
  if (stream == NULL || stream->integrity_error != 0U) return false;
  if (stream->interval_count >= AUTOBAUD_EDGE_MAX_INTERVALS)
    return true;
  return (stream->interval_count >= 8U) &&
         ((uint32_t)(now_ms - stream->last_edge_ms) >= quiet_ms);
}

bool autobaud_edge_stream_snapshot(const autobaud_edge_stream_t *stream,
                                  uint32_t *intervals, uint16_t capacity,
                                  uint16_t *count)
{
  uint16_t n;
  if (stream == NULL || intervals == NULL || count == NULL ||
      stream->integrity_error != 0U) return false;
  n = stream->interval_count;
  if (n == 0U || n > AUTOBAUD_EDGE_MAX_INTERVALS || capacity < n)
    return false;
  for (uint16_t i = 0U; i < n; ++i)
    intervals[i] = stream->intervals[i];
  *count = n;
  return true;
}
