#include "autobaud_score.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static void test_all_rates(void)
{
  const uint32_t bauds[] = {9600U,19200U,38400U,57600U,115200U};
  const uint32_t runs[] = {1U,2U,3U,4U,7U,8U,9U};
  for (size_t b=0U;b<5U;b++) {
    uint32_t intervals[42U];
    const uint32_t ticks=(10000000U+bauds[b]/2U)/bauds[b];
    hw_autobaud_measurement_t measured={0U};
    for (size_t i=0U;i<42U;i++)
      intervals[i]=runs[i%7U]*ticks+(uint32_t)(i%3U);
    assert(autobaud_score_intervals(intervals,42U,10000000U,&measured)==
           HW_AUTOBAUD_MEASURE_VALID);
    assert(measured.baud_rate==bauds[b]);
    assert(measured.sample_count>=HW_AUTOBAUD_MIN_VALID_INTERVALS);
    assert(measured.confidence_permille>=750U);
  }
}
static void test_bad_input(void)
{
  uint32_t too_few[]={1042U,2084U,3126U,1042U,2084U,3126U,1042U};
  uint32_t tiny[16U];
  hw_autobaud_measurement_t m={.baud_rate=123U};
  for(size_t i=0U;i<16U;i++) tiny[i]=1U;
  assert(autobaud_score_intervals(too_few,7U,10000000U,&m)==HW_AUTOBAUD_MEASURE_TIMEOUT);
  assert(m.baud_rate==0U);
  assert(autobaud_score_intervals(tiny,16U,10000000U,&m)==HW_AUTOBAUD_MEASURE_AMBIGUOUS);
  assert(autobaud_score_intervals(tiny,16U,0U,&m)==HW_AUTOBAUD_MEASURE_ERROR);
  assert(autobaud_score_intervals(NULL,16U,10000000U,&m)==HW_AUTOBAUD_MEASURE_ERROR);
  assert(autobaud_score_intervals(tiny,16U,10000000U,NULL)==HW_AUTOBAUD_MEASURE_ERROR);
}
static void test_timer_wrap(void)
{
  uint16_t delta=999U;
  assert(autobaud_capture_delta16(100U,1100U,false,&delta));
  assert(delta==1000U);
  assert(!autobaud_capture_delta16(65535U,10U,true,&delta));
  assert(delta==0U);
  assert(!autobaud_capture_delta16(100U,100U,false,&delta));
  assert(!autobaud_capture_delta16(65000U,100U,true,&delta));
  assert(!autobaud_capture_delta16(1U,2U,false,NULL));
}
int main(void)
{
  test_all_rates();test_bad_input();test_timer_wrap();
  puts("autobaud_score_host_tests: PASS");return 0;
}
