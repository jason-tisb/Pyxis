#include "autobaud_v2_framing.h"
#include "autobaud_v2_candidate.h"
#include <stddef.h>
#include <string.h>

/* E ordering: even parity, no-parity 2-stop, odd parity, no-parity 1-stop.
 * A standard UART cannot positively distinguish 8N1 vs 8N2 from a
 * CRC-correct received request: 8N2 is only the preferred convention.
 */
static const autobaud_uart_mode_t k_modes[AUTOBAUD_V2_FRAMING_COUNT] = {
  AUTOBAUD_UART_MODE_8E1, AUTOBAUD_UART_MODE_8N2,
  AUTOBAUD_UART_MODE_8O1, AUTOBAUD_UART_MODE_8N1
};

static void increment(uint32_t *value)
{
  if (*value != UINT32_MAX) ++*value;
}

static bool ops_valid(const autobaud_e_ops_t *ops)
{
  return ops != NULL && ops->now_ms != NULL && ops->fatal != NULL &&
         ops->snapshot != NULL && ops->apply != NULL &&
         ops->get_session != NULL && ops->take_frame != NULL &&
         ops->verify != NULL && ops->accept != NULL &&
         ops->latch_fatal != NULL;
}

static autobaud_e_status_t fatal(autobaud_e_ctx_t *ctx,
                                autobaud_e_status_t status)
{
  ctx->ops.latch_fatal(ctx->ops.user);
  ctx->state = AUTOBAUD_E_STATE_TERMINAL;
  ctx->status = status;
  return status;
}

static autobaud_e_status_t restore(autobaud_e_ctx_t *ctx,
                                   autobaud_e_status_t status)
{
  autobaud_uart_txn_result_t result =
      ctx->ops.apply(ctx->ops.user, &ctx->previous, ctx->previous_passive);
  if (result != AUTOBAUD_UART_TXN_APPLIED_VERIFIED ||
      ctx->ops.fatal(ctx->ops.user))
  {
    increment(&ctx->diag.restore_failures);
    return fatal(ctx, AUTOBAUD_E_RESTORE_FAILED);
  }
  ctx->state = AUTOBAUD_E_STATE_TERMINAL;
  ctx->status = status;
  return status;
}

static autobaud_e_status_t next(autobaud_e_ctx_t *ctx)
{
  ++ctx->candidate_index;
  ctx->rx_recoveries = 0U;
  ctx->session_id = 0U;
  if (ctx->candidate_index >= AUTOBAUD_V2_FRAMING_COUNT)
    return restore(ctx, AUTOBAUD_E_NO_VALID_REQUEST);
  ctx->state = AUTOBAUD_E_STATE_APPLY;
  return AUTOBAUD_E_RUNNING;
}

static autobaud_e_status_t recover_rx(autobaud_e_ctx_t *ctx)
{
  autobaud_uart_txn_result_t recovered;
  increment(&ctx->diag.rx_faults);
  if (ctx->rx_recoveries >= AUTOBAUD_E_MAX_RX_RECOVERIES)
    return next(ctx);
  ++ctx->rx_recoveries;
  increment(&ctx->diag.recovery_attempts);
  recovered = ctx->ops.apply(ctx->ops.user, &ctx->active, true);
  if (recovered == AUTOBAUD_UART_TXN_FATAL_RECOVERY ||
      recovered == AUTOBAUD_UART_TXN_BUSY)
    return fatal(ctx, AUTOBAUD_E_FATAL_RECOVERY);
  if (recovered != AUTOBAUD_UART_TXN_APPLIED_VERIFIED ||
      !ctx->ops.get_session(ctx->ops.user, &ctx->session_id) ||
      ctx->session_id == 0U) {
    increment(&ctx->diag.recovery_failures);
    return next(ctx);
  }
  return AUTOBAUD_E_RUNNING;
}

/* Called immediately before the candidate's passive-to-normal handoff.
 * The associated frame was already consumed by the candidate helper.
 */
static bool candidate_deadline(void *user)
{
  const autobaud_e_ctx_t *ctx = (const autobaud_e_ctx_t *)user;
  uint32_t now = ctx->ops.now_ms(ctx->ops.user);
  return (uint32_t)(now - ctx->start_ms) < ctx->budget_ms &&
         (uint32_t)(now - ctx->candidate_start_ms) <
             ctx->candidate_timeout_ms;
}

bool autobaud_e_start_dwell(autobaud_e_ctx_t *ctx, const autobaud_e_ops_t *ops,
                            const hw_autobaud_measurement_t *measurement,
                            hw_autobaud_measure_status_t measurement_status,
                            uint32_t budget_ms, uint32_t candidate_timeout_ms)
{
  if (ctx == NULL || !ops_valid(ops)) return false;
  if (ctx->state == AUTOBAUD_E_STATE_APPLY ||
      ctx->state == AUTOBAUD_E_STATE_LISTEN)
    return false; /* Do not erase an active transaction. */
  memset(ctx, 0, sizeof(*ctx));
  ctx->ops = *ops;
  ctx->state = AUTOBAUD_E_STATE_TERMINAL;
  ctx->status = AUTOBAUD_E_INVALID_MEASUREMENT;

  if (measurement == NULL || measurement_status != HW_AUTOBAUD_MEASURE_VALID ||
      !autobaud_v2_baud_supported(measurement->baud_rate) ||
      measurement->confidence_permille < 750U ||
      measurement->sample_count < HW_AUTOBAUD_MIN_VALID_INTERVALS ||
      budget_ms == 0U || budget_ms >= UINT32_MAX / 2U ||
      candidate_timeout_ms == 0U || candidate_timeout_ms > budget_ms)
    return false;
  if (ops->fatal(ops->user)) {
    ctx->status = AUTOBAUD_E_FATAL_RECOVERY;
    return false;
  }
  if (!ops->snapshot(ops->user, &ctx->previous, &ctx->previous_passive) ||
      !autobaud_v2_profile_supported(&ctx->previous)) {
    ctx->status = AUTOBAUD_E_BUSY; /* No hardware mutated. */
    return false;
  }
  ctx->measurement = *measurement;
  ctx->budget_ms = budget_ms;
  ctx->candidate_timeout_ms = candidate_timeout_ms;
  ctx->start_ms = ops->now_ms(ops->user);
  ctx->state = AUTOBAUD_E_STATE_APPLY;
  ctx->status = AUTOBAUD_E_RUNNING;
  return true;
}

autobaud_e_status_t autobaud_e_service(autobaud_e_ctx_t *ctx)
{
  uint32_t now;
  if (ctx == NULL) return AUTOBAUD_E_INVALID_MEASUREMENT;
  if (ctx->state == AUTOBAUD_E_STATE_IDLE)
    return AUTOBAUD_E_INVALID_MEASUREMENT;
  if (ctx->state == AUTOBAUD_E_STATE_TERMINAL)
    return ctx->status;
  if (ctx->ops.fatal(ctx->ops.user))
    return fatal(ctx, AUTOBAUD_E_FATAL_RECOVERY);

  now = ctx->ops.now_ms(ctx->ops.user);
  if ((uint32_t)(now - ctx->start_ms) >= ctx->budget_ms)
    return restore(ctx, AUTOBAUD_E_BUDGET_EXPIRED);

  if (ctx->state == AUTOBAUD_E_STATE_APPLY) {
    autobaud_uart_txn_result_t applied;
    ctx->active.baud_rate = ctx->measurement.baud_rate;
    ctx->active.framing = k_modes[ctx->candidate_index];
    increment(&ctx->diag.candidates);
    applied = ctx->ops.apply(ctx->ops.user, &ctx->active, true);
    if (applied == AUTOBAUD_UART_TXN_BUSY) {
      /* On the first candidate no UART mutation has yet occurred. Never
       * attempt a competing restore while another owner reports BUSY.
       * Later candidate ownership conflict is unsafe and must fail closed.
       */
      if (ctx->candidate_index == 0U) {
        ctx->state = AUTOBAUD_E_STATE_TERMINAL;
        ctx->status = AUTOBAUD_E_BUSY;
        return AUTOBAUD_E_BUSY;
      }
      return fatal(ctx, AUTOBAUD_E_FATAL_RECOVERY);
    }
    if (applied == AUTOBAUD_UART_TXN_FATAL_RECOVERY)
      return fatal(ctx, AUTOBAUD_E_FATAL_RECOVERY);
    if (applied == AUTOBAUD_UART_TXN_ROLLED_BACK_VERIFIED) {
      increment(&ctx->diag.recovery_failures);
      return next(ctx);
    }
    if (applied != AUTOBAUD_UART_TXN_APPLIED_VERIFIED ||
        !ctx->ops.get_session(ctx->ops.user, &ctx->session_id) ||
        ctx->session_id == 0U)
      return fatal(ctx, AUTOBAUD_E_FATAL_RECOVERY);
    ctx->candidate_start_ms = ctx->ops.now_ms(ctx->ops.user);
    ctx->state = AUTOBAUD_E_STATE_LISTEN;
    return AUTOBAUD_E_RUNNING;
  }

  if ((uint32_t)(now - ctx->candidate_start_ms) >=
      ctx->candidate_timeout_ms) {
    increment(&ctx->diag.candidate_timeouts);
    return next(ctx);
  }

  if (ctx->state == AUTOBAUD_E_STATE_LISTEN) {
    autobaud_candidate_observation_t observed = {0};
    autobaud_candidate_status_t status =
        autobaud_candidate_observe(&ctx->ops, ctx->session_id, &observed);

    if (status == AUTOBAUD_CANDIDATE_WAIT) return AUTOBAUD_E_RUNNING;
    if (status == AUTOBAUD_CANDIDATE_RX_FAULT) return recover_rx(ctx);
    if (observed.frame.frame_id != 0U)
      increment(&ctx->diag.frames_checked);
    if (status == AUTOBAUD_CANDIDATE_REJECTED) {
      increment(&ctx->diag.frames_rejected);
      if (observed.stale_session)
        increment(&ctx->diag.invalid_session_frames);
      if (observed.validation == AUTOBAUD_VALIDATE_INVALID_CRC)
        increment(&ctx->diag.crc_rejections);
      if (observed.validation == AUTOBAUD_VALIDATE_UNTRUSTED_RX)
        increment(&ctx->diag.untrusted_rejections);
      return AUTOBAUD_E_RUNNING;
    }
    if (status != AUTOBAUD_CANDIDATE_READY ||
        !autobaud_candidate_commit(&ctx->ops, &ctx->active, &observed,
                                   candidate_deadline, ctx)) {
      increment(&ctx->diag.lock_handoff_failures);
      if (ctx->ops.fatal(ctx->ops.user))
        return fatal(ctx, AUTOBAUD_E_FATAL_RECOVERY);
      /* A late request must not be committed after the candidate/global
       * deadline. Observe the deadline before deciding how to recover.
       */
      if ((uint32_t)(ctx->ops.now_ms(ctx->ops.user) -
                      ctx->start_ms) >= ctx->budget_ms)
        return restore(ctx, AUTOBAUD_E_BUDGET_EXPIRED);
      if ((uint32_t)(ctx->ops.now_ms(ctx->ops.user) -
                      ctx->candidate_start_ms) >=
          ctx->candidate_timeout_ms)
        return next(ctx);
      return recover_rx(ctx);
    }
    ctx->observed_slave_id = observed.request.slave_id;
    ctx->observed_function_code = observed.request.function_code;
    increment(&ctx->diag.valid_requests);
    ctx->state = AUTOBAUD_E_STATE_TERMINAL;
    ctx->status = AUTOBAUD_E_LOCKED;
    ctx->stop_bits_independently_verified = false;
    return AUTOBAUD_E_LOCKED;
  }
  return fatal(ctx, AUTOBAUD_E_FATAL_RECOVERY);
}

autobaud_e_status_t autobaud_e_cancel(autobaud_e_ctx_t *ctx)
{
  if (ctx == NULL) return AUTOBAUD_E_INVALID_MEASUREMENT;
  if (ctx->state == AUTOBAUD_E_STATE_TERMINAL)
    return ctx->status;
  if (ctx->state == AUTOBAUD_E_STATE_IDLE)
    return AUTOBAUD_E_INVALID_MEASUREMENT;
  if (ctx->ops.fatal(ctx->ops.user))
    return fatal(ctx, AUTOBAUD_E_FATAL_RECOVERY);
  return restore(ctx, AUTOBAUD_E_NO_VALID_REQUEST);
}

bool autobaud_e_start(autobaud_e_ctx_t *ctx, const autobaud_e_ops_t *ops,
                      const hw_autobaud_measurement_t *measurement,
                      hw_autobaud_measure_status_t status, uint32_t budget_ms)
{
  return autobaud_e_start_dwell(ctx, ops, measurement, status, budget_ms,
                                AUTOBAUD_V2_FRAMING_TIMEOUT_MS);
}
