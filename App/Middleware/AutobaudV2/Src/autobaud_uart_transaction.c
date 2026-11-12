#include "autobaud_uart_transaction.h"
#include <stddef.h>
#include <string.h>

static void sat_increment(uint32_t *value)
{
  if (*value != UINT32_MAX) ++*value;
}

static bool ops_valid(const autobaud_uart_txn_ops_t *ops)
{
  return (ops != NULL) && (ops->claim != NULL) &&
         (ops->snapshot != NULL) && (ops->previous_passive != NULL) &&
         (ops->quiesce != NULL) &&
         (ops->apply != NULL) && (ops->timing != NULL) &&
         (ops->restart != NULL) && (ops->verify != NULL) &&
         (ops->release != NULL);
}

bool autobaud_uart_txn_init(autobaud_uart_txn_t *txn,
                           const autobaud_uart_txn_ops_t *ops,
                           void *ctx)
{
  if ((txn == NULL) || !ops_valid(ops)) return false;
  *txn = (autobaud_uart_txn_t){0};
  txn->ops = *ops;
  txn->ctx = ctx;
  return true;
}

static autobaud_uart_txn_result_t finish(
    autobaud_uart_txn_t *txn, autobaud_uart_txn_result_t outcome,
    bool fatal, bool passive)
{
  if (fatal) {
    txn->phase = AUTOBAUD_UART_TXN_PHASE_FATAL;
    txn->fatal_latched = true;
    sat_increment(&txn->diag.fatal_count);
  } else {
    txn->phase = AUTOBAUD_UART_TXN_PHASE_IDLE;
  }
  txn->ops.release(txn->ctx, fatal, !fatal && passive);
  txn->in_progress = false;
  return outcome;
}

static bool rollback(autobaud_uart_txn_t *txn,
                     const autobaud_v2_profile_t *previous,
                     bool previous_passive)
{
  /* After a destructive failure, always attempt a fresh quiescence
   * before changing UART registers, and fully verify the old receive path.
   * Continue every recovery stage only when the prior one succeeded.
   */
  sat_increment(&txn->diag.rollbacks);
  txn->phase = AUTOBAUD_UART_TXN_PHASE_ROLLBACK;
  if (!txn->ops.quiesce(txn->ctx) ||
      !txn->ops.apply(txn->ctx, previous) ||
      !txn->ops.timing(txn->ctx, previous) ||
      !txn->ops.restart(txn->ctx, previous_passive) ||
      !txn->ops.verify(txn->ctx, previous, previous_passive)) {
    sat_increment(&txn->diag.rollback_failures);
    return false;
  }
  sat_increment(&txn->diag.rollback_successes);
  return true;
}

static autobaud_uart_txn_result_t fail_after_claim(
    autobaud_uart_txn_t *txn, bool have_previous,
    const autobaud_v2_profile_t *previous, bool previous_passive)
{
  if (!have_previous || !rollback(txn, previous, previous_passive))
    return finish(txn, AUTOBAUD_UART_TXN_FATAL_RECOVERY, true, false);
  return finish(txn, AUTOBAUD_UART_TXN_ROLLED_BACK_VERIFIED,
                false, previous_passive);
}

autobaud_uart_txn_result_t autobaud_uart_txn_apply(
    autobaud_uart_txn_t *txn, const autobaud_v2_profile_t *requested,
    bool passive)
{
  autobaud_v2_profile_t previous = {0};
  bool previous_passive = false;

  if (txn == NULL || !ops_valid(&txn->ops))
    return AUTOBAUD_UART_TXN_FATAL_RECOVERY;
  if (txn->fatal_latched) return AUTOBAUD_UART_TXN_FATAL_RECOVERY;
  if (txn->in_progress) return AUTOBAUD_UART_TXN_BUSY;
  if (!autobaud_v2_profile_supported(requested))
    return AUTOBAUD_UART_TXN_INVALID_PROFILE;

  txn->in_progress = true;
  txn->failure_phase = AUTOBAUD_UART_TXN_PHASE_IDLE;
  sat_increment(&txn->diag.attempts);
  txn->phase = AUTOBAUD_UART_TXN_PHASE_CLAIM;
  if (!txn->ops.claim(txn->ctx)) {
    txn->phase = AUTOBAUD_UART_TXN_PHASE_IDLE;
    txn->in_progress = false;
    sat_increment(&txn->diag.claim_failures);
    return AUTOBAUD_UART_TXN_BUSY;
  }
  txn->phase = AUTOBAUD_UART_TXN_PHASE_SNAPSHOT;
  previous_passive = txn->ops.previous_passive(txn->ctx);
  if (!txn->ops.snapshot(txn->ctx, &previous) ||
      !autobaud_v2_profile_supported(&previous)) {
    txn->failure_phase = txn->phase;
    /* No destructive action occurred; release the gate without touching
     * any UART, DMA or Modbus configuration.
     */
    return finish(txn, AUTOBAUD_UART_TXN_FATAL_RECOVERY, true, false);
  }

  txn->phase = AUTOBAUD_UART_TXN_PHASE_QUIESCE;
  if (!txn->ops.quiesce(txn->ctx)) {
    sat_increment(&txn->diag.quiesce_failures);
    txn->failure_phase = txn->phase;
    return fail_after_claim(txn, true, &previous, previous_passive);
  }
  txn->phase = AUTOBAUD_UART_TXN_PHASE_APPLY;
  if (!txn->ops.apply(txn->ctx, requested)) {
    sat_increment(&txn->diag.apply_failures);
    txn->failure_phase = txn->phase;
    return fail_after_claim(txn, true, &previous, previous_passive);
  }
  txn->phase = AUTOBAUD_UART_TXN_PHASE_TIMING;
  if (!txn->ops.timing(txn->ctx, requested)) {
    sat_increment(&txn->diag.timing_failures);
    txn->failure_phase = txn->phase;
    return fail_after_claim(txn, true, &previous, previous_passive);
  }
  txn->phase = AUTOBAUD_UART_TXN_PHASE_RESTART;
  if (!txn->ops.restart(txn->ctx, passive)) {
    sat_increment(&txn->diag.restart_failures);
    txn->failure_phase = txn->phase;
    return fail_after_claim(txn, true, &previous, previous_passive);
  }
  txn->phase = AUTOBAUD_UART_TXN_PHASE_VERIFY;
  if (!txn->ops.verify(txn->ctx, requested, passive)) {
    sat_increment(&txn->diag.verify_failures);
    txn->failure_phase = txn->phase;
    return fail_after_claim(txn, true, &previous, previous_passive);
  }

  sat_increment(&txn->diag.successes);
  return finish(txn, AUTOBAUD_UART_TXN_APPLIED_VERIFIED, false, passive);
}

autobaud_uart_txn_result_t autobaud_uart_txn_capture(
    autobaud_uart_txn_t *txn, uint32_t timeout_ms,
    hw_autobaud_measurement_t *measurement,
    hw_autobaud_measure_status_t *capture_status)
{
  autobaud_v2_profile_t previous = {0};
  bool previous_passive = false;
  hw_autobaud_measurement_t captured = {0};
  hw_autobaud_measure_status_t measured = HW_AUTOBAUD_MEASURE_ERROR;
  bool safe;

  if (measurement != NULL) *measurement = (hw_autobaud_measurement_t){0};
  if (capture_status != NULL) *capture_status = HW_AUTOBAUD_MEASURE_ERROR;
  if ((txn == NULL) || (measurement == NULL) || (capture_status == NULL) ||
      !ops_valid(&txn->ops) || (txn->ops.capture == NULL) ||
      (txn->ops.capture_released == NULL))
    return AUTOBAUD_UART_TXN_INVALID_PROFILE;
  if (txn->fatal_latched) return AUTOBAUD_UART_TXN_FATAL_RECOVERY;
  if (txn->in_progress) return AUTOBAUD_UART_TXN_BUSY;

  txn->in_progress = true;
  txn->failure_phase = AUTOBAUD_UART_TXN_PHASE_IDLE;
  sat_increment(&txn->diag.captures);
  txn->phase = AUTOBAUD_UART_TXN_PHASE_CLAIM;
  if (!txn->ops.claim(txn->ctx)) {
    txn->in_progress = false;
    txn->phase = AUTOBAUD_UART_TXN_PHASE_IDLE;
    sat_increment(&txn->diag.claim_failures);
    return AUTOBAUD_UART_TXN_BUSY;
  }

  txn->phase = AUTOBAUD_UART_TXN_PHASE_SNAPSHOT;
  previous_passive = txn->ops.previous_passive(txn->ctx);
  if (!txn->ops.snapshot(txn->ctx, &previous) ||
      !autobaud_v2_profile_supported(&previous)) {
    txn->failure_phase = txn->phase;
    return finish(txn, AUTOBAUD_UART_TXN_FATAL_RECOVERY, true, false);
  }

  txn->phase = AUTOBAUD_UART_TXN_PHASE_QUIESCE;
  if (!txn->ops.quiesce(txn->ctx)) {
    sat_increment(&txn->diag.quiesce_failures);
    txn->failure_phase = txn->phase;
    return fail_after_claim(txn, true, &previous, previous_passive);
  }

  txn->phase = AUTOBAUD_UART_TXN_PHASE_CAPTURE;
  safe = txn->ops.capture(txn->ctx, timeout_ms, &captured, &measured);
  if (!safe || !txn->ops.capture_released(txn->ctx)) {
    txn->failure_phase = txn->phase;
    return finish(txn, AUTOBAUD_UART_TXN_FATAL_RECOVERY, true, false);
  }

  /* The old configuration is re-applied even when the D.1 result is
   * TIMEOUT/AMBIGUOUS: those are evidence failures, not ownership faults.
   */
  txn->phase = AUTOBAUD_UART_TXN_PHASE_ROLLBACK;
  if (!rollback(txn, &previous, previous_passive)) {
    txn->failure_phase = AUTOBAUD_UART_TXN_PHASE_ROLLBACK;
    return finish(txn, AUTOBAUD_UART_TXN_FATAL_RECOVERY, true, false);
  }
  if (measured == HW_AUTOBAUD_MEASURE_VALID)
    *measurement = captured;
  *capture_status = measured;
  return finish(txn, AUTOBAUD_UART_TXN_CAPTURE_RESTORED,
                false, previous_passive);
}
