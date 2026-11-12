/* Milestone D.1: deterministic and randomized pure timestamp/fault tests. */
#include "autobaud_timestamp32.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static autobaud_ts32_result_t step(autobaud_ts32_state_t *s,
                                   bool update, bool capture, bool overrun,
                                   uint16_t ccr, uint16_t cnt,
                                   autobaud_ts32_fault_t expect)
{
  autobaud_ts32_event_t event = {
      .update_pending = update, .capture_pending = capture,
      .overcapture = overrun, .captured_count = ccr, .counter = cnt};
  autobaud_ts32_result_t result = {0};
  assert(autobaud_ts32_process(s, &event, &result) == expect);
  return result;
}

static void test_normal_and_overflow(void)
{
  autobaud_ts32_state_t s;
  autobaud_ts32_result_t r;
  autobaud_ts32_reset(&s);
  r = step(&s, false, true, false, 0xFF00U, 0xFF01U, AUTOBAUD_TS32_OK);
  assert(r.captured && !r.has_interval && r.timestamp == 0xFF00U);
  r = step(&s, false, false, false, 0U, 0U, AUTOBAUD_TS32_OK);
  assert(!r.captured);
  r = step(&s, true, false, false, 0U, 4U, AUTOBAUD_TS32_OK);
  assert(!r.captured && s.epoch == 1U && s.update_events == 1U);
  r = step(&s, false, true, false, 0x00D0U, 0x00D2U, AUTOBAUD_TS32_OK);
  assert(r.has_interval && r.interval == 0x1D0U);
  for (uint32_t i = 0U; i < 3U; i++)
    (void)step(&s, true, false, false, 0U, 1U, AUTOBAUD_TS32_OK);
  r = step(&s, false, true, false, 0x100U, 0x105U, AUTOBAUD_TS32_OK);
  assert(r.has_interval && s.epoch == 4U &&
         r.interval == (3U * 65536U + 0x100U - 0xD0U));
}

static void test_ambiguous_and_late(void)
{
  autobaud_ts32_state_t s;
  autobaud_ts32_reset(&s);
  (void)step(&s, true, true, false, 0xFFFFU, 0U,
             AUTOBAUD_TS32_AMBIGUOUS_EPOCH);
  assert(s.epoch == 0U);
  (void)step(&s, false, true, false, 100U, 102U,
             AUTOBAUD_TS32_AMBIGUOUS_EPOCH);
  autobaud_ts32_reset(&s);
  (void)step(&s, false, true, true, 100U, 102U,
             AUTOBAUD_TS32_OVERCAPTURE);
  autobaud_ts32_reset(&s);
  (void)step(&s, true, false, false, 0U, 700U,
             AUTOBAUD_TS32_LATE_INTERRUPT);
  autobaud_ts32_reset(&s);
  (void)step(&s, false, true, false, 100U, 700U,
             AUTOBAUD_TS32_LATE_INTERRUPT);
  autobaud_ts32_reset(&s);
  (void)step(&s, false, true, false, 65535U, 1U,
             AUTOBAUD_TS32_AMBIGUOUS_EPOCH);
  autobaud_ts32_reset(&s);
  (void)step(&s, false, true, false, 200U, 202U, AUTOBAUD_TS32_OK);
  (void)step(&s, false, true, false, 200U, 203U,
             AUTOBAUD_TS32_DISCONTINUITY);
  assert(s.fault == AUTOBAUD_TS32_DISCONTINUITY);
}

static void test_32bit_wrap_and_reset(void)
{
  autobaud_ts32_state_t s;
  autobaud_ts32_result_t r;
  autobaud_ts32_reset(&s);
  s.epoch = 65535U;
  s.have_previous = true;
  s.last_capture = 0xFFFFFF00U;
  (void)step(&s, true, false, false, 0U, 0U, AUTOBAUD_TS32_OK);
  r = step(&s, false, true, false, 0x90U, 0x92U, AUTOBAUD_TS32_OK);
  assert(r.timestamp == 0x90U && r.interval == 0x190U);
  autobaud_ts32_reset(&s);
  assert(s.epoch == 0U && !s.have_previous &&
         s.fault == AUTOBAUD_TS32_OK);
}

static void test_reference_and_snapshot(void)
{
  autobaud_ts32_state_t s;
  uint32_t ts = UINT32_MAX;
  uint32_t error = UINT32_MAX;
  uint32_t old_dwt = UINT32_MAX - 4095U;
  uint32_t new_dwt = 4096U;
  autobaud_ts32_reset(&s);

  assert(autobaud_ts32_reference_consistent(25000000U, 1000000U,
        250000000U, 10000000U, 512U, &error));
  assert(error == 0U);
  assert(!autobaud_ts32_reference_consistent(25000000U, 1000000U - 65536U,
          250000000U, 10000000U, 512U, &error));
  assert(error == 65536U);
  assert(!autobaud_ts32_reference_consistent(25000000U, 1000000U - 131072U,
          250000000U, 10000000U, 512U, &error));
  assert(error == 131072U);
  assert(!autobaud_ts32_reference_consistent(25000000U, 1000000U,
          0U, 10000000U, 512U, &error));
  assert(!autobaud_ts32_reference_consistent(25000000U, 1000000U,
          250000000U, 0U, 512U, &error));
  assert(autobaud_ts32_reference_consistent(new_dwt-old_dwt, 328U,
          250000000U, 10000000U, 1U, &error));
  assert(autobaud_ts32_snapshot(&s, 100U, false, &ts) && ts == 100U);
  assert(autobaud_ts32_snapshot(&s, 100U, true, &ts) &&
         ts == 65636U);
  assert(!autobaud_ts32_snapshot(&s, 700U, true, &ts));
  s.fault = AUTOBAUD_TS32_REFERENCE_FAULT;
  assert(!autobaud_ts32_snapshot(&s, 1U, false, &ts));
}

static uint32_t rng_state = 0x1abf3cd9U;
static uint32_t random32(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

static void test_randomized_reference_model(void)
{
  autobaud_ts32_state_t s;
  uint64_t now = 0U;
  uint64_t prior = 0U;
  bool have_prior = false;
  autobaud_ts32_reset(&s);

  for (unsigned i = 0; i < 20000U; i++) {
    uint32_t delta = 70U + (random32() % 1000U);
    uint64_t next = now + delta;
    while ((now >> 16) < (next >> 16)) {
      now = (now & ~UINT64_C(65535)) + UINT64_C(65536);
      (void)step(&s, true, false, false, 0U, 0U, AUTOBAUD_TS32_OK);
    }
    now = next;
    {
      uint16_t ccr = (uint16_t)now;
      uint16_t lag = (uint16_t)((65535U - (uint32_t)ccr) < 3U
                                     ? (65535U - (uint32_t)ccr) : 3U);
      autobaud_ts32_result_t r =
          step(&s, false, true, false, ccr,
               (uint16_t)((uint32_t)ccr + lag), AUTOBAUD_TS32_OK);
      assert(r.captured && r.timestamp == (uint32_t)now);
      assert(r.has_interval == have_prior);
      if (have_prior) assert(r.interval == (uint32_t)(now - prior));
    }
    prior = now;
    have_prior = true;
  }
  assert(s.fault == AUTOBAUD_TS32_OK && s.captured_edges == 20000U);
}

static void test_fault_injection_never_valid(void)
{
  autobaud_ts32_state_t s;
  autobaud_ts32_result_t r;
  autobaud_ts32_reset(&s);
  /* Coalesced two hardware periods into one UIF: the epoch count appears
   * plausible modulo 16 bits but DWT confirms 65536 ticks were missed. */
  (void)step(&s, true, false, false, 0U, 1U, AUTOBAUD_TS32_OK);
  assert(autobaud_ts32_snapshot(&s, 1U, false, &r.timestamp));
  assert(!autobaud_ts32_reference_consistent(131073U * 25U, r.timestamp,
         250000000U, 10000000U, 512U, NULL));
  /* Releasing after a fault must reset the session; stale status is sticky
   * until reset, even when a fresh capture event looks valid. */
  (void)step(&s, false, true, true, 100U, 101U,
             AUTOBAUD_TS32_OVERCAPTURE);
  r = step(&s, false, true, false, 200U, 201U,
           AUTOBAUD_TS32_OVERCAPTURE);
  assert(!r.captured);
}


static void test_randomized_fail_closed_events(void)
{
  autobaud_ts32_state_t state;
  for (unsigned iteration = 0U; iteration < 10000U; iteration++) {
    autobaud_ts32_result_t output;
    uint32_t choice = random32() % 7U;
    uint16_t base = (uint16_t)(100U + (random32() % 30000U));
    autobaud_ts32_reset(&state);
    switch (choice) {
      case 0U:
        output = step(&state, true, true, false, base, base,
                      AUTOBAUD_TS32_AMBIGUOUS_EPOCH);
        break;
      case 1U:
        output = step(&state, false, true, true, base, base,
                      AUTOBAUD_TS32_OVERCAPTURE);
        break;
      case 2U:
        output = step(&state, false, true, false,
                      (uint16_t)(base + 2U), base,
                      AUTOBAUD_TS32_AMBIGUOUS_EPOCH);
        break;
      case 3U:
        output = step(&state, false, true, false, base,
                      (uint16_t)(base + 600U),
                      AUTOBAUD_TS32_LATE_INTERRUPT);
        break;
      case 4U:
        output = step(&state, true, false, false, 0U, 600U,
                      AUTOBAUD_TS32_LATE_INTERRUPT);
        break;
      case 5U:
        (void)step(&state, false, true, false, base,
                   (uint16_t)(base + 1U), AUTOBAUD_TS32_OK);
        output = step(&state, false, true, false, base,
                      (uint16_t)(base + 2U),
                      AUTOBAUD_TS32_DISCONTINUITY);
        break;
      default: {
        uint32_t ticks;
        assert(autobaud_ts32_snapshot(&state, base, false, &ticks));
        assert(!autobaud_ts32_reference_consistent(
            ((uint32_t)base + 65536U) * 25U, ticks,
            250000000U, 10000000U, 512U, NULL));
        output = step(&state, false, true, true, base, base,
                      AUTOBAUD_TS32_OVERCAPTURE);
        break;
      }
    }
    assert(!output.captured && !output.has_interval);
    assert(output.timestamp == 0U && output.interval == 0U);
    assert(state.fault != AUTOBAUD_TS32_OK);
  }
}

int main(void)
{
  test_normal_and_overflow();
  test_ambiguous_and_late();
  test_32bit_wrap_and_reset();
  test_reference_and_snapshot();
  test_randomized_reference_model();
  test_fault_injection_never_valid();
  test_randomized_fail_closed_events();
  puts("autobaud_timestamp32_host_tests: PASS");
  return 0;
}
