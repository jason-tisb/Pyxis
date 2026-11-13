/* M7: compile the REAL Pyxis on-demand coordinator against bounded mocks.
 * One process per scenario: the production coordinator is intentionally
 * sticky and cannot be reset without an MCU reboot.
 */
#include "pyxis_autobaud_v2_stm32h563.h"
#include "pyxis_autobaud_uart_stm32h563.h"
#include "pyxis_autobaud_capture_stm32h563.h"
#include "pyxis_autobaud_framing_stm32h563.h"
#include "modbus_engine_v2_transport.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned scenario;
static uint32_t clock_ms;
static bool fatal;
static bool activity;
static bool direct_valid;
static bool passive;
static unsigned uart_applies;
static unsigned uart_begins;
static unsigned capture_calls;
static unsigned polls;
static unsigned framing_begins;
static unsigned framing_polls;
static unsigned reset_initials;
static unsigned latch_count;
static autobaud_e_ctx_t framing_result;
static autobaud_v2_profile_t active_uart = {
    9600U, AUTOBAUD_UART_MODE_8N1
};

uint32_t ModbusEngineV2_NowMs(void) { return clock_ms; }
bool ModbusEngineV2_IsFatal(void) { return fatal; }
void ModbusEngineV2_LatchFatal(void)
{
  fatal = true;
  ++latch_count;
}
bool PyxisAutobaudUart_Init(void) { ++uart_begins; return !fatal; }
bool PyxisAutobaudUart_IsFatal(void) { return fatal; }
autobaud_uart_txn_result_t PyxisAutobaudUart_Apply(
    const autobaud_v2_profile_t *p, bool psv)
{
  ++uart_applies;
  if (scenario == 4U) return AUTOBAUD_UART_TXN_BUSY;
  assert(p && psv && p->baud_rate == PYXIS_AUTOBAUD_V2_INITIAL_BAUD);
  assert(p->framing == AUTOBAUD_UART_MODE_8E1);
  active_uart = *p;
  passive = true;
  return AUTOBAUD_UART_TXN_APPLIED_VERIFIED;
}
bool PyxisAutobaudCapture_IsFatal(void) { return fatal; }
bool PyxisAutobaudCapture_Begin(uint32_t ms)
{
  assert(passive && ms == AUTOBAUD_OD_CAPTURE_MS);
  ++capture_calls;
  return true;
}
hw_autobaud_async_poll_t PyxisAutobaudCapture_Poll(
    hw_autobaud_measurement_t *measure,
    hw_autobaud_measure_status_t *status)
{
  ++polls;
  assert(measure && status);
  if (scenario == 5U) return HW_AUTOBAUD_ASYNC_FAILED;
  if (polls == 1U) return HW_AUTOBAUD_ASYNC_PENDING;
  if (scenario == 3U) {
    *status = HW_AUTOBAUD_MEASURE_TIMEOUT;
    return HW_AUTOBAUD_ASYNC_READY;
  }
  *measure = (hw_autobaud_measurement_t){
      .baud_rate=115200U, .confidence_permille=950U,
      .sample_count=25U};
  *status = HW_AUTOBAUD_MEASURE_VALID;
  return HW_AUTOBAUD_ASYNC_READY;
}
bool PyxisAutobaudFraming_Init(void) { return !fatal; }
bool PyxisAutobaudFraming_IsFaulted(void) { return fatal; }
bool PyxisAutobaudFraming_ResetInitial(void)
{
  ++reset_initials;
  return passive && !fatal && active_uart.baud_rate == 9600U &&
         active_uart.framing == AUTOBAUD_UART_MODE_8E1;
}
autobaud_od_probe_result_t PyxisAutobaudFraming_ProbeInitial(
    autobaud_v2_profile_t *out)
{
  assert(out);
  if (direct_valid) {
    direct_valid = false;
    assert(passive && active_uart.baud_rate == 9600U);
    *out = active_uart;
    passive = false; /* Models successful verified normal RX handoff. */
    return AUTOBAUD_OD_PROBE_LOCKED;
  }
  return AUTOBAUD_OD_PROBE_WAITING;
}
bool PyxisAutobaudFraming_Activity(void)
{
  bool observed = activity;
  activity = false;
  return observed;
}
bool PyxisAutobaudFraming_Start(
    const hw_autobaud_measurement_t *m, uint32_t budget, uint32_t dwell)
{
  assert(m && m->baud_rate == 115200U && m->confidence_permille >= 750U);
  assert(budget == 52000U && dwell == 12000U && passive);
  ++framing_begins;
  framing_result.active = (autobaud_v2_profile_t){
      115200U, AUTOBAUD_UART_MODE_8O1};
  return true;
}
autobaud_e_status_t PyxisAutobaudFraming_Service(void)
{
  ++framing_polls;
  if (framing_polls == 1U) return AUTOBAUD_E_RUNNING;
  passive = false;
  return AUTOBAUD_E_LOCKED;
}
const autobaud_e_ctx_t *PyxisAutobaudFraming_State(void)
{
  return &framing_result;
}

static pyxis_autobaud_v2_diagnostics_t diag(void)
{
  pyxis_autobaud_v2_diagnostics_t d={0};
  PyxisAutobaudV2_GetDiagnostics(&d);
  return d;
}

int main(int argc, char **argv)
{
  pyxis_autobaud_v2_diagnostics_t d;
  assert(argc == 2);
  scenario = (unsigned)strtoul(argv[1], NULL, 10);
  assert(scenario <= 5U);
  assert(!PyxisAutobaudV2_Locked());
  assert(PyxisAutobaudV2_Start());
  assert(!PyxisAutobaudV2_Start());
  assert(uart_applies == 0U && capture_calls == 0U);
  /* Normal Modbus and vibration tasks may already run; Start is instant. */
  PyxisAutobaudV2_Service();

  if (scenario == 4U) {
    assert(uart_applies == 1U && !PyxisAutobaudV2_Locked());
    clock_ms = PYXIS_AUTOBAUD_V2_START_BUDGET_MS - 1U;
    PyxisAutobaudV2_Service();
    assert(!PyxisAutobaudV2_IsFatal());
    ++clock_ms;
    PyxisAutobaudV2_Service();
    assert(PyxisAutobaudV2_IsFatal() && latch_count == 1U);
    assert(!PyxisAutobaudV2_Locked());
  } else if (scenario == 0U) {
    assert(diag().state == PYXIS_AUTOBAUD_V2_LISTENING);
    for (unsigned i = 0U; i < 200000U; ++i) {
      clock_ms += 1000U;
      PyxisAutobaudV2_Service();
    }
    assert(!PyxisAutobaudV2_Locked() && !PyxisAutobaudV2_IsFatal());
    assert(capture_calls == 0U && framing_begins == 0U);
    assert(latch_count == 0U);
  } else if (scenario == 1U) {
    direct_valid = true;
    PyxisAutobaudV2_Service();
    assert(PyxisAutobaudV2_Locked() && !passive);
    d=diag();
    assert(d.active.baud_rate == 9600U &&
           d.active.framing == AUTOBAUD_UART_MODE_8E1);
    assert(d.locks == 1U && d.capture_attempts == 0U);
    activity = true;
    for(unsigned i=0;i<30U;++i) {
      clock_ms += 20000U;
      PyxisAutobaudV2_Service();
    }
    assert(capture_calls == 0U && d.locks == 1U);
  } else {
    activity = true;
    PyxisAutobaudV2_Service();
    assert(capture_calls == 1U);
    PyxisAutobaudV2_Service();
    assert(polls == 1U);
    PyxisAutobaudV2_Service();
    if (scenario == 5U) {
      assert(PyxisAutobaudV2_IsFatal() && latch_count == 1U);
      assert(!PyxisAutobaudV2_Locked());
    } else if (scenario == 3U) {
      assert(reset_initials == 2U);
      d=diag();
      assert(d.engine_state == AUTOBAUD_OD_COOLDOWN);
      clock_ms += AUTOBAUD_OD_COOLDOWN_MS;
      PyxisAutobaudV2_Service();
      assert(diag().engine_state == AUTOBAUD_OD_WAIT_ACTIVITY);
      PyxisAutobaudV2_Service();
      assert(capture_calls == 1U && !PyxisAutobaudV2_Locked());
    } else {
      assert(framing_begins == 1U);
      PyxisAutobaudV2_Service();
      assert(framing_polls == 1U);
      PyxisAutobaudV2_Service();
      assert(PyxisAutobaudV2_Locked() && !passive);
      d=diag();
      assert(d.locks == 1U && d.active.baud_rate == 115200U);
      assert(d.active.framing == AUTOBAUD_UART_MODE_8O1);
      for(unsigned i=0;i<100U;++i) {
        clock_ms += 5000U;
        PyxisAutobaudV2_Service();
      }
      assert(capture_calls == 1U && framing_polls == 2U);
    }
  }
  puts("Pyxis M7 on-demand coordinator scenario: PASS");
  return 0;
}
