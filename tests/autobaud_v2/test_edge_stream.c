/* M5 hardware-neutral TIM2 edge stream, including the real M2 scorer.
 * Test 32-bit counter wrap, bounded ISR events, frame quiet time,
 * overcapture fail-closed, and five original Titania baud candidates.
 */
#include "autobaud_edge_stream.h"
#include "autobaud_score.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_empty_and_reset(void)
{
  autobaud_edge_stream_t s = {0};
  uint32_t out[AUTOBAUD_EDGE_MAX_INTERVALS] = {0};
  uint16_t count = 99U;
  assert(!autobaud_edge_stream_ready(NULL, 0U, 5U));
  assert(!autobaud_edge_stream_snapshot(&s, out, 64U, &count));
  autobaud_edge_stream_reset(&s);
  assert(s.interval_count == 0U && s.have_previous == 0U);
  autobaud_edge_stream_on_edge(NULL, 1U, 1U, false);
  autobaud_edge_stream_reset(NULL);
  autobaud_edge_stream_on_edge(&s, 123U, 7U, false);
  assert(s.edge_count == 1U && s.interval_count == 0U);
  autobaud_edge_stream_on_edge(&s, 125U, 8U, false);
  assert(s.interval_count == 0U);  /* Glitch rejected, baseline advances. */
  autobaud_edge_stream_on_edge(&s, 245U, 9U, false);
  assert(s.interval_count == 1U && s.intervals[0] == 120U);
  assert(!autobaud_edge_stream_snapshot(&s, out, 0U, &count));
  assert(autobaud_edge_stream_snapshot(&s, out, 64U, &count));
  assert(count == 1U && out[0] == 120U);
}

static void test_timer_wrap_and_error_latch(void)
{
  autobaud_edge_stream_t s = {0};
  uint32_t out[64] = {0};
  uint16_t n = 0U;
  autobaud_edge_stream_on_edge(&s, UINT32_MAX - 33U, UINT32_MAX - 4U, false);
  autobaud_edge_stream_on_edge(&s, 65U, UINT32_MAX - 3U, false);
  assert(s.interval_count == 1U && s.intervals[0] == 99U);
  autobaud_edge_stream_on_edge(&s, 100U, 0U, true);
  assert(s.integrity_error != 0U);
  autobaud_edge_stream_on_edge(&s, 200U, 1U, false);
  assert(s.interval_count == 1U);
  assert(!autobaud_edge_stream_snapshot(&s, out, 64U, &n));
  assert(!autobaud_edge_stream_ready(&s, 20U, 5U));
  autobaud_edge_stream_reset(&s);
  assert(s.integrity_error == 0U && s.interval_count == 0U);
}

static void test_five_baud_candidates(void)
{
  static const uint32_t rates[] = {
      9600U, 19200U, 38400U, 57600U, 115200U};
  static const uint32_t runs[] = {1U, 2U, 3U, 4U, 7U, 8U, 9U};
  for (unsigned b = 0U; b < 5U; ++b) {
    autobaud_edge_stream_t s = {0};
    uint32_t out[64] = {0};
    uint32_t tick = 20000U;
    uint16_t count = 0U;
    hw_autobaud_measurement_t measured = {0};
    const uint32_t bits = (10000000U + rates[b] / 2U) / rates[b];
    autobaud_edge_stream_on_edge(&s, tick, 500U, false);
    for (unsigned i = 0U; i < 42U; ++i) {
      tick += runs[i % 7U] * bits + i % 3U;
      autobaud_edge_stream_on_edge(&s, tick, 500U + i, false);
    }
    assert(s.interval_count == 42U);
    assert(!autobaud_edge_stream_ready(&s, 543U, 5U));
    assert(autobaud_edge_stream_ready(&s, 546U, 5U));
    assert(autobaud_edge_stream_snapshot(&s, out, 64U, &count));
    assert(count == 42U);
    assert(autobaud_score_intervals(out, count, 10000000U, &measured) ==
           HW_AUTOBAUD_MEASURE_VALID);
    assert(measured.baud_rate == rates[b]);
    assert(measured.sample_count >= 8U &&
           measured.confidence_permille >= 750U);
  }
}

static void test_bounded_max_samples(void)
{
  autobaud_edge_stream_t s = {0};
  uint32_t out[64] = {0};
  uint16_t n = 0U;
  autobaud_edge_stream_on_edge(&s, 0U, 0U, false);
  for (unsigned i = 1U; i < 1000U; ++i)
    autobaud_edge_stream_on_edge(&s, (uint32_t)i * 100U, i, false);
  assert(s.interval_count == AUTOBAUD_EDGE_MAX_INTERVALS);
  assert(autobaud_edge_stream_ready(&s, 1000U, 5U));
  assert(!autobaud_edge_stream_snapshot(&s, out, 63U, &n));
  assert(autobaud_edge_stream_snapshot(&s, out, 64U, &n));
  assert(n == 64U && out[0] == 100U && out[63] == 100U);
  autobaud_edge_stream_reset(&s);
  assert(s.interval_count == 0U);
}

int main(void)
{
  test_empty_and_reset();
  test_timer_wrap_and_error_latch();
  test_five_baud_candidates();
  test_bounded_max_samples();
  puts("Pyxis M5 TIM2 portable edge stream and scoring: PASS");
  return 0;
}
