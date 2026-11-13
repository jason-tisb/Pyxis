/* M6 passive RTU frame evidence and atomic serial receive handoff.
 * Compiled only with MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1.
 * M7 supplies the on-demand supervisor calls; this file performs no boot
 * scanning, normal Modbus processing, TX or persistent writes.
 */
#ifndef PYXIS_AUTOBAUD_FRAMING_STM32H563_H
#define PYXIS_AUTOBAUD_FRAMING_STM32H563_H

#include "autobaud_ondemand.h"
#include "autobaud_v2_framing.h"
#include <stdbool.h>

bool PyxisAutobaudFraming_Init(void);
bool PyxisAutobaudFraming_ResetInitial(void);
autobaud_od_probe_result_t PyxisAutobaudFraming_ProbeInitial(
    autobaud_v2_profile_t *locked_profile);
bool PyxisAutobaudFraming_Activity(void);
bool PyxisAutobaudFraming_Start(
    const hw_autobaud_measurement_t *measurement,
    uint32_t budget_ms, uint32_t dwell_ms);
autobaud_e_status_t PyxisAutobaudFraming_Service(void);
const autobaud_e_ctx_t *PyxisAutobaudFraming_State(void);
bool PyxisAutobaudFraming_IsFaulted(void);

#endif
