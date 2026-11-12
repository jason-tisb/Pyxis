#include "autobaud_ondemand.h"
#include <string.h>
static void od_fault(autobaud_od_t *ctx)
{
  ctx->state = AUTOBAUD_OD_FAULT;
  ctx->entered_ms = ctx->ops.now_ms(ctx->ops.user);
  ctx->ops.latch_fault(ctx->ops.user);
}
static void od_cooldown(autobaud_od_t *ctx)
{
  ++ctx->failures;
  ctx->state = AUTOBAUD_OD_COOLDOWN;
  ctx->entered_ms = ctx->ops.now_ms(ctx->ops.user);
}
bool autobaud_od_init(autobaud_od_t *ctx, const autobaud_od_ops_t *ops)
{
  if (ctx == NULL || ops == NULL || ops->now_ms == NULL ||
      ops->activity == NULL || ops->capture_begin == NULL ||
      ops->capture_poll == NULL || ops->framing_begin == NULL ||
      ops->framing_poll == NULL || ops->fatal == NULL ||
      ops->latch_fault == NULL) return false;
  memset(ctx, 0, sizeof(*ctx));
  ctx->ops = *ops;
  ctx->state = AUTOBAUD_OD_WAIT_ACTIVITY;
  ctx->initialized = true;
  return true;
}
void autobaud_od_service(autobaud_od_t *ctx)
{
  hw_autobaud_measurement_t measurement = {0};
  autobaud_v2_profile_t verified = {0};
  autobaud_od_capture_t capture;
  autobaud_od_frame_t frame;
  if (ctx == NULL || !ctx->initialized ||
      ctx->state == AUTOBAUD_OD_LOCKED ||
      ctx->state == AUTOBAUD_OD_FAULT) return;
  if (ctx->ops.fatal(ctx->ops.user)) {
    od_fault(ctx);
    return;
  }
  switch (ctx->state) {
  case AUTOBAUD_OD_WAIT_ACTIVITY:
    /* A complete trustworthy request at the default UART profile is
     * stronger evidence than raw DMA bytes. Check it before TIM3. */
    if (ctx->ops.probe_initial != NULL) {
      autobaud_od_probe_result_t probe =
          ctx->ops.probe_initial(ctx->ops.user, &verified);
      if (probe == AUTOBAUD_OD_PROBE_FAULT) {
        od_fault(ctx);
        return;
      }
      if (probe == AUTOBAUD_OD_PROBE_LOCKED) {
        if (!autobaud_v2_profile_supported(&verified)) {
          od_fault(ctx);
          return;
        }
        ctx->active = verified;
        ++ctx->locks;
        ctx->state = AUTOBAUD_OD_LOCKED;
        ctx->entered_ms = ctx->ops.now_ms(ctx->ops.user);
        return;
      }
    }
    if (!ctx->ops.activity(ctx->ops.user)) return;
    ++ctx->attempts;
    if (!ctx->ops.capture_begin(ctx->ops.user, AUTOBAUD_OD_CAPTURE_MS)) {
      if (ctx->ops.fatal(ctx->ops.user)) od_fault(ctx);
      else od_cooldown(ctx);
      return;
    }
    ctx->state = AUTOBAUD_OD_CAPTURING;
    ctx->entered_ms = ctx->ops.now_ms(ctx->ops.user);
    return;
  case AUTOBAUD_OD_CAPTURING:
    capture = ctx->ops.capture_poll(ctx->ops.user, &measurement);
    if (capture == AUTOBAUD_OD_CAPTURE_PENDING) return;
    if (capture == AUTOBAUD_OD_CAPTURE_FATAL) {
      od_fault(ctx);
      return;
    }
    if (capture != AUTOBAUD_OD_CAPTURE_VALID ||
        !autobaud_v2_baud_supported(measurement.baud_rate) ||
        measurement.confidence_permille < 750U ||
        measurement.sample_count < HW_AUTOBAUD_MIN_VALID_INTERVALS) {
      od_cooldown(ctx);
      return;
    }
    if (!ctx->ops.framing_begin(ctx->ops.user, &measurement,
                                AUTOBAUD_OD_FRAMING_MS,
                                AUTOBAUD_OD_FRAMING_DWELL_MS)) {
      if (ctx->ops.fatal(ctx->ops.user)) od_fault(ctx);
      else od_cooldown(ctx);
      return;
    }
    ctx->state = AUTOBAUD_OD_FRAMING;
    ctx->entered_ms = ctx->ops.now_ms(ctx->ops.user);
    return;
  case AUTOBAUD_OD_FRAMING:
    frame = ctx->ops.framing_poll(ctx->ops.user, &verified);
    if (frame == AUTOBAUD_OD_FRAME_RUNNING) return;
    if (frame == AUTOBAUD_OD_FRAME_FATAL) {
      od_fault(ctx);
      return;
    }
    if (frame != AUTOBAUD_OD_FRAME_LOCKED ||
        !autobaud_v2_profile_supported(&verified)) {
      od_cooldown(ctx);
      return;
    }
    ctx->active = verified;
    ++ctx->locks;
    ctx->state = AUTOBAUD_OD_LOCKED;
    ctx->entered_ms = ctx->ops.now_ms(ctx->ops.user);
    return;
  case AUTOBAUD_OD_COOLDOWN:
    if ((uint32_t)(ctx->ops.now_ms(ctx->ops.user) - ctx->entered_ms) >=
        AUTOBAUD_OD_COOLDOWN_MS)
      ctx->state = AUTOBAUD_OD_WAIT_ACTIVITY;
    return;
  default:
    od_fault(ctx);
    return;
  }
}
bool autobaud_od_locked(const autobaud_od_t *ctx)
{
  return ctx != NULL && ctx->initialized &&
         ctx->state == AUTOBAUD_OD_LOCKED;
}
