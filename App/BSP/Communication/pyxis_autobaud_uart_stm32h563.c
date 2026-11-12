/* M4: target-specific binding for the portable Titania V2 UART transaction.
 * This source is only compiled when MODBUS_AUTOBAUD_V2_PORT_ENABLE=1.
 * The existing custom Modbus transport exclusively owns USART3/GPDMA RX.
 * M5 supplies an asynchronous edge-capture adapter; never invoke the
 * transaction engine's optional blocking capture helper from FreeRTOS.
 */
#include "pyxis_autobaud_uart_stm32h563.h"
#include "modbus_engine_v2_transport.h"
#include <stddef.h>

static autobaud_uart_txn_t s_transaction;
static bool s_initialized;

static bool port_claim(void *ctx)
{
  (void)ctx;
  return ModbusEngineV2_Claim();
}
static bool port_snapshot(void *ctx, autobaud_v2_profile_t *profile)
{
  (void)ctx;
  return ModbusEngineV2_Snapshot(profile);
}
static bool port_previous_passive(void *ctx)
{
  (void)ctx;
  return ModbusEngineV2_PreviousPassive();
}
static bool port_quiesce(void *ctx)
{
  (void)ctx;
  return ModbusEngineV2_Quiesce();
}
static bool port_apply(void *ctx, const autobaud_v2_profile_t *profile)
{
  (void)ctx;
  return ModbusEngineV2_Apply(profile);
}
static bool port_timing(void *ctx, const autobaud_v2_profile_t *profile)
{
  (void)ctx;
  return ModbusEngineV2_ApplyTiming(profile);
}
static bool port_restart(void *ctx, bool passive)
{
  (void)ctx;
  return ModbusEngineV2_Restart(passive);
}
static bool port_verify(void *ctx, const autobaud_v2_profile_t *profile,
                        bool passive)
{
  (void)ctx;
  return ModbusEngineV2_Verify(profile, passive);
}
static void port_release(void *ctx, bool fatal, bool passive)
{
  (void)ctx;
  ModbusEngineV2_Release(fatal, passive);
}

static const autobaud_uart_txn_ops_t k_ops = {
  .claim = port_claim,
  .snapshot = port_snapshot,
  .previous_passive = port_previous_passive,
  .quiesce = port_quiesce,
  .apply = port_apply,
  .timing = port_timing,
  .restart = port_restart,
  .verify = port_verify,
  .capture = NULL,  /* Non-blocking M5 interface, not a foreground loop. */
  .capture_released = NULL,
  .release = port_release
};

bool PyxisAutobaudUart_Init(void)
{
  if (s_initialized) return !PyxisAutobaudUart_IsFatal();
  if (!autobaud_uart_txn_init(&s_transaction, &k_ops, NULL)) return false;
  s_initialized = true;
  return true;
}

autobaud_uart_txn_result_t PyxisAutobaudUart_Apply(
    const autobaud_v2_profile_t *profile, bool passive)
{
  if (!s_initialized && !PyxisAutobaudUart_Init())
    return AUTOBAUD_UART_TXN_FATAL_RECOVERY;
  if (PyxisAutobaudUart_IsFatal())
    return AUTOBAUD_UART_TXN_FATAL_RECOVERY;
  return autobaud_uart_txn_apply(&s_transaction, profile, passive);
}

bool PyxisAutobaudUart_Snapshot(autobaud_v2_profile_t *profile,
                                bool *passive)
{
  if (profile == NULL || passive == NULL || PyxisAutobaudUart_IsFatal())
    return false;
  if (!ModbusEngineV2_Snapshot(profile)) return false;
  *passive = ModbusEngineV2_IsPassive();
  return autobaud_v2_profile_supported(profile);
}

bool PyxisAutobaudUart_IsFatal(void)
{
  return (s_initialized && s_transaction.fatal_latched) ||
         ModbusEngineV2_IsFatal();
}

bool PyxisAutobaudUart_GetDiagnostics(
    autobaud_uart_txn_diag_t *diagnostics,
    autobaud_uart_txn_phase_t *last_failure)
{
  if (!s_initialized || diagnostics == NULL) return false;
  *diagnostics = s_transaction.diag;
  if (last_failure != NULL) *last_failure = s_transaction.failure_phase;
  return true;
}
