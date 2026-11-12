#include "autobaud_ondemand.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
  uint32_t now;
  int activity;
  int captures;
  int frames;
  int faults;
  autobaud_od_capture_t capture_status;
  autobaud_od_frame_t frame_status;
  bool hw_fatal;
  autobaud_od_probe_result_t initial_probe;
  int probe_calls;
} fake_t;
static uint32_t now_ms(void *u) { return ((fake_t *)u)->now; }
static bool activity(void *u) { fake_t *f = u; int a = f->activity; f->activity = 0; return a != 0; }
static bool cap_begin(void *u, uint32_t ms) { fake_t *f = u; assert(ms >= 10000U); ++f->captures; return true; }
static autobaud_od_capture_t cap_poll(void *u, hw_autobaud_measurement_t *m) {
  fake_t *f = u;
  m->baud_rate = 115200U; m->confidence_permille = 900U; m->sample_count = 16U;
  return f->capture_status;
}
static bool frame_begin(void *u, const hw_autobaud_measurement_t *m, uint32_t b, uint32_t d) {
  fake_t *f = u; assert(m->baud_rate == 115200U); assert(b >= d); ++f->frames; return true;
}
static autobaud_od_frame_t frame_poll(void *u, autobaud_v2_profile_t *p) {
  fake_t *f = u; p->baud_rate = 115200U; p->framing = AUTOBAUD_UART_MODE_8E1;
  return f->frame_status;
}
static autobaud_od_probe_result_t probe_initial(void *u,
                                                  autobaud_v2_profile_t *out) {
  fake_t *f = u;
  ++f->probe_calls;
  if (f->initial_probe == AUTOBAUD_OD_PROBE_LOCKED) {
    out->baud_rate = 9600U;
    out->framing = AUTOBAUD_UART_MODE_8E1;
  }
  return f->initial_probe;
}
static bool fatal_hw(void *u) { return ((fake_t *)u)->hw_fatal; }
static void fault(void *u) { ++((fake_t *)u)->faults; }
static autobaud_od_ops_t ops(fake_t *f) {
  autobaud_od_ops_t o = {0};
  o.now_ms = now_ms; o.activity = activity;
  o.probe_initial = probe_initial; o.capture_begin = cap_begin;
  o.capture_poll = cap_poll; o.framing_begin = frame_begin;
  o.framing_poll = frame_poll; o.fatal = fatal_hw; o.latch_fault = fault; o.user = f;
  return o;
}
static void no_traffic_never_starts(void) {
  fake_t f = {0}; autobaud_od_t c; autobaud_od_ops_t o = ops(&f);
  assert(autobaud_od_init(&c, &o));
  for (unsigned i = 0; i < 2000; ++i) { f.now += 1000U; autobaud_od_service(&c); }
  assert(c.state == AUTOBAUD_OD_WAIT_ACTIVITY && f.captures == 0);
}
static void locks_once_and_survives_infinite_silence(void) {
  fake_t f = {0}; autobaud_od_t c; autobaud_od_ops_t o = ops(&f);
  assert(autobaud_od_init(&c, &o));
  f.activity = 1; autobaud_od_service(&c); assert(f.captures == 1);
  f.capture_status = AUTOBAUD_OD_CAPTURE_VALID;
  autobaud_od_service(&c); assert(f.frames == 1);
  f.frame_status = AUTOBAUD_OD_FRAME_LOCKED;
  autobaud_od_service(&c); assert(autobaud_od_locked(&c));
  f.activity = 1; f.hw_fatal = true; f.now = 100000000U;
  for (int i = 0; i < 5; ++i) autobaud_od_service(&c);
  assert(f.captures == 1 && f.faults == 0);
  assert(c.active.baud_rate == 115200U && c.locks == 1);
}
static void retry_after_timeout_and_cooldown(void) {
  fake_t f = {0}; autobaud_od_t c; autobaud_od_ops_t o = ops(&f);
  assert(autobaud_od_init(&c, &o));
  f.activity = 1; autobaud_od_service(&c);
  f.capture_status = AUTOBAUD_OD_CAPTURE_INCONCLUSIVE;
  autobaud_od_service(&c); assert(c.state == AUTOBAUD_OD_COOLDOWN);
  f.now = AUTOBAUD_OD_COOLDOWN_MS;
  autobaud_od_service(&c); assert(c.state == AUTOBAUD_OD_WAIT_ACTIVITY);
  f.activity = 1; autobaud_od_service(&c); assert(f.captures == 2);
}
static void default_valid_request_locks_without_tim3(void) {
  fake_t f = {0}; autobaud_od_t c; autobaud_od_ops_t o = ops(&f);
  assert(autobaud_od_init(&c, &o));
  f.activity = 1;
  f.initial_probe = AUTOBAUD_OD_PROBE_LOCKED;
  autobaud_od_service(&c);
  assert(autobaud_od_locked(&c) && c.active.baud_rate == 9600U);
  assert(c.active.framing == AUTOBAUD_UART_MODE_8E1);
  assert(c.attempts == 0U && f.captures == 0 && f.frames == 0);
  f.now = 5000000U;
  autobaud_od_service(&c);
  assert(f.captures == 0 && f.probe_calls == 1 && c.locks == 1U);
}
static void no_initial_request_falls_back_to_capture(void) {
  fake_t f = {0}; autobaud_od_t c; autobaud_od_ops_t o = ops(&f);
  assert(autobaud_od_init(&c, &o));
  f.initial_probe = AUTOBAUD_OD_PROBE_WAITING;
  f.activity = 1;
  autobaud_od_service(&c);
  assert(c.state == AUTOBAUD_OD_CAPTURING && f.captures == 1);
}
int main(void) {
  default_valid_request_locks_without_tim3();
  no_initial_request_falls_back_to_capture();
  no_traffic_never_starts();
  locks_once_and_survives_infinite_silence();
  retry_after_timeout_and_cooldown();
  puts("autobaud on-demand supervisor OK");
  return 0;
}
