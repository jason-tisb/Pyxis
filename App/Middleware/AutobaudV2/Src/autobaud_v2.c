/* Portable Titania V2 profile helpers, adapted only for self-contained Pyxis
 * public types. Not part of the active embedded CMake target in M1.
 */
#include "autobaud_v2.h"
#include <stddef.h>

_Static_assert(AUTOBAUD_V2_SCAN_PROFILE_COUNT ==
                   (AUTOBAUD_V2_BAUD_COUNT * AUTOBAUD_V2_FRAMING_COUNT),
               "V2 profile count must cover every baud/framing pair");

bool autobaud_v2_baud_supported(uint32_t baud_rate)
{
  switch (baud_rate) {
  case 9600U:
  case 19200U:
  case 38400U:
  case 57600U:
  case 115200U:
    return true;
  default:
    return false;
  }
}

bool autobaud_v2_framing_supported(autobaud_uart_mode_t framing)
{
  switch (framing) {
  case AUTOBAUD_UART_MODE_8N1:
  case AUTOBAUD_UART_MODE_8E1:
  case AUTOBAUD_UART_MODE_8O1:
  case AUTOBAUD_UART_MODE_8N2:
    return true;
  default:
    return false;
  }
}

bool autobaud_v2_profile_supported(const autobaud_v2_profile_t *profile)
{
  return (profile != NULL) &&
         autobaud_v2_baud_supported(profile->baud_rate) &&
         autobaud_v2_framing_supported(profile->framing);
}

bool autobaud_v2_state_is_terminal(autobaud_v2_state_t state)
{
  return (state == AUTOBAUD_V2_STATE_LOCKED) ||
         (state == AUTOBAUD_V2_STATE_FALLBACK) ||
         (state == AUTOBAUD_V2_STATE_ERROR);
}

void autobaud_v2_result_reset(autobaud_v2_result_t *result)
{
  if (result == NULL)
    return;
  *result = (autobaud_v2_result_t){0};
  result->state = AUTOBAUD_V2_STATE_IDLE;
  result->outcome = AUTOBAUD_V2_OUTCOME_NONE;
  result->lock_source = AUTOBAUD_V2_LOCK_SOURCE_NONE;
}
