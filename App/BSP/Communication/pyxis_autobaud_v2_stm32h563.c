/* Pyxis M7: wire Titania's HAL-independent on-demand supervisor to
 * Pyxis USART3/GPDMA1 Ch2, TIM2 Ch4 and passive RX evidence (M4-M6).
 *
 * The application remains active at startup; only the Modbus reply path
 * is gated until CRC-valid evidence confirms the UART profile.
 * No autobaud scanner, no silent-master reacquire, no request replay.
 */
#include "pyxis_autobaud_v2_stm32h563.h"
#include "pyxis_autobaud_uart_stm32h563.h"
#include "pyxis_autobaud_capture_stm32h563.h"
#include "pyxis_autobaud_framing_stm32h563.h"
#include "modbus_engine_v2_transport.h"
#include <stddef.h>

static autobaud_od_t s_supervisor;
static pyxis_autobaud_v2_diagnostics_t s_diag;
static uint32_t s_start_ms;
static uint8_t s_armed;
static uint8_t s_started;

static uint32_t od_now(void *u)
{
  (void)u;
  return ModbusEngineV2_NowMs();
}

static bool od_fatal(void *u)
{
  (void)u;
  return ModbusEngineV2_IsFatal() ||
         PyxisAutobaudUart_IsFatal() ||
         PyxisAutobaudCapture_IsFatal() ||
         PyxisAutobaudFraming_IsFaulted();
}

static void od_latch(void *u)
{
  (void)u;
  if (!ModbusEngineV2_IsFatal()) ModbusEngineV2_LatchFatal();
  s_diag.state = PYXIS_AUTOBAUD_V2_FAULT;
}

static bool reset_default_evidence(void)
{
  if (od_fatal(NULL)) return false;
  /* M6 checks exact 9600 8E1 passive UART, DMA session and error token. */
  return PyxisAutobaudFraming_ResetInitial();
}

static bool od_activity(void *u)
{
  (void)u;
  return PyxisAutobaudFraming_Activity();
}

static autobaud_od_probe_result_t od_probe(
    void *u, autobaud_v2_profile_t *profile)
{
  (void)u;
  return PyxisAutobaudFraming_ProbeInitial(profile);
}

static bool od_capture_begin(void *u, uint32_t budget)
{
  (void)u;
  ++s_diag.capture_attempts;
  if (PyxisAutobaudCapture_Begin(budget)) return true;
  /* A BUSY claim or safely rolled-back failed start must not count the
   * same original RX burst again after the supervisor's cooldown. */
  if (!od_fatal(NULL) && !reset_default_evidence()) od_latch(NULL);
  return false;
}

static autobaud_od_capture_t od_capture_poll(
    void *u, hw_autobaud_measurement_t *measurement)
{
  hw_autobaud_measure_status_t status = HW_AUTOBAUD_MEASURE_ERROR;
  hw_autobaud_async_poll_t poll;
  (void)u;
  if (measurement == NULL) return AUTOBAUD_OD_CAPTURE_FATAL;
  poll = PyxisAutobaudCapture_Poll(measurement, &status);
  if (poll == HW_AUTOBAUD_ASYNC_PENDING)
    return AUTOBAUD_OD_CAPTURE_PENDING;
  if (poll == HW_AUTOBAUD_ASYNC_FAILED || od_fatal(NULL))
    return AUTOBAUD_OD_CAPTURE_FATAL;

  if (status == HW_AUTOBAUD_MEASURE_VALID &&
      autobaud_v2_baud_supported(measurement->baud_rate) &&
      measurement->confidence_permille >= 750U &&
      measurement->sample_count >= HW_AUTOBAUD_MIN_VALID_INTERVALS)
    return AUTOBAUD_OD_CAPTURE_VALID;

  /* A rejected measurement has restored the original passive listener.
   * Rebase the activity counter after each failed capture, not before: the
   * first master burst must never repeatedly trigger measurement. */
  if (!reset_default_evidence()) {
    od_latch(NULL);
    return AUTOBAUD_OD_CAPTURE_FATAL;
  }
  return AUTOBAUD_OD_CAPTURE_INCONCLUSIVE;
}

static bool od_framing_begin(
    void *u, const hw_autobaud_measurement_t *measurement,
    uint32_t budget, uint32_t dwell)
{
  (void)u;
  if (PyxisAutobaudFraming_Start(measurement, budget, dwell))
    return true;
  if (!od_fatal(NULL) && !reset_default_evidence())
    od_latch(NULL);
  return false;
}

static autobaud_od_frame_t od_framing_poll(
    void *u, autobaud_v2_profile_t *profile)
{
  autobaud_e_status_t status;
  const autobaud_e_ctx_t *framing;
  (void)u;
  status = PyxisAutobaudFraming_Service();
  if (status == AUTOBAUD_E_RUNNING) return AUTOBAUD_OD_FRAME_RUNNING;
  if (status == AUTOBAUD_E_LOCKED) {
    framing = PyxisAutobaudFraming_State();
    if (framing == NULL || profile == NULL ||
        !autobaud_v2_profile_supported(&framing->active))
      return AUTOBAUD_OD_FRAME_FATAL;
    *profile = framing->active;
    return AUTOBAUD_OD_FRAME_LOCKED;
  }
  if (status == AUTOBAUD_E_RESTORE_FAILED ||
      status == AUTOBAUD_E_FATAL_RECOVERY || od_fatal(NULL))
    return AUTOBAUD_OD_FRAME_FATAL;
  return AUTOBAUD_OD_FRAME_NO_LOCK;
}

static const autobaud_od_ops_t k_ops = {
  .now_ms = od_now,
  .activity = od_activity,
  .probe_initial = od_probe,
  .capture_begin = od_capture_begin,
  .capture_poll = od_capture_poll,
  .framing_begin = od_framing_begin,
  .framing_poll = od_framing_poll,
  .fatal = od_fatal,
  .latch_fault = od_latch,
  .user = NULL
};

bool PyxisAutobaudV2_Start(void)
{
  if (s_armed || s_started || ModbusEngineV2_IsFatal())
    return false;
  s_diag = (pyxis_autobaud_v2_diagnostics_t){0};
  s_diag.state = PYXIS_AUTOBAUD_V2_STARTING;
  s_start_ms = ModbusEngineV2_NowMs();
  s_armed = 1U;
  return true; /* Never blocks the Modbus task's regular startup. */
}

void PyxisAutobaudV2_Service(void)
{
  static const autobaud_v2_profile_t initial = {
    PYXIS_AUTOBAUD_V2_INITIAL_BAUD, AUTOBAUD_UART_MODE_8E1
  };
  autobaud_uart_txn_result_t result;
  if (s_armed == 0U ||
      s_diag.state == PYXIS_AUTOBAUD_V2_LOCKED ||
      s_diag.state == PYXIS_AUTOBAUD_V2_FAULT)
    return;

  if (od_fatal(NULL)) {
    od_latch(NULL);
    return;
  }

  if (s_started == 0U) {
    /* The high-priority RX worker may not have completed its first
     * scheduler turn yet. Retry BUSY cooperatively for a bounded 2s,
     * never falling back to a normal responder while detecting. */
    if ((uint32_t)(od_now(NULL) - s_start_ms) >=
        PYXIS_AUTOBAUD_V2_START_BUDGET_MS) {
      od_latch(NULL);
      return;
    }
    ++s_diag.start_attempts;
    if (!PyxisAutobaudUart_Init() ||
        !PyxisAutobaudFraming_Init()) {
      od_latch(NULL);
      return;
    }
    result = PyxisAutobaudUart_Apply(&initial, true);
    if (result == AUTOBAUD_UART_TXN_BUSY) {
      ++s_diag.start_failures;
      return;
    }
    if (result != AUTOBAUD_UART_TXN_APPLIED_VERIFIED ||
        !reset_default_evidence() ||
        !autobaud_od_init(&s_supervisor, &k_ops)) {
      od_latch(NULL);
      return;
    }
    s_started = 1U;
    s_diag.state = PYXIS_AUTOBAUD_V2_LISTENING;
    return;
  }

  autobaud_od_service(&s_supervisor);
  s_diag.engine_state = s_supervisor.state;
  s_diag.active = s_supervisor.active;
  s_diag.locks = s_supervisor.locks;
  s_diag.failures = s_supervisor.failures;
  s_diag.recovery_attempts = s_supervisor.attempts;
  if (s_supervisor.state == AUTOBAUD_OD_LOCKED) {
    s_diag.state = PYXIS_AUTOBAUD_V2_LOCKED;
  } else if (s_supervisor.state == AUTOBAUD_OD_FAULT) {
    od_latch(NULL);
  }
}

bool PyxisAutobaudV2_Locked(void)
{
  return s_armed != 0U && s_started != 0U &&
         s_diag.state == PYXIS_AUTOBAUD_V2_LOCKED &&
         autobaud_od_locked(&s_supervisor) &&
         !od_fatal(NULL);
}

bool PyxisAutobaudV2_IsFatal(void)
{
  return s_diag.state == PYXIS_AUTOBAUD_V2_FAULT ||
         (s_armed != 0U && od_fatal(NULL));
}

void PyxisAutobaudV2_GetDiagnostics(
    pyxis_autobaud_v2_diagnostics_t *out)
{
  if (out != NULL) *out = s_diag;
}
