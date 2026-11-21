/* Hardware-independent post-lock rearm guard. */
#include "autobaud_reacquire.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void silence_and_noise(void)
{
  autobaud_reacquire_t w = {0};
  autobaud_reacquire_reset(&w, 10U);
  for (uint32_t t = 0U; t < 600000U; t += 100U)
    assert(!autobaud_reacquire_observe(&w, t, 10U));
  assert(!autobaud_reacquire_observe(&w, 600000U, 11U));
  assert(!autobaud_reacquire_observe(&w, 600100U, 11U));
  autobaud_reacquire_reset(&w, 11U); /* verified healthy exchange */
  assert(!autobaud_reacquire_observe(&w, 600200U, 11U));
}

static void multiple_qualified_bursts(void)
{
  autobaud_reacquire_t w = {0};
  autobaud_reacquire_reset(&w, 100U);
  assert(!autobaud_reacquire_observe(&w, 1000U, 107U));
  assert(!autobaud_reacquire_observe(&w, 1030U, 108U));
  assert(autobaud_reacquire_observe(&w, 1100U, 109U));

  autobaud_reacquire_reset(&w, 200U);
  assert(!autobaud_reacquire_observe(&w, 1000U, 207U));
  /* A new window must not inherit the prior burst's errors. */
  assert(!autobaud_reacquire_observe(
      &w, 1000U + AUTOBAUD_REACQUIRE_WINDOW_MS, 208U));
  assert(!autobaud_reacquire_observe(&w, 1000U +
      AUTOBAUD_REACQUIRE_WINDOW_MS + 100U, 209U));
}

static void rollover_and_long_poll(void)
{
  autobaud_reacquire_t w = {0};
  autobaud_reacquire_reset(&w, UINT32_MAX - 3U);
  assert(!autobaud_reacquire_observe(&w, UINT32_MAX - 20U, 3U));
  assert(autobaud_reacquire_observe(&w, 100U, 5U));

  autobaud_reacquire_reset(&w, 0U);
  assert(!autobaud_reacquire_observe(&w, 1000U, 5U));
  assert(autobaud_reacquire_observe(&w, 11000U, 8U));
}

int main(void)
{
  silence_and_noise();
  multiple_qualified_bursts();
  rollover_and_long_poll();
  puts("Pyxis M9 post-lock UART FE/PE evidence guard: PASS");
  return 0;
}
