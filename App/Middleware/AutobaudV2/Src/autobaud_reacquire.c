#include "autobaud_reacquire.h"
#include <stddef.h>
#include <stdint.h>

void autobaud_reacquire_reset(autobaud_reacquire_t *ctx,
                             uint32_t total_framing_errors)
{
  if (ctx == NULL) return;
  *ctx = (autobaud_reacquire_t){0};
  ctx->initialized = true;
  ctx->baseline = total_framing_errors;
}

bool autobaud_reacquire_observe(autobaud_reacquire_t *ctx,
                               uint32_t now_ms,
                               uint32_t total_framing_errors)
{
  uint32_t delta;
  if (ctx == NULL) return false;
  if (!ctx->initialized) {
    autobaud_reacquire_reset(ctx, total_framing_errors);
    return false;
  }
  delta = total_framing_errors - ctx->baseline; /* rollover-safe */
  ctx->baseline = total_framing_errors;
  if (delta == 0U) return false; /* silence is never a trigger */

  if (ctx->burst_count == 0U ||
      (uint32_t)(now_ms - ctx->window_start_ms) >=
          AUTOBAUD_REACQUIRE_WINDOW_MS) {
    ctx->window_start_ms = now_ms;
    ctx->last_burst_ms = now_ms;
    ctx->errors_in_window = 0U;
    ctx->burst_count = 1U;
  } else if ((uint32_t)(now_ms - ctx->last_burst_ms) >=
             AUTOBAUD_REACQUIRE_BURST_GAP_MS) {
    if (ctx->burst_count < UINT8_MAX) ++ctx->burst_count;
    ctx->last_burst_ms = now_ms;
  }
  if (delta > UINT32_MAX - ctx->errors_in_window)
    ctx->errors_in_window = UINT32_MAX;
  else
    ctx->errors_in_window += delta;
  return ctx->burst_count >= AUTOBAUD_REACQUIRE_MIN_BURSTS &&
         ctx->errors_in_window >= AUTOBAUD_REACQUIRE_MIN_ERRORS;
}
