/* M7 Pyxis V2 on-demand coordinator.
 * Board binding only; never include this from portable AutobaudV2.
 * The M3 supervisor remains pure C; only the Modbus task calls Service.
 */
#ifndef PYXIS_AUTOBAUD_V2_STM32H563_H
#define PYXIS_AUTOBAUD_V2_STM32H563_H

#include "autobaud_ondemand.h"
#include <stdbool.h>
#include <stdint.h>

#define PYXIS_AUTOBAUD_V2_INITIAL_BAUD 9600U
#define PYXIS_AUTOBAUD_V2_START_BUDGET_MS 2000U

typedef enum {
  PYXIS_AUTOBAUD_V2_DISABLED = 0,
  PYXIS_AUTOBAUD_V2_STARTING,
  PYXIS_AUTOBAUD_V2_LISTENING,
  PYXIS_AUTOBAUD_V2_LOCKED,
  PYXIS_AUTOBAUD_V2_FAULT
} pyxis_autobaud_v2_run_state_t;

typedef struct {
  pyxis_autobaud_v2_run_state_t state;
  autobaud_od_state_t engine_state;
  autobaud_v2_profile_t active;
  uint32_t start_attempts;
  uint32_t start_failures;
  uint32_t capture_attempts;
  uint32_t recovery_attempts;
  uint32_t locks;
  uint32_t failures;
} pyxis_autobaud_v2_diagnostics_t;

/* Arm only when configured baud selector is 0 and V2 is compiled.
 * Does not wait, claim UART, start capture or block task startup. */
bool PyxisAutobaudV2_Start(void);

/* Call once per Modbus task loop. One bounded transaction or supervisor
 * operation per call, no burst wait and no legacy startup scanner. */
void PyxisAutobaudV2_Service(void);

bool PyxisAutobaudV2_Locked(void);
bool PyxisAutobaudV2_IsFatal(void);
void PyxisAutobaudV2_GetDiagnostics(
    pyxis_autobaud_v2_diagnostics_t *out);

#endif
