#include "autobaud_uart_transaction.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef enum {
  FAIL_NONE, FAIL_CLAIM, FAIL_SNAPSHOT, FAIL_QUIESCE, FAIL_APPLY,
  FAIL_TIMING, FAIL_RESTART, FAIL_VERIFY, FAIL_CAPTURE, FAIL_CAPTURE_RELEASE
} fault_stage_t;

typedef struct {
  autobaud_v2_profile_t current;
  autobaud_v2_profile_t timing;
  fault_stage_t inject;
  unsigned faults_remaining;
  bool claimed;
  bool quiesced;
  bool rx_ready;
  bool passive;
  bool prior_passive;
  bool fatal;
  bool tx_blocked;
  bool old_frame_pending;
  uint32_t generation;
  uint32_t stale_frames_discarded;
  unsigned capture_calls;
  hw_autobaud_measure_status_t capture_outcome;
} mock_t;

static bool same(const autobaud_v2_profile_t *a, const autobaud_v2_profile_t *b)
{
  return a->baud_rate == b->baud_rate && a->framing == b->framing;
}
static bool inject(mock_t *m, fault_stage_t stage)
{
  if (m->inject != stage || m->faults_remaining == 0U) return false;
  --m->faults_remaining;
  return true;
}
static bool claim(void *ctx)
{
  mock_t *m = ctx;
  if (inject(m, FAIL_CLAIM) || m->claimed || m->fatal) return false;
  m->prior_passive = m->passive;
  m->claimed = true;
  m->tx_blocked = true;
  return true;
}
static bool snapshot(void *ctx, autobaud_v2_profile_t *previous)
{
  mock_t *m = ctx;
  if (!m->claimed || inject(m, FAIL_SNAPSHOT)) return false;
  *previous = m->current;
  return true;
}
static bool previous_passive(void *ctx)
{
  return ((mock_t *)ctx)->prior_passive;
}
static bool quiesce(void *ctx)
{
  mock_t *m = ctx;
  if (!m->claimed) return false;
  m->quiesced = true;
  m->rx_ready = false;
  m->passive = false;
  if (m->old_frame_pending) {
    m->stale_frames_discarded++;
    m->old_frame_pending = false;
  }
  return !inject(m, FAIL_QUIESCE);
}
static bool apply(void *ctx, const autobaud_v2_profile_t *profile)
{
  mock_t *m = ctx;
  if (!m->quiesced) return false;
  m->current = *profile; /* Simulate partially applied config on failure. */
  return !inject(m, FAIL_APPLY);
}
static bool timing(void *ctx, const autobaud_v2_profile_t *profile)
{
  mock_t *m = ctx;
  if (!m->quiesced) return false;
  m->timing = *profile;
  return !inject(m, FAIL_TIMING);
}
static bool restart(void *ctx, bool passive)
{
  mock_t *m = ctx;
  if (!m->quiesced) return false;
  m->generation++;
  m->rx_ready = true;
  m->passive = passive;
  m->quiesced = false;
  return !inject(m, FAIL_RESTART);
}
static bool verify(void *ctx, const autobaud_v2_profile_t *profile, bool passive)
{
  mock_t *m = ctx;
  if (inject(m, FAIL_VERIFY)) return false;
  return m->claimed && m->rx_ready && !m->quiesced &&
         m->passive == passive &&
         same(&m->current, profile) && same(&m->timing, profile) &&
         !m->old_frame_pending;
}
static bool capture(void *ctx, uint32_t timeout_ms,
                    hw_autobaud_measurement_t *out,
                    hw_autobaud_measure_status_t *status)
{
  mock_t *m = ctx;
  if (!m->quiesced || timeout_ms == 0U) return false;
  m->capture_calls++;
  if (inject(m, FAIL_CAPTURE)) return false;
  *status = m->capture_outcome;
  out->baud_rate = 115200U;
  out->confidence_permille = 950U;
  out->sample_count = 15U;
  return true;
}
static bool capture_released(void *ctx)
{
  return !inject((mock_t *)ctx, FAIL_CAPTURE_RELEASE);
}
static void release_gate(void *ctx, bool fatal, bool passive)
{
  mock_t *m = ctx;
  m->fatal = fatal;
  m->claimed = false;
  m->tx_blocked = fatal || passive;
  m->passive = !fatal && passive;
}

static const autobaud_uart_txn_ops_t ops = {
    .claim = claim, .snapshot = snapshot,
    .previous_passive = previous_passive, .quiesce = quiesce,
    .apply = apply, .timing = timing, .restart = restart,
    .verify = verify, .capture = capture,
    .capture_released = capture_released, .release = release_gate
};

static const autobaud_v2_profile_t base = {19200U, AUTOBAUD_UART_MODE_8E1};
static const autobaud_v2_profile_t next = {115200U, AUTOBAUD_UART_MODE_8N2};

static void setup(mock_t *m, autobaud_uart_txn_t *txn)
{
  *m = (mock_t){0};
  m->current = base;
  m->timing = base;
  m->rx_ready = true;
  m->old_frame_pending = true;
  m->capture_outcome = HW_AUTOBAUD_MEASURE_VALID;
  assert(autobaud_uart_txn_init(txn, &ops, m));
}

static void test_all_20_profiles(void)
{
  static const uint32_t rates[] = {9600U, 19200U, 38400U, 57600U, 115200U};
  mock_t m;
  autobaud_uart_txn_t txn;
  setup(&m, &txn);
  for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++) {
    for (uint32_t j = 0U; j < 4U; j++) {
      autobaud_v2_profile_t candidate = {rates[i], (autobaud_uart_mode_t)j};
      assert(autobaud_uart_txn_apply(&txn, &candidate, true) ==
             AUTOBAUD_UART_TXN_APPLIED_VERIFIED);
      assert(m.rx_ready && m.passive && m.tx_blocked);
      assert(same(&m.current, &candidate));
      assert(m.stale_frames_discarded == 1U);
      assert(txn.phase == AUTOBAUD_UART_TXN_PHASE_IDLE);
    }
  }
  assert(txn.diag.successes == 20U);
  assert(!txn.fatal_latched);
}

static void test_invalid_busy_and_snapshot(void)
{
  mock_t m;
  autobaud_uart_txn_t txn;
  autobaud_v2_profile_t invalid = {4800U, AUTOBAUD_UART_MODE_8E1};
  setup(&m, &txn);
  assert(autobaud_uart_txn_apply(&txn, &invalid, false) ==
         AUTOBAUD_UART_TXN_INVALID_PROFILE);
  assert(m.old_frame_pending && !m.claimed);
  txn.in_progress = true;
  assert(autobaud_uart_txn_apply(&txn, &next, false) ==
         AUTOBAUD_UART_TXN_BUSY);
  txn.in_progress = false;
  m.inject = FAIL_CLAIM;
  m.faults_remaining = 1U;
  assert(autobaud_uart_txn_apply(&txn, &next, false) ==
         AUTOBAUD_UART_TXN_BUSY);
  assert(!m.claimed && m.old_frame_pending);
  m.inject = FAIL_SNAPSHOT;
  m.faults_remaining = 1U;
  assert(autobaud_uart_txn_apply(&txn, &next, false) ==
         AUTOBAUD_UART_TXN_FATAL_RECOVERY);
  assert(txn.fatal_latched && m.fatal && m.rx_ready);
  assert(autobaud_uart_txn_apply(&txn, &next, false) ==
         AUTOBAUD_UART_TXN_FATAL_RECOVERY);
}

static void test_single_failures_rollback(void)
{
  for (fault_stage_t stage = FAIL_QUIESCE; stage <= FAIL_VERIFY; stage++) {
    mock_t m;
    autobaud_uart_txn_t txn;
    setup(&m, &txn);
    m.inject = stage;
    m.faults_remaining = 1U;
    assert(autobaud_uart_txn_apply(&txn, &next, false) ==
           AUTOBAUD_UART_TXN_ROLLED_BACK_VERIFIED);
    assert(m.rx_ready && !m.passive && !m.tx_blocked && !m.fatal);
    assert(same(&m.current, &base) && same(&m.timing, &base));
    assert(!m.old_frame_pending && m.stale_frames_discarded == 1U);
    assert(txn.diag.rollbacks == 1U);
    assert(txn.diag.rollback_successes == 1U);
    assert(!txn.fatal_latched);
    assert(m.generation >= 1U);
    m.inject = FAIL_NONE;
    assert(autobaud_uart_txn_apply(&txn, &next, false) ==
           AUTOBAUD_UART_TXN_APPLIED_VERIFIED);
  }
}

static void test_passive_rollback_stays_passive(void)
{
  mock_t m;
  autobaud_uart_txn_t txn;
  setup(&m, &txn);
  assert(autobaud_uart_txn_apply(&txn, &next, true) ==
         AUTOBAUD_UART_TXN_APPLIED_VERIFIED);
  m.inject = FAIL_VERIFY;
  m.faults_remaining = 1U;
  assert(autobaud_uart_txn_apply(&txn, &base, true) ==
         AUTOBAUD_UART_TXN_ROLLED_BACK_VERIFIED);
  assert(m.passive && m.tx_blocked && m.rx_ready);
  assert(same(&m.current, &next));
}

static void test_rollback_fatal(void)
{
  mock_t m;
  autobaud_uart_txn_t txn;
  setup(&m, &txn);
  m.inject = FAIL_QUIESCE;
  m.faults_remaining = 2U; /* Quiescence and rollback both fail. */
  assert(autobaud_uart_txn_apply(&txn, &next, false) ==
         AUTOBAUD_UART_TXN_FATAL_RECOVERY);
  assert(txn.fatal_latched && m.fatal && m.tx_blocked);
  assert(txn.diag.rollback_failures == 1U);
  assert(autobaud_uart_txn_apply(&txn, &base, false) ==
         AUTOBAUD_UART_TXN_FATAL_RECOVERY);
}

static void test_capture_restore(void)
{
  mock_t m;
  autobaud_uart_txn_t txn;
  hw_autobaud_measurement_t out;
  hw_autobaud_measure_status_t status;
  setup(&m, &txn);
  assert(autobaud_uart_txn_capture(&txn, 1500U, &out, &status) ==
         AUTOBAUD_UART_TXN_CAPTURE_RESTORED);
  assert(status == HW_AUTOBAUD_MEASURE_VALID &&
         out.baud_rate == 115200U);
  assert(m.capture_calls == 1U && m.rx_ready && !m.passive &&
         same(&m.current, &base));
  m.capture_outcome = HW_AUTOBAUD_MEASURE_AMBIGUOUS;
  assert(autobaud_uart_txn_capture(&txn, 1500U, &out, &status) ==
         AUTOBAUD_UART_TXN_CAPTURE_RESTORED);
  assert(status == HW_AUTOBAUD_MEASURE_AMBIGUOUS &&
         out.baud_rate == 0U);
  m.inject = FAIL_CAPTURE_RELEASE;
  m.faults_remaining = 1U;
  assert(autobaud_uart_txn_capture(&txn, 1500U, &out, &status) ==
         AUTOBAUD_UART_TXN_FATAL_RECOVERY);
  assert(txn.fatal_latched && m.tx_blocked);
}


static uint32_t s_seed = UINT32_C(0xC071C0DE);
static uint32_t random32(void)
{
  s_seed ^= s_seed << 13;
  s_seed ^= s_seed >> 17;
  s_seed ^= s_seed << 5;
  return s_seed;
}

static void test_randomized_rollback_invariants(void)
{
  static const uint32_t rates[5] = {
    9600U, 19200U, 38400U, 57600U, 115200U
  };
  for (uint32_t i = 0U; i < 10000U; i++) {
    mock_t m;
    autobaud_uart_txn_t txn;
    uint32_t seed = random32();
    autobaud_v2_profile_t candidate = {
      rates[(seed >> 4) % 5U],
      (autobaud_uart_mode_t)((seed >> 9) % 4U)
    };
    fault_stage_t stage = (fault_stage_t)(seed % 8U);
    bool was_passive = ((seed >> 13) & 1U) != 0U;
    autobaud_uart_txn_result_t result;
    setup(&m, &txn);
    m.passive = was_passive;
    m.inject = stage;
    m.faults_remaining = stage == FAIL_NONE ? 0U : 1U;

    result = autobaud_uart_txn_apply(&txn, &candidate, true);
    if (stage == FAIL_NONE) {
      assert(result == AUTOBAUD_UART_TXN_APPLIED_VERIFIED);
      assert(same(&m.current, &candidate));
      assert(m.passive && m.rx_ready && m.tx_blocked);
    } else if (stage == FAIL_CLAIM) {
      assert(result == AUTOBAUD_UART_TXN_BUSY);
      assert(!m.claimed && same(&m.current, &base));
    } else if (stage == FAIL_SNAPSHOT) {
      assert(result == AUTOBAUD_UART_TXN_FATAL_RECOVERY);
      assert(txn.fatal_latched && m.tx_blocked);
    } else {
      assert(result == AUTOBAUD_UART_TXN_ROLLED_BACK_VERIFIED);
      assert(same(&m.current, &base) && same(&m.timing, &base));
      assert(m.rx_ready && m.passive == was_passive);
      assert(m.tx_blocked == was_passive);
    }
    assert(!txn.in_progress);
    assert(!m.claimed);
    if (result == AUTOBAUD_UART_TXN_FATAL_RECOVERY)
      assert(txn.phase == AUTOBAUD_UART_TXN_PHASE_FATAL);
  }
}

static void test_capture_recoverable_fault(void)
{
  mock_t m;
  autobaud_uart_txn_t txn;
  hw_autobaud_measurement_t out = {0};
  hw_autobaud_measure_status_t status = HW_AUTOBAUD_MEASURE_VALID;
  setup(&m, &txn);
  m.capture_outcome = HW_AUTOBAUD_MEASURE_ERROR;
  assert(autobaud_uart_txn_capture(&txn, 1500U, &out, &status) ==
         AUTOBAUD_UART_TXN_CAPTURE_RESTORED);
  assert(status == HW_AUTOBAUD_MEASURE_ERROR && out.baud_rate == 0U);
  assert(m.rx_ready && !m.fatal && same(&m.current, &base));
  m.inject = FAIL_CAPTURE;
  m.faults_remaining = 1U;
  assert(autobaud_uart_txn_capture(&txn, 1500U, &out, &status) ==
         AUTOBAUD_UART_TXN_FATAL_RECOVERY);
  assert(m.fatal && txn.fatal_latched);
}

int main(void)
{
  test_all_20_profiles();
  test_invalid_busy_and_snapshot();
  test_single_failures_rollback();
  test_passive_rollback_stays_passive();
  test_rollback_fatal();
  test_capture_restore();
  test_capture_recoverable_fault();
  test_randomized_rollback_invariants();
  puts("autobaud_uart_transaction_host_tests: PASS");
  return 0;
}
