#include "autobaud_activity.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
int main(void)
{
  autobaud_activity_t q = {0};
  assert(!autobaud_activity_observe(&q, 0U, 0U, 0U));
  assert(!autobaud_activity_observe(&q, 100U, 0U, 1U));
  assert(!autobaud_activity_observe(&q, 201U, 0U, 0U));
  assert(autobaud_activity_observe(&q, 5000U, 0U, 1U));
  assert(q.pending_events == 0U);
  assert(autobaud_activity_observe(&q, 9000U, 5U, 0U));
  assert(!autobaud_activity_observe(&q, 9100U, 1U, 0U));
  assert(!autobaud_activity_observe(&q, 81000U, 0U, 1U));
  assert(autobaud_activity_observe(&q, 82000U, 0U, 1U));
  autobaud_activity_reset(&q);
  assert(!autobaud_activity_observe(&q, UINT32_MAX - 10U, 1U, 0U));
  assert(autobaud_activity_observe(&q, 10U, 1U, 0U));
  puts("autobaud activity filter OK");
  return 0;
}
