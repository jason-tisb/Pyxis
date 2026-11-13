/* Milestone E: host-only side-effect-free framing / Modbus validation.
 * Deliberately links no Modbus request handler, USART or RS485 driver.
 */
#include "autobaud_v2_framing.h"
#include "modbus_crc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef enum {
  MOCK_NO_FRAME = 0,
  MOCK_CORRECT,
  MOCK_BAD_CRC,
  MOCK_INVALID_FUNCTION,
  MOCK_INVALID_EVIDENCE,
  MOCK_STALE_SESSION,
  MOCK_WRONG_ADDRESS,
  MOCK_BROADCAST,
  MOCK_RX_FAULT
} mock_frame_kind_t;

typedef struct {
  uint32_t now;
  uint32_t session;
  uint32_t next_frame;
  uint32_t error_gen;
  uint32_t applied;
  uint32_t restored;
  uint32_t accepted;
  uint32_t fatal_calls;
  uint32_t consumed;
  uint32_t validator_side_effects;
  bool fatal;
  bool passive;
  bool apply_fails_once;
  bool apply_busy_once;
  bool apply_fatal;
  bool accept_fails_once;
  bool verify_fails_once;
  autobaud_v2_profile_t current;
  autobaud_uart_mode_t requested_mode;
  mock_frame_kind_t kind;
  bool emit_once;
  bool emitted;
} mock_t;

static const autobaud_v2_profile_t k_previous = {
  19200U, AUTOBAUD_UART_MODE_8E1
};
static const hw_autobaud_measurement_t k_measured = {
  115200U, 960U, 20U
};

static uint32_t now_ms(void *u)
{
  return ((mock_t *)u)->now;
}

static bool fatal_check(void *u)
{
  return ((mock_t *)u)->fatal;
}

static bool snapshot(void *u, autobaud_v2_profile_t *p, bool *passive)
{
  mock_t *m = u;
  *p = m->current;
  *passive = m->passive;
  return !m->fatal;
}

static autobaud_uart_txn_result_t apply(void *u,
                                         const autobaud_v2_profile_t *p,
                                         bool passive)
{
  mock_t *m = u;
  if (m->apply_fatal) {
    m->fatal = true;
    return AUTOBAUD_UART_TXN_FATAL_RECOVERY;
  }
  if (m->apply_busy_once) {
    m->apply_busy_once = false;
    return AUTOBAUD_UART_TXN_BUSY;
  }
  if (m->apply_fails_once) {
    m->apply_fails_once = false;
    return AUTOBAUD_UART_TXN_ROLLED_BACK_VERIFIED;
  }
  m->current = *p;
  m->passive = passive;
  m->emitted = false;
  ++m->session;
  ++m->applied;
  if (p->baud_rate == k_previous.baud_rate && !passive)
    ++m->restored;
  return AUTOBAUD_UART_TXN_APPLIED_VERIFIED;
}

static bool get_session(void *u, uint32_t *session)
{
  mock_t *m = u;
  if (!m->passive || m->fatal) return false;
  *session = m->session;
  return true;
}

static uint16_t make_request(uint8_t *out, mock_frame_kind_t kind)
{
  uint16_t crc;
  out[0] = kind == MOCK_WRONG_ADDRESS ? 247U :
           kind == MOCK_BROADCAST ? 0U : 17U;
  out[1] = kind == MOCK_BROADCAST ? 0x06U : 0x03U;
  if (kind == MOCK_INVALID_FUNCTION) out[1] = 0x17U;
  out[2] = 0U;
  out[3] = 12U;
  out[4] = 0U;
  out[5] = 2U;
  crc = modbus_crc16(out, 6U);
  out[6] = (uint8_t)crc;
  out[7] = (uint8_t)(crc >> 8U);
  if (kind == MOCK_BAD_CRC) out[7] ^= 1U;
  return 8U;
}

static autobaud_e_poll_t take(void *u, uint8_t *out, uint16_t capacity,
                              uint16_t *len, autobaud_e_frame_meta_t *meta)
{
  mock_t *m = u;
  mock_frame_kind_t kind = m->kind;
  *len = 0U;
  *meta = (autobaud_e_frame_meta_t){0};
  if (!m->passive || m->fatal) return AUTOBAUD_E_POLL_RX_FAULT;
  if (m->current.framing != m->requested_mode ||
      kind == MOCK_NO_FRAME || (m->emit_once && m->emitted))
    return AUTOBAUD_E_POLL_NONE;
  m->emitted = true;
  ++m->consumed;
  if (kind == MOCK_RX_FAULT) return AUTOBAUD_E_POLL_RX_FAULT;
  assert(capacity >= 8U);
  *len = make_request(out, kind);
  ++m->next_frame;
  meta->session_id = m->session;
  meta->frame_id = m->next_frame;
  meta->uart_error_generation = m->error_gen;
  meta->evidence = (autobaud_validate_rx_evidence_t){
    true, true, true, true
  };
  if (kind == MOCK_INVALID_EVIDENCE)
    meta->evidence.uart_error_free = false;
  if (kind == MOCK_STALE_SESSION)
    meta->session_id = m->session - 1U;
  return AUTOBAUD_E_POLL_FRAME;
}

static bool verify(void *u, const autobaud_v2_profile_t *p,
                   uint32_t session, uint32_t error_generation)
{
  mock_t *m = u;
  if (m->verify_fails_once) {
    m->verify_fails_once = false;
    return false;
  }
  return !m->fatal && m->passive &&
         m->current.framing == p->framing &&
         m->current.baud_rate == p->baud_rate &&
         session == m->session && error_generation == m->error_gen;
}

static bool accept(void *u, const autobaud_v2_profile_t *p,
                   const autobaud_e_frame_meta_t *meta)
{
  mock_t *m = u;
  if (m->accept_fails_once) {
    m->accept_fails_once = false;
    return false;
  }
  if (!m->passive || meta->session_id != m->session ||
      meta->frame_id != m->next_frame ||
      m->current.framing != p->framing ||
      m->current.baud_rate != p->baud_rate) return false;
  ++m->accepted;
  m->passive = false;
  return true;
}

static void latch_fatal(void *u)
{
  mock_t *m = u;
  m->fatal = true;
  ++m->fatal_calls;
}

static const autobaud_e_ops_t base_ops = {
  .now_ms = now_ms, .fatal = fatal_check,
  .snapshot = snapshot, .apply = apply,
  .get_session = get_session, .take_frame = take,
  .verify = verify, .accept = accept,
  .latch_fatal = latch_fatal, .user = NULL
};

static void setup(mock_t *m, autobaud_e_ctx_t *ctx,
                  mock_frame_kind_t kind, autobaud_uart_mode_t requested)
{
  *m = (mock_t){0};
  *ctx = (autobaud_e_ctx_t){0};
  m->current = k_previous;
  m->requested_mode = requested;
  m->session = 20U;
  m->kind = kind;
}

static bool start(mock_t *m, autobaud_e_ctx_t *ctx, uint32_t budget)
{
  autobaud_e_ops_t ops = base_ops;
  ops.user = m;
  return autobaud_e_start(ctx, &ops, &k_measured,
                           HW_AUTOBAUD_MEASURE_VALID, budget);
}

static autobaud_e_status_t drive(mock_t *m, autobaud_e_ctx_t *ctx)
{
  for (uint32_t i = 0U; i < 200U; ++i) {
    autobaud_e_status_t status = autobaud_e_service(ctx);
    if (status != AUTOBAUD_E_RUNNING) return status;
    if (ctx->state == AUTOBAUD_E_STATE_LISTEN &&
        (m->kind == MOCK_NO_FRAME ||
         m->current.framing != m->requested_mode ||
         (m->emit_once && m->emitted)))
      m->now += 2001U;
  }
  assert(!"framing state machine stuck");
  return AUTOBAUD_E_FATAL_RECOVERY;
}

static void test_all_modes_all_bauds(void)
{
  static const uint32_t bauds[] = {
    9600U, 19200U, 38400U, 57600U, 115200U
  };
  static const autobaud_uart_mode_t modes[] = {
    AUTOBAUD_UART_MODE_8E1, AUTOBAUD_UART_MODE_8N2,
    AUTOBAUD_UART_MODE_8O1, AUTOBAUD_UART_MODE_8N1
  };
  for (unsigned b = 0U; b < 5U; b++) {
    for (unsigned p = 0U; p < 4U; p++) {
      autobaud_e_ctx_t ctx = {0};
      mock_t m = {0};
      autobaud_e_ops_t ops = base_ops;
      hw_autobaud_measurement_t measurement = k_measured;
      setup(&m, &ctx, MOCK_CORRECT, modes[p]);
      ops.user = &m;
      measurement.baud_rate = bauds[b];
      assert(autobaud_e_start(&ctx, &ops, &measurement,
                              HW_AUTOBAUD_MEASURE_VALID, 12000U));
      assert(drive(&m, &ctx) == AUTOBAUD_E_LOCKED);
      assert(ctx.active.baud_rate == bauds[b]);
      assert(ctx.active.framing == modes[p]);
      assert(m.accepted == 1U && m.consumed == 1U);
      assert(m.validator_side_effects == 0U);
      assert(ctx.observed_slave_id == 17U);
      assert(ctx.observed_function_code == 0x03U);
      assert(!ctx.stop_bits_independently_verified);
      assert(autobaud_e_service(&ctx) == AUTOBAUD_E_LOCKED);
      assert(m.accepted == 1U);
    }
  }
}

static void test_any_id_broadcast_and_rejection(void)
{
  static const mock_frame_kind_t valid[] = {
    MOCK_CORRECT, MOCK_WRONG_ADDRESS, MOCK_BROADCAST
  };
  for (unsigned k = 0U; k < sizeof(valid) / sizeof(valid[0]); ++k) {
    mock_t m;
    autobaud_e_ctx_t ctx;
    setup(&m, &ctx, valid[k], AUTOBAUD_UART_MODE_8E1);
    assert(start(&m, &ctx, 12000U));
    assert(drive(&m, &ctx) == AUTOBAUD_E_LOCKED);
    assert(ctx.observed_slave_id == (valid[k] == MOCK_WRONG_ADDRESS
              ? 247U : valid[k] == MOCK_BROADCAST ? 0U : 17U));
    assert(m.accepted == 1U && m.validator_side_effects == 0U);
  }
  {
    static const mock_frame_kind_t invalid[] = {
      MOCK_BAD_CRC, MOCK_INVALID_FUNCTION,
      MOCK_INVALID_EVIDENCE, MOCK_STALE_SESSION
    };
    for (unsigned k = 0U; k < sizeof(invalid) / sizeof(invalid[0]); ++k) {
      mock_t m;
      autobaud_e_ctx_t ctx;
      setup(&m, &ctx, invalid[k], AUTOBAUD_UART_MODE_8E1);
      m.emit_once = true;
      assert(start(&m, &ctx, 12000U));
      assert(drive(&m, &ctx) == AUTOBAUD_E_NO_VALID_REQUEST);
      assert(m.accepted == 0U && m.restored == 1U);
      assert(ctx.diag.frames_rejected +
             ctx.diag.invalid_session_frames >= 1U);
    }
  }
}

static void test_input_failures(void)
{
  autobaud_e_ctx_t ctx = {0};
  mock_t m = {0};
  assert(autobaud_e_service(&ctx) == AUTOBAUD_E_INVALID_MEASUREMENT);
  autobaud_e_ops_t ops = base_ops;
  hw_autobaud_measurement_t measurement = k_measured;
  setup(&m, &ctx, MOCK_NO_FRAME, AUTOBAUD_UART_MODE_8E1);
  ops.user = &m;
  measurement.confidence_permille = 749U;
  assert(!autobaud_e_start(&ctx, &ops, &measurement,
                           HW_AUTOBAUD_MEASURE_VALID, 12000U));
  assert(m.applied == 0U);
  measurement = k_measured;
  measurement.baud_rate = 4800U;
  assert(!autobaud_e_start(&ctx, &ops, &measurement,
                           HW_AUTOBAUD_MEASURE_VALID, 12000U));
  measurement = k_measured;
  assert(!autobaud_e_start(&ctx, &ops, &measurement,
                           HW_AUTOBAUD_MEASURE_TIMEOUT, 12000U));
  assert(!autobaud_e_start(&ctx, &ops, &measurement,
                           HW_AUTOBAUD_MEASURE_VALID, 0U));
  assert(m.applied == 0U);
}

static void test_recovery_and_deadline(void)
{
  mock_t m;
  autobaud_e_ctx_t ctx;
  setup(&m, &ctx, MOCK_RX_FAULT, AUTOBAUD_UART_MODE_8E1);
  assert(start(&m, &ctx, 12000U));
  assert(drive(&m, &ctx) == AUTOBAUD_E_NO_VALID_REQUEST);
  assert(ctx.diag.rx_faults >= 3U);
  assert(ctx.diag.recovery_attempts == 2U);
  assert(m.accepted == 0U && m.restored == 1U);

  setup(&m, &ctx, MOCK_NO_FRAME, AUTOBAUD_UART_MODE_8O1);
  assert(start(&m, &ctx, 3000U));
  assert(drive(&m, &ctx) == AUTOBAUD_E_BUDGET_EXPIRED);
  assert(m.restored == 1U && m.accepted == 0U);
}

static void test_lock_race_recovery(void)
{
  mock_t m;
  autobaud_e_ctx_t ctx;
  setup(&m, &ctx, MOCK_CORRECT, AUTOBAUD_UART_MODE_8E1);
  m.verify_fails_once = true;
  assert(start(&m, &ctx, 12000U));
  assert(drive(&m, &ctx) == AUTOBAUD_E_LOCKED);
  assert(ctx.diag.lock_handoff_failures == 1U);
  assert(ctx.diag.recovery_attempts == 1U);
  assert(m.accepted == 1U && m.consumed == 2U);
}

static void test_fault_latch_and_cancel(void)
{
  mock_t m;
  autobaud_e_ctx_t ctx;
  setup(&m, &ctx, MOCK_NO_FRAME, AUTOBAUD_UART_MODE_8E1);
  assert(start(&m, &ctx, 12000U));
  assert(autobaud_e_cancel(&ctx) == AUTOBAUD_E_NO_VALID_REQUEST);
  assert(m.restored == 1U);
  setup(&m, &ctx, MOCK_NO_FRAME, AUTOBAUD_UART_MODE_8E1);
  m.apply_fatal = true;
  assert(start(&m, &ctx, 12000U));
  assert(autobaud_e_service(&ctx) == AUTOBAUD_E_FATAL_RECOVERY);
  assert(m.fatal && m.fatal_calls == 1U);
  assert(autobaud_e_service(&ctx) == AUTOBAUD_E_FATAL_RECOVERY);
}

static uint32_t rng = UINT32_C(0xE2026A11);
static uint32_t rand32(void)
{
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

static void test_random_untrusted_never_locks(void)
{
  for (uint32_t i = 0U; i < 2000U; ++i) {
    static const mock_frame_kind_t bad[] = {
      MOCK_BAD_CRC, MOCK_INVALID_FUNCTION,
      MOCK_INVALID_EVIDENCE, MOCK_STALE_SESSION,
      MOCK_RX_FAULT, MOCK_NO_FRAME
    };
    mock_t m;
    autobaud_e_ctx_t ctx;
    setup(&m, &ctx, bad[rand32() % 6U], AUTOBAUD_UART_MODE_8E1);
    m.emit_once = true;
    assert(start(&m, &ctx, 10000U));
    assert(drive(&m, &ctx) != AUTOBAUD_E_LOCKED);
    assert(m.accepted == 0U && m.validator_side_effects == 0U);
    assert(!m.fatal);
  }
}

static void test_first_candidate_busy(void)
{
  mock_t m;
  autobaud_e_ctx_t ctx;
  setup(&m, &ctx, MOCK_NO_FRAME, AUTOBAUD_UART_MODE_8N2);
  m.apply_busy_once = true;
  assert(start(&m, &ctx, 12000U));
  assert(autobaud_e_service(&ctx) == AUTOBAUD_E_BUSY);
  assert(m.applied == 0U && !m.fatal);
}

int main(void)
{
  test_all_modes_all_bauds();
  test_any_id_broadcast_and_rejection();
  test_input_failures();
  test_recovery_and_deadline();
  test_lock_race_recovery();
  test_fault_latch_and_cancel();
  test_first_candidate_busy();
  test_random_untrusted_never_locks();
  puts("autobaud_v2_framing_host_tests: PASS");
  return 0;
}
