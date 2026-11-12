/* Pyxis M4: compile the actual board adapter against mock transport.
 * Confirms the M3 transaction coordinator invokes the M4 transport in
 * the correct order, enforces passive receive and never returns a false
 * verified result after a broken rollback. Does not emulate STM32 registers.
 */
#include "pyxis_autobaud_uart_stm32h563.h"
#include "modbus_engine_v2_transport.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  autobaud_v2_profile_t uart;
  autobaud_v2_profile_t timing;
  autobaud_v2_profile_t snapshot;
  bool claimed;
  bool quiesced;
  bool rx_ready;
  bool passive;
  bool saved_passive;
  bool fatal;
  bool fail_claim;
  int verify_fail_count;
  unsigned claims;
  unsigned quiesces;
  unsigned applies;
  unsigned restarts;
  unsigned releases;
  unsigned sessions;
} fake_t;
static fake_t f = {
    .uart = {9600U, AUTOBAUD_UART_MODE_8N1},
    .timing = {9600U, AUTOBAUD_UART_MODE_8N1},
    .rx_ready = true
};
static bool same(const autobaud_v2_profile_t *a,
                 const autobaud_v2_profile_t *b)
{
  return a->baud_rate == b->baud_rate && a->framing == b->framing;
}
bool ModbusEngineV2_Claim(void)
{
  if (f.fatal || f.claimed || f.fail_claim) return false;
  f.saved_passive = f.passive;
  f.claimed = true;
  f.claims++;
  return true;
}
bool ModbusEngineV2_Snapshot(autobaud_v2_profile_t *p)
{
  if (f.fatal || p == NULL) return false;
  *p = f.uart;
  return true;
}
bool ModbusEngineV2_PreviousPassive(void)
{
  return f.saved_passive;
}
bool ModbusEngineV2_Quiesce(void)
{
  if (!f.claimed) return false;
  f.quiesced = true;
  f.rx_ready = false;
  f.quiesces++;
  return true;
}
bool ModbusEngineV2_Apply(const autobaud_v2_profile_t *p)
{
  if (!f.claimed || !f.quiesced) return false;
  f.uart = *p;
  f.applies++;
  return true;
}
bool ModbusEngineV2_ApplyTiming(const autobaud_v2_profile_t *p)
{
  if (!f.quiesced) return false;
  f.timing = *p;
  return true;
}
bool ModbusEngineV2_Restart(bool passive)
{
  if (!f.quiesced) return false;
  f.rx_ready = true;
  f.quiesced = false;
  f.passive = passive;
  f.restarts++;
  return true;
}
bool ModbusEngineV2_Verify(const autobaud_v2_profile_t *p, bool passive)
{
  if (f.verify_fail_count > 0) {
    f.verify_fail_count--;
    return false;
  }
  return f.claimed && f.rx_ready && !f.quiesced &&
         f.passive == passive && same(&f.uart, p) &&
         same(&f.timing, p);
}
void ModbusEngineV2_Release(bool fatal, bool passive)
{
  f.releases++;
  f.claimed = false;
  if (fatal) f.fatal = true;
  else {
    f.passive = passive;
    f.sessions++;
  }
}
bool ModbusEngineV2_IsFatal(void) { return f.fatal; }
bool ModbusEngineV2_IsPassive(void) { return f.passive; }
bool ModbusEngineV2_SnapshotSession(uint32_t *session,
                                    uint32_t *errors)
{
  if (session == NULL || errors == NULL || f.fatal || !f.passive)
    return false;
  *session = f.sessions;
  *errors = 0U;
  return f.sessions != 0U;
}

int main(void)
{
  const uint32_t bauds[] = {9600U, 19200U, 38400U, 57600U, 115200U};
  autobaud_v2_profile_t profile = {0};
  autobaud_uart_txn_diag_t diag = {0};
  autobaud_uart_txn_phase_t failure = AUTOBAUD_UART_TXN_PHASE_FATAL;
  bool passive = true;
  uint32_t session = 0U, generation = 0U;

  assert(PyxisAutobaudUart_Init());
  assert(PyxisAutobaudUart_Init());
  assert(PyxisAutobaudUart_Snapshot(&profile, &passive));
  assert(profile.baud_rate == 9600U && !passive);
  assert(PyxisAutobaudUart_Apply(NULL, false) ==
         AUTOBAUD_UART_TXN_INVALID_PROFILE);

  for (unsigned b = 0U; b < 5U; ++b) {
    for (unsigned mode = 0U; mode < 4U; ++mode) {
      profile.baud_rate = bauds[b];
      profile.framing = (autobaud_uart_mode_t)mode;
      assert(PyxisAutobaudUart_Apply(&profile, true) ==
             AUTOBAUD_UART_TXN_APPLIED_VERIFIED);
      assert(f.passive && f.rx_ready && !f.fatal && !f.claimed);
      assert(same(&f.uart, &profile));
      assert(PyxisAutobaudUart_Snapshot(&profile, &passive) && passive);
      assert(ModbusEngineV2_SnapshotSession(&session, &generation));
      assert(session == f.sessions);
    }
  }
  assert(f.claims == 20U && f.sessions == 20U);
  assert(f.releases == 20U);

  /* A busy UART claim must not reconfigure or latch fatal. */
  f.fail_claim = true;
  profile = (autobaud_v2_profile_t){115200U, AUTOBAUD_UART_MODE_8N1};
  assert(PyxisAutobaudUart_Apply(&profile, false) ==
         AUTOBAUD_UART_TXN_BUSY);
  assert(!f.fatal);
  f.fail_claim = false;

  /* A failed physical verify must be rolled back to the former profile,
   * including restoring passive receive ownership. */
  profile = (autobaud_v2_profile_t){19200U, AUTOBAUD_UART_MODE_8E1};
  const autobaud_v2_profile_t before = f.uart;
  f.verify_fail_count = 1;
  assert(PyxisAutobaudUart_Apply(&profile, true) ==
         AUTOBAUD_UART_TXN_ROLLED_BACK_VERIFIED);
  assert(f.passive && same(&f.uart, &before) && !f.fatal);

  assert(PyxisAutobaudUart_GetDiagnostics(&diag, &failure));
  assert(diag.successes == 20U);
  assert(diag.rollbacks == 1U && diag.rollback_successes == 1U);
  assert(failure == AUTOBAUD_UART_TXN_PHASE_VERIFY);

  /* Failure to verify both candidate and rollback permanently latches
   * the transport and blocks any subsequent profile update. */
  f.verify_fail_count = 2;
  assert(PyxisAutobaudUart_Apply(&profile, true) ==
         AUTOBAUD_UART_TXN_FATAL_RECOVERY);
  assert(PyxisAutobaudUart_IsFatal() && f.fatal);
  assert(PyxisAutobaudUart_Apply(&profile, false) ==
         AUTOBAUD_UART_TXN_FATAL_RECOVERY);
  assert(!PyxisAutobaudUart_Snapshot(&profile, &passive));
  assert(PyxisAutobaudUart_GetDiagnostics(&diag, &failure));
  assert(diag.rollback_failures == 1U);
  puts("Pyxis M4 UART transaction adapter: PASS");
  return 0;
}
