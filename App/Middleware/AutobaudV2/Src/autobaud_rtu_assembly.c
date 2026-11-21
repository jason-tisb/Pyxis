#include "autobaud_rtu_assembly.h"
#include <stddef.h>
#include <string.h>

uint32_t autobaud_rtu_quiet_ms(uint32_t baud, bool parity,
                               bool two_stop_bits)
{
  uint32_t bits;
  uint64_t numerator;
  if (baud == 0U) return 0U;
  if (baud > 19200U) return 2U; /* Modbus RTU fixed 1.75 ms. */
  bits = 1U + 8U + (parity ? 1U : 0U) + (two_stop_bits ? 2U : 1U);
  numerator = (uint64_t)35U * (uint64_t)bits * 1000ULL;
  /* ceil(3.5 * bits / baud * 1000) without float or overflow. */
  return (uint32_t)((numerator + (10ULL * baud - 1ULL)) /
                    (10ULL * baud));
}

void autobaud_rtu_assembly_reset(autobaud_rtu_assembly_t *state)
{
  if (state == NULL) return;
  state->length = 0U;
  state->last_ms = 0U;
}

bool autobaud_rtu_assembly_ready(const autobaud_rtu_assembly_t *state,
                                 uint32_t now_ms, uint32_t quiet_ms)
{
  return state != NULL && state->length != 0U && quiet_ms != 0U &&
         (uint32_t)(now_ms - state->last_ms) >= quiet_ms;
}

autobaud_rtu_append_result_t autobaud_rtu_assembly_append(
    autobaud_rtu_assembly_t *state,
    const uint8_t *data, uint16_t length, uint32_t ended_ms,
    uint32_t quiet_ms)
{
  if (state == NULL || data == NULL || length == 0U ||
      quiet_ms == 0U) return AUTOBAUD_RTU_APPEND_INVALID;
  if (state->length != 0U &&
      autobaud_rtu_assembly_ready(state, ended_ms, quiet_ms))
    return AUTOBAUD_RTU_APPEND_NEEDS_FLUSH;
  if (length > (uint32_t)AUTOBAUD_RTU_MAX_FRAME -
                   (uint32_t)state->length)
    return AUTOBAUD_RTU_APPEND_OVERFLOW;
  memcpy(&state->bytes[state->length], data, length);
  state->length = (uint16_t)(state->length + length);
  state->last_ms = ended_ms;
  return AUTOBAUD_RTU_APPEND_OK;
}

bool autobaud_rtu_assembly_take(autobaud_rtu_assembly_t *state,
                                uint8_t *out, uint16_t capacity,
                                uint16_t *length)
{
  if (state == NULL || out == NULL || length == NULL ||
      state->length == 0U || capacity < state->length) return false;
  memcpy(out, state->bytes, state->length);
  *length = state->length;
  autobaud_rtu_assembly_reset(state);
  return true;
}
