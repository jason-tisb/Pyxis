#include "autobaud_timestamp32.h"
#include <stddef.h>

void autobaud_ts32_reset(autobaud_ts32_state_t *state)
{
  if (state != NULL) {
    *state = (autobaud_ts32_state_t){0};
  }
}

static autobaud_ts32_fault_t ts32_fail(autobaud_ts32_state_t *state,
                                      autobaud_ts32_fault_t fault)
{
  state->fault = fault;
  return fault;
}

autobaud_ts32_fault_t autobaud_ts32_process(
    autobaud_ts32_state_t *state, const autobaud_ts32_event_t *event,
    autobaud_ts32_result_t *result)
{
  uint32_t timestamp;
  uint32_t delta;

  if (result != NULL) *result = (autobaud_ts32_result_t){0};
  if ((state == NULL) || (event == NULL) || (result == NULL))
    return AUTOBAUD_TS32_CONTROL_FAULT;
  if (state->fault != AUTOBAUD_TS32_OK) return state->fault;

  if (event->overcapture)
    return ts32_fail(state, AUTOBAUD_TS32_OVERCAPTURE);

  /* When update and capture are simultaneously pending, the hardware
   * event ordering is unknown. Reject instead of guessing.
   * This deliberately sacrifices a rare burst for timestamp integrity.
   */
  if (event->update_pending && event->capture_pending)
    return ts32_fail(state, AUTOBAUD_TS32_AMBIGUOUS_EPOCH);

  if (event->update_pending) {
    if (event->counter > AUTOBAUD_TS32_MAX_UPDATE_LAG_TICKS)
      return ts32_fail(state, AUTOBAUD_TS32_LATE_INTERRUPT);
    state->epoch++;
    state->update_events++;
  }

  if (!event->capture_pending) return AUTOBAUD_TS32_OK;

  /* With no pending update, an edge greater than the live count implies
   * rollover while sampling, a lost update or a non-atomic IRQ snapshot.
   */
  if (event->captured_count > event->counter)
    return ts32_fail(state, AUTOBAUD_TS32_AMBIGUOUS_EPOCH);
  if (((uint32_t)event->counter - (uint32_t)event->captured_count) >
      AUTOBAUD_TS32_MAX_CAPTURE_LAG_TICKS)
    return ts32_fail(state, AUTOBAUD_TS32_LATE_INTERRUPT);

  timestamp = (state->epoch << 16) | (uint32_t)event->captured_count;
  if (state->have_previous) {
    delta = timestamp - state->last_capture;
    if ((delta == 0U) || (delta > AUTOBAUD_TS32_MAX_ATTEMPT_TICKS))
      return ts32_fail(state, AUTOBAUD_TS32_DISCONTINUITY);
    result->has_interval = true;
    result->interval = delta;
  }

  /* Publish evidence only after every validation succeeds. A failed
   * timestamp must not expose apparently useful data to a caller.
   */
  result->captured = true;
  result->timestamp = timestamp;
  state->captured_edges++;
  state->last_capture = timestamp;
  state->have_previous = true;
  return AUTOBAUD_TS32_OK;
}

bool autobaud_ts32_snapshot(const autobaud_ts32_state_t *state,
                            uint16_t counter, bool update_pending,
                            uint32_t *timestamp)
{
  uint32_t epoch;
  if (timestamp != NULL) *timestamp = 0U;
  if ((state == NULL) || (timestamp == NULL) ||
      (state->fault != AUTOBAUD_TS32_OK))
    return false;
  if (update_pending && (counter > AUTOBAUD_TS32_MAX_UPDATE_LAG_TICKS))
    return false;
  epoch = state->epoch + (update_pending ? 1U : 0U);
  *timestamp = (epoch << 16) | (uint32_t)counter;
  return true;
}

bool autobaud_ts32_reference_consistent(uint32_t reference_elapsed,
                                          uint32_t timer_elapsed_ticks,
                                          uint32_t reference_hz, uint32_t timer_hz,
                                  uint32_t tolerance_ticks,
                                  uint32_t *error_ticks)
{
  uint64_t expected;
  uint64_t observed = (uint64_t)timer_elapsed_ticks;
  uint64_t error;

  if (error_ticks != NULL) *error_ticks = UINT32_MAX;
  if ((reference_hz == 0U) || (timer_hz == 0U) ||
      (timer_hz > reference_hz) ||
      (timer_elapsed_ticks > AUTOBAUD_TS32_MAX_ATTEMPT_TICKS))
    return false;

  expected = (((uint64_t)reference_elapsed * (uint64_t)timer_hz) +
              ((uint64_t)reference_hz / 2ULL)) / (uint64_t)reference_hz;
  if (expected > AUTOBAUD_TS32_MAX_ATTEMPT_TICKS) return false;
  error = (expected >= observed) ? expected - observed : observed - expected;
  if (error_ticks != NULL) *error_ticks = (uint32_t)error;
  return error <= (uint64_t)tolerance_ticks;
}

