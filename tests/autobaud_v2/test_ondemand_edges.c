/* Pyxis M3: conservative on-demand supervisor edge-case coverage.
 * No hardware register access, UART, RTOS, Modbus or application callbacks.
 */
#include "autobaud_ondemand.h"
#include "autobaud_v2_framing.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  uint32_t now;
  bool rx_activity;
  bool fatal;
  bool capture_begin_ok;
  bool framing_begin_ok;
  autobaud_od_probe_result_t probe_status;
  autobaud_od_capture_t capture_status;
  autobaud_od_frame_t frame_status;
  autobaud_v2_profile_t initial_profile;
  autobaud_v2_profile_t final_profile;
  hw_autobaud_measurement_t measurement;
  uint32_t probes;
  uint32_t capture_starts;
  uint32_t capture_polls;
  uint32_t framing_starts;
  uint32_t framing_polls;
  uint32_t fault_latches;
} mock_t;

static uint32_t now_ms(void *u) { return ((mock_t *)u)->now; }
static bool activity(void *u)
{
  mock_t *m = u;
  bool observed = m->rx_activity;
  m->rx_activity = false;
  return observed;
}
static autobaud_od_probe_result_t probe(void *u, autobaud_v2_profile_t *p)
{
  mock_t *m = u;
  ++m->probes;
  *p = m->initial_profile;
  return m->probe_status;
}
static bool capture_begin(void *u, uint32_t ms)
{
  mock_t *m = u;
  assert(ms == AUTOBAUD_OD_CAPTURE_MS);
  ++m->capture_starts;
  return m->capture_begin_ok;
}
static autobaud_od_capture_t capture_poll(void *u,
                                           hw_autobaud_measurement_t *m)
{
  mock_t *mock = u;
  ++mock->capture_polls;
  *m = mock->measurement;
  return mock->capture_status;
}
static bool framing_begin(void *u,
                           const hw_autobaud_measurement_t *measurement,
                           uint32_t budget, uint32_t dwell)
{
  mock_t *m = u;
  assert(budget == AUTOBAUD_OD_FRAMING_MS);
  assert(dwell == AUTOBAUD_OD_FRAMING_DWELL_MS);
  assert(measurement->baud_rate == m->measurement.baud_rate);
  ++m->framing_starts;
  return m->framing_begin_ok;
}
static autobaud_od_frame_t framing_poll(void *u, autobaud_v2_profile_t *p)
{
  mock_t *m = u;
  ++m->framing_polls;
  *p = m->final_profile;
  return m->frame_status;
}
static bool fatal(void *u) { return ((mock_t *)u)->fatal; }
static void latch(void *u)
{
  mock_t *m = u;
  ++m->fault_latches;
  m->fatal = true;
}
static autobaud_od_ops_t ops(mock_t *m)
{
  const autobaud_od_ops_t o = {
      .now_ms = now_ms, .activity = activity,
      .probe_initial = probe, .capture_begin = capture_begin,
      .capture_poll = capture_poll, .framing_begin = framing_begin,
      .framing_poll = framing_poll, .fatal = fatal,
      .latch_fault = latch, .user = m};
  return o;
}
static void setup(mock_t *m, autobaud_od_t *ctx)
{
  *m = (mock_t){0};
  m->capture_begin_ok = true;
  m->framing_begin_ok = true;
  m->probe_status = AUTOBAUD_OD_PROBE_WAITING;
  m->capture_status = AUTOBAUD_OD_CAPTURE_PENDING;
  m->frame_status = AUTOBAUD_OD_FRAME_RUNNING;
  m->initial_profile = (autobaud_v2_profile_t){
      9600U, AUTOBAUD_UART_MODE_8E1};
  m->final_profile = (autobaud_v2_profile_t){
      115200U, AUTOBAUD_UART_MODE_8N1};
  m->measurement = (hw_autobaud_measurement_t){
      .baud_rate = 115200U, .confidence_permille = 950U,
      .sample_count = 12U};
  *ctx = (autobaud_od_t){0};
  const autobaud_od_ops_t callbacks = ops(m);
  assert(autobaud_od_init(ctx, &callbacks));
}
static void enter_capture(mock_t *m, autobaud_od_t *ctx)
{
  m->rx_activity = true;
  autobaud_od_service(ctx);
  assert(ctx->state == AUTOBAUD_OD_CAPTURING);
}
static void enter_framing(mock_t *m, autobaud_od_t *ctx)
{
  enter_capture(m, ctx);
  m->capture_status = AUTOBAUD_OD_CAPTURE_VALID;
  autobaud_od_service(ctx);
  assert(ctx->state == AUTOBAUD_OD_FRAMING);
}

static void test_init_contract(void)
{
  autobaud_od_t ctx = {0};
  mock_t m = {0};
  autobaud_od_ops_t callbacks = ops(&m);
  assert(!autobaud_od_init(NULL, &callbacks));
  assert(!autobaud_od_init(&ctx, NULL));
  callbacks.now_ms = NULL;
  assert(!autobaud_od_init(&ctx, &callbacks));
  callbacks = ops(&m);
  callbacks.capture_poll = NULL;
  assert(!autobaud_od_init(&ctx, &callbacks));
  assert(!autobaud_od_locked(NULL));
  assert(!autobaud_od_locked(&ctx));
  autobaud_od_service(NULL);
  autobaud_od_service(&ctx);
}

static void test_no_master_never_arms(void)
{
  autobaud_od_t ctx;
  mock_t m;
  setup(&m, &ctx);
  for (uint32_t i = 0U; i < 100000U; ++i) {
    m.now += 3600000U; /* Continuous boot, even after wrap. */
    autobaud_od_service(&ctx);
  }
  assert(ctx.state == AUTOBAUD_OD_WAIT_ACTIVITY);
  assert(ctx.attempts == 0U && m.capture_starts == 0U);
  assert(m.fault_latches == 0U && !autobaud_od_locked(&ctx));
}

static void test_first_request_lock_and_invalid_probe(void)
{
  autobaud_od_t ctx;
  mock_t m;
  setup(&m, &ctx);
  m.probe_status = AUTOBAUD_OD_PROBE_LOCKED;
  m.rx_activity = true;
  autobaud_od_service(&ctx);
  assert(autobaud_od_locked(&ctx));
  assert(ctx.active.baud_rate == 9600U);
  assert(ctx.active.framing == AUTOBAUD_UART_MODE_8E1);
  assert(ctx.attempts == 0U && ctx.locks == 1U);
  assert(m.capture_starts == 0U && m.fault_latches == 0U);
  m.fatal = true;
  for (unsigned i = 0U; i < 50U; i++) autobaud_od_service(&ctx);
  assert(autobaud_od_locked(&ctx) && m.fault_latches == 0U);
  assert(m.probes == 1U);

  setup(&m, &ctx);
  m.probe_status = AUTOBAUD_OD_PROBE_LOCKED;
  m.initial_profile.baud_rate = 4800U;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_FAULT);
  assert(m.fault_latches == 1U && ctx.locks == 0U);
  autobaud_od_service(&ctx);
  assert(m.fault_latches == 1U);

  setup(&m, &ctx);
  m.probe_status = AUTOBAUD_OD_PROBE_FAULT;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_FAULT && m.fault_latches == 1U);
}

static void test_capture_failures_and_cooldown_wrap(void)
{
  autobaud_od_t ctx;
  mock_t m;
  setup(&m, &ctx);
  m.now = UINT32_MAX - 500U;
  m.rx_activity = true;
  m.capture_begin_ok = false;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_COOLDOWN && ctx.failures == 1U);
  m.now += 999U;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_COOLDOWN);
  ++m.now;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_WAIT_ACTIVITY);
  m.capture_begin_ok = true;
  enter_capture(&m, &ctx);
  assert(ctx.attempts == 2U);
  autobaud_od_service(&ctx); /* Capture still pending. */
  assert(ctx.state == AUTOBAUD_OD_CAPTURING);
  m.capture_status = AUTOBAUD_OD_CAPTURE_INCONCLUSIVE;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_COOLDOWN && ctx.failures == 2U);

  setup(&m, &ctx);
  enter_capture(&m, &ctx);
  m.capture_status = AUTOBAUD_OD_CAPTURE_FATAL;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_FAULT && m.fault_latches == 1U);
}

static void test_measurement_is_only_evidence(void)
{
  autobaud_od_t ctx;
  mock_t m;
  setup(&m, &ctx);
  enter_capture(&m, &ctx);
  m.capture_status = AUTOBAUD_OD_CAPTURE_VALID;
  m.measurement.confidence_permille = 749U;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_COOLDOWN && m.framing_starts == 0U);
  setup(&m, &ctx);
  enter_capture(&m, &ctx);
  m.capture_status = AUTOBAUD_OD_CAPTURE_VALID;
  m.measurement.sample_count = HW_AUTOBAUD_MIN_VALID_INTERVALS - 1U;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_COOLDOWN && m.framing_starts == 0U);
  setup(&m, &ctx);
  enter_capture(&m, &ctx);
  m.capture_status = AUTOBAUD_OD_CAPTURE_VALID;
  m.measurement.baud_rate = 4800U;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_COOLDOWN && m.framing_starts == 0U);
}

static void test_framing_retries_and_terminal_lock(void)
{
  autobaud_od_t ctx;
  mock_t m;
  setup(&m, &ctx);
  enter_framing(&m, &ctx);
  autobaud_od_service(&ctx); /* Framing not yet complete. */
  assert(ctx.state == AUTOBAUD_OD_FRAMING);
  m.frame_status = AUTOBAUD_OD_FRAME_NO_LOCK;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_COOLDOWN && ctx.failures == 1U);
  assert(ctx.locks == 0U);
  m.now += AUTOBAUD_OD_COOLDOWN_MS;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_WAIT_ACTIVITY);
  m.rx_activity = true;
  autobaud_od_service(&ctx);
  m.capture_status = AUTOBAUD_OD_CAPTURE_VALID;
  autobaud_od_service(&ctx);
  m.frame_status = AUTOBAUD_OD_FRAME_LOCKED;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_LOCKED && ctx.locks == 1U);
  assert(ctx.attempts == 2U && m.fault_latches == 0U);
  assert(ctx.active.baud_rate == 115200U);
  assert(ctx.active.framing == AUTOBAUD_UART_MODE_8N1);
  m.rx_activity = true;
  m.now += UINT32_MAX / 2U;
  autobaud_od_service(&ctx);
  assert(ctx.locks == 1U && m.capture_starts == 2U);

  setup(&m, &ctx);
  enter_framing(&m, &ctx);
  m.frame_status = AUTOBAUD_OD_FRAME_LOCKED;
  m.final_profile.baud_rate = 2400U;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_COOLDOWN && ctx.locks == 0U);
  setup(&m, &ctx);
  enter_framing(&m, &ctx);
  m.frame_status = AUTOBAUD_OD_FRAME_FATAL;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_FAULT && m.fault_latches == 1U);
}

static void test_begin_and_recovery_failures(void)
{
  autobaud_od_t ctx;
  mock_t m;
  setup(&m, &ctx);
  enter_capture(&m, &ctx);
  m.capture_status = AUTOBAUD_OD_CAPTURE_VALID;
  m.framing_begin_ok = false;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_COOLDOWN && m.fault_latches == 0U);

  setup(&m, &ctx);
  m.fatal = true;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_FAULT && m.fault_latches == 1U);

  setup(&m, &ctx);
  enter_capture(&m, &ctx);
  m.capture_status = AUTOBAUD_OD_CAPTURE_VALID;
  m.framing_begin_ok = false;
  /* The board port reports a fault during an attempted frame switch. */
  m.fatal = true;
  autobaud_od_service(&ctx);
  assert(ctx.state == AUTOBAUD_OD_FAULT && m.fault_latches == 1U);
}

int main(void)
{
  /* M3 imports the future transaction/framing type contracts but does not
   * link the physical adapters or the passive framing implementation. */
  assert(AUTOBAUD_E_MAX_RX_RECOVERIES == 2U);
  assert(sizeof(autobaud_e_ops_t) > sizeof(void *));
  assert(AUTOBAUD_OD_CAPTURE_MS == 12000U);
  assert(AUTOBAUD_OD_FRAMING_MS == 52000U);
  assert(AUTOBAUD_OD_FRAMING_DWELL_MS == 12000U);
  test_init_contract();
  test_no_master_never_arms();
  test_first_request_lock_and_invalid_probe();
  test_capture_failures_and_cooldown_wrap();
  test_measurement_is_only_evidence();
  test_framing_retries_and_terminal_lock();
  test_begin_and_recovery_failures();
  puts("Pyxis Autobaud V2 M3 supervisor edge cases: PASS");
  return 0;
}
