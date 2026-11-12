/* Native, hardware-independent M1 contract test; no STM32 or RTOS headers. */
#include "autobaud_v2.h"
#include "autobaud_v2_measurement.h"
#include "autobaud_ondemand.h"
#include "pyxis_autobaud_port.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(AUTOBAUD_V2_BAUD_COUNT == 5U, "expected baud candidates");
_Static_assert(AUTOBAUD_V2_FRAMING_COUNT == 4U, "expected RTU framings");
_Static_assert(AUTOBAUD_V2_SCAN_PROFILE_COUNT == 20U, "candidate matrix");
_Static_assert(AUTOBAUD_UART_MODE_8N1 == 0, "Titania ABI 8N1");
_Static_assert(AUTOBAUD_UART_MODE_8E1 == 1, "Titania ABI 8E1");
_Static_assert(AUTOBAUD_UART_MODE_8O1 == 2, "Titania ABI 8O1");
_Static_assert(AUTOBAUD_UART_MODE_8N2 == 3, "Titania ABI 8N2");

static uint32_t fake_now(void *user)
{
  return *(uint32_t *)user;
}
static bool fake_activity(void *user)
{
  return *(uint32_t *)user != 0U;
}
static autobaud_od_probe_result_t fake_probe(
    void *user, autobaud_v2_profile_t *out)
{
  (void)user;
  out->baud_rate = 9600U;
  out->framing = AUTOBAUD_UART_MODE_8E1;
  return AUTOBAUD_OD_PROBE_LOCKED;
}
static bool fake_capture_begin(void *user, uint32_t timeout_ms)
{
  (void)user;
  return timeout_ms == AUTOBAUD_OD_CAPTURE_MS;
}
static autobaud_od_capture_t fake_capture_poll(
    void *user, hw_autobaud_measurement_t *out)
{
  (void)user;
  out->baud_rate = 19200U;
  out->sample_count = HW_AUTOBAUD_MIN_VALID_INTERVALS;
  out->confidence_permille = 900U;
  return AUTOBAUD_OD_CAPTURE_VALID;
}
static bool fake_framing_begin(void *user,
                               const hw_autobaud_measurement_t *measurement,
                               uint32_t budget_ms, uint32_t dwell_ms)
{
  (void)user;
  return measurement->baud_rate == 19200U &&
         budget_ms == AUTOBAUD_OD_FRAMING_MS &&
         dwell_ms == AUTOBAUD_OD_FRAMING_DWELL_MS;
}
static autobaud_od_frame_t fake_framing_poll(
    void *user, autobaud_v2_profile_t *out)
{
  (void)user;
  out->baud_rate = 19200U;
  out->framing = AUTOBAUD_UART_MODE_8N1;
  return AUTOBAUD_OD_FRAME_LOCKED;
}
static bool fake_fatal(void *user)
{
  (void)user;
  return false;
}
static void fake_latch(void *user)
{
  *(uint32_t *)user = 0U;
}

int main(void)
{
  static const uint32_t bauds[] = {
      9600U, 19200U, 38400U, 57600U, 115200U};
  static const autobaud_uart_mode_t frames[] = {
      AUTOBAUD_UART_MODE_8E1, AUTOBAUD_UART_MODE_8N2,
      AUTOBAUD_UART_MODE_8O1, AUTOBAUD_UART_MODE_8N1};
  uint32_t clock_ms = 37U;
  autobaud_v2_profile_t p = {0};
  autobaud_v2_result_t result = {0};
  hw_autobaud_measurement_t measurement = {0};
  autobaud_od_ops_t ops = {
      .now_ms = fake_now,
      .activity = fake_activity,
      .probe_initial = fake_probe,
      .capture_begin = fake_capture_begin,
      .capture_poll = fake_capture_poll,
      .framing_begin = fake_framing_begin,
      .framing_poll = fake_framing_poll,
      .fatal = fake_fatal,
      .latch_fault = fake_latch,
      .user = &clock_ms};
  unsigned int i, j;

  assert(!autobaud_v2_profile_supported(NULL));
  assert(!autobaud_v2_baud_supported(4800U));
  assert(!autobaud_v2_framing_supported((autobaud_uart_mode_t)99));
  for (i = 0U; i < AUTOBAUD_V2_BAUD_COUNT; ++i) {
    for (j = 0U; j < AUTOBAUD_V2_FRAMING_COUNT; ++j) {
      p.baud_rate = bauds[i];
      p.framing = frames[j];
      assert(autobaud_v2_profile_supported(&p));
    }
  }
  p.baud_rate = 14400U;
  assert(!autobaud_v2_profile_supported(&p));
  assert(!autobaud_v2_state_is_terminal(AUTOBAUD_V2_STATE_CAPTURE));
  assert(autobaud_v2_state_is_terminal(AUTOBAUD_V2_STATE_LOCKED));
  assert(autobaud_v2_state_is_terminal(AUTOBAUD_V2_STATE_FALLBACK));
  assert(autobaud_v2_state_is_terminal(AUTOBAUD_V2_STATE_ERROR));

  result.state = AUTOBAUD_V2_STATE_LOCKED;
  result.locked = true;
  result.active_profile.baud_rate = 115200U;
  autobaud_v2_result_reset(&result);
  assert(result.state == AUTOBAUD_V2_STATE_IDLE);
  assert(result.outcome == AUTOBAUD_V2_OUTCOME_NONE);
  assert(result.lock_source == AUTOBAUD_V2_LOCK_SOURCE_NONE);
  assert(result.active_profile.baud_rate == 0U && !result.locked);
  autobaud_v2_result_reset(NULL);

  assert(ops.now_ms(ops.user) == 37U);
  assert(ops.activity(ops.user));
  assert(ops.probe_initial(ops.user, &p) == AUTOBAUD_OD_PROBE_LOCKED);
  assert(p.baud_rate == 9600U && p.framing == AUTOBAUD_UART_MODE_8E1);
  assert(ops.capture_begin(ops.user, AUTOBAUD_OD_CAPTURE_MS));
  assert(ops.capture_poll(ops.user, &measurement) == AUTOBAUD_OD_CAPTURE_VALID);
  assert(measurement.sample_count == HW_AUTOBAUD_MIN_VALID_INTERVALS);
  assert(ops.framing_begin(ops.user, &measurement,
                           AUTOBAUD_OD_FRAMING_MS,
                           AUTOBAUD_OD_FRAMING_DWELL_MS));
  assert(ops.framing_poll(ops.user, &p) == AUTOBAUD_OD_FRAME_LOCKED);
  assert(autobaud_v2_profile_supported(&p));
  assert(!ops.fatal(ops.user));
  ops.latch_fault(ops.user);
  assert(clock_ms == 0U && !ops.activity(ops.user));

  puts("Pyxis Autobaud V2 M1 portable contracts: PASS");
  return 0;
}
