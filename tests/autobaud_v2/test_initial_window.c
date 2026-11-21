#include "autobaud_initial_window.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* A first RX burst must never start capture on a timer alone. */
static void first_burst_then_silence(void)
{
  autobaud_initial_window_t w = {0};
  autobaud_initial_window_reset(&w, 4U, 0U);
  for (uint32_t t = 0U; t < 100000U; t += 100U)
    assert(!autobaud_initial_window_trigger(&w, t, 4U, 0U));

  assert(!autobaud_initial_window_trigger(&w, 100000U, 5U, 0U));
  assert(!autobaud_initial_window_trigger(&w, 100100U, 6U, 0U));
  assert(!autobaud_initial_window_trigger(&w, 100499U, 6U, 0U));
  assert(!autobaud_initial_window_trigger(&w, 100500U, 6U, 0U));
  assert(!autobaud_initial_window_trigger(&w, 300000U, 6U, 0U));
  /* Fresh, complete second request triggers detection immediately. */
  assert(autobaud_initial_window_trigger(&w, 300001U, 14U, 0U));
}

static void invalid_first_request_does_not_force_capture(void)
{
  autobaud_initial_window_t w = {0};
  autobaud_initial_window_reset(&w, 0U, 0U);
  assert(!autobaud_initial_window_trigger(&w, 10U, 1U, 0U));
  autobaud_initial_window_invalid_frame(&w);
  assert(!autobaud_initial_window_trigger(&w, 11U, 1U, 0U));
  assert(!autobaud_initial_window_trigger(&w, 700U, 1U, 0U));
  /* Invalid first-frame evidence is not replayed as a fresh trigger. */
  assert(autobaud_initial_window_trigger(&w, 701U, 9U, 0U));

  autobaud_initial_window_reset(&w, 0U, 0U);
  autobaud_initial_window_invalid_frame(&w);
  assert(!autobaud_initial_window_trigger(&w, 10U, 0U, 0U));
  assert(!autobaud_initial_window_trigger(&w, 1010U, 0U, 0U));
  assert(autobaud_initial_window_trigger(&w, 1011U, 8U, 0U));
}

static void errors_and_wrap(void)
{
  autobaud_initial_window_t w = {0};
  autobaud_initial_window_reset(&w, 0U, 0U);
  assert(!autobaud_initial_window_trigger(&w, 100U, 0U, 1U));
  assert(!autobaud_initial_window_trigger(&w, 120U, 0U, 2U));
  assert(!autobaud_initial_window_trigger(&w, 1000U, 0U, 2U));
  /* Multiple fresh errors after grace can qualify the next request. */
  assert(autobaud_initial_window_trigger(&w, 1010U, 0U, 4U));

  autobaud_initial_window_reset(&w, 0U, 0U);
  assert(!autobaud_initial_window_trigger(&w, 200U, 0U, 2U));
  assert(!autobaud_initial_window_trigger(&w, 701U, 0U, 2U));
  assert(autobaud_initial_window_trigger(&w, 702U, 0U, 4U));

  autobaud_initial_window_reset(&w, 0U, 0U);
  assert(!autobaud_initial_window_trigger(&w, UINT32_MAX - 10U, 1U, 0U));
  assert(!autobaud_initial_window_trigger(&w, 488U, 1U, 0U));
  assert(!autobaud_initial_window_trigger(&w, 490U, 1U, 0U));
  assert(autobaud_initial_window_trigger(&w, 491U, 9U, 0U));
}

int main(void)
{
  first_burst_then_silence();
  invalid_first_request_does_not_force_capture();
  errors_and_wrap();
  puts("Pyxis M9 second-activity trigger and wrap: PASS");
  return 0;
}
