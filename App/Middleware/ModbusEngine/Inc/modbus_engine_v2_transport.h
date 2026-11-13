/* M4: Pyxis-specific USART3/GPDMA1 transport transaction seam.
 * Implemented by the custom Modbus receive transport only when
 * MODBUS_AUTOBAUD_V2_PORT_ENABLE=1. Caller: task context, not ISR.
 * No GPIO or STM32 registers leak into the portable autobaud engine.
 */
#ifndef PYXIS_MODBUS_ENGINE_V2_TRANSPORT_H
#define PYXIS_MODBUS_ENGINE_V2_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "autobaud_v2.h"

bool ModbusEngineV2_Claim(void);
bool ModbusEngineV2_Snapshot(autobaud_v2_profile_t *profile);
bool ModbusEngineV2_PreviousPassive(void);
bool ModbusEngineV2_Quiesce(void);
bool ModbusEngineV2_Apply(const autobaud_v2_profile_t *profile);
bool ModbusEngineV2_ApplyTiming(const autobaud_v2_profile_t *profile);
bool ModbusEngineV2_Restart(bool passive);
bool ModbusEngineV2_Verify(const autobaud_v2_profile_t *profile, bool passive);
void ModbusEngineV2_Release(bool fatal, bool passive);
bool ModbusEngineV2_IsFatal(void);
bool ModbusEngineV2_SnapshotSession(uint32_t *session_id,
                                     uint32_t *error_generation);
bool ModbusEngineV2_IsPassive(void);
/* Suppress active Modbus TX while the worker/transaction is claimed,
 * passive framing is in progress, or a fatal communication fault is latched. */
bool ModbusEngineV2_TxAllowed(void);

/* M6 passive frame transport. The source is enabled only when the new
 * framing adapter is compiled; the current production image remains inert. */
#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
#include "autobaud_v2_evidence.h"
bool ModbusEngineV2_RxActivity(uint32_t *total_bytes, uint32_t *total_errors);
autobaud_e_poll_t ModbusEngineV2_TakePassiveFrame(
    uint8_t *out, uint16_t capacity, uint16_t *length,
    autobaud_e_frame_meta_t *meta);
bool ModbusEngineV2_VerifyPassiveFrame(
    const autobaud_v2_profile_t *profile, uint32_t session_id,
    uint32_t error_generation);
bool ModbusEngineV2_AcceptPassiveFrame(
    const autobaud_v2_profile_t *profile,
    const autobaud_e_frame_meta_t *meta);
void ModbusEngineV2_LatchFatal(void);
#endif

#endif
