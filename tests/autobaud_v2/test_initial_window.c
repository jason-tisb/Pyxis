#include "autobaud_initial_window.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void grace(void)
{
  autobaud_initial_window_t w = {0};
  autobaud_initial_window_reset(&w, 4U, 0U);
  for (uint32_t t = 0U; t < 100000U; t += 100U)
    assert(!autobaud_initial_window_trigger(&w,t,4U,0U));
  assert(!autobaud_initial_window_trigger(&w,100000U,5U,0U));
  assert(!autobaud_initial_window_trigger(&w,100499U,6U,0U));
  assert(autobaud_initial_window_trigger(&w,100500U,7U,0U));
  autobaud_initial_window_reset(&w,0U,0U);
  assert(!autobaud_initial_window_trigger(&w,10U,1U,0U));
  autobaud_initial_window_invalid_frame(&w);
  assert(autobaud_initial_window_trigger(&w,11U,1U,0U));
  assert(!autobaud_initial_window_trigger(&w,12U,1U,0U));
}

static void errors_and_wrap(void)
{
  autobaud_initial_window_t w = {0};
  autobaud_initial_window_reset(&w,0U,0U);
  assert(!autobaud_initial_window_trigger(&w,100U,0U,1U));
  assert(autobaud_initial_window_trigger(&w,120U,0U,2U));
  autobaud_initial_window_reset(&w,0U,0U);
  assert(autobaud_initial_window_trigger(&w,200U,0U,2U));

  autobaud_initial_window_reset(&w,UINT32_MAX-5U,UINT32_MAX);
  assert(!autobaud_initial_window_trigger(&w,UINT32_MAX-20U,
                                           UINT32_MAX-2U,UINT32_MAX));
  /* First byte observed at wrap; elapsed gate is wrap-safe. */
  autobaud_initial_window_reset(&w,0U,0U);
  assert(!autobaud_initial_window_trigger(&w,UINT32_MAX-50U,0U,0U));
  assert(!autobaud_initial_window_trigger(&w,UINT32_MAX-30U,0U,0U));
  assert(!autobaud_initial_window_trigger(&w,UINT32_MAX-10U,1U,0U));
  assert(!autobaud_initial_window_trigger(&w,488U,1U,0U));
  assert(autobaud_initial_window_trigger(&w,490U,1U,0U));
}

int main(void)
{
  grace();
  errors_and_wrap();
  puts("Pyxis M6 Titania initial grace window: PASS");
  return 0;
}
