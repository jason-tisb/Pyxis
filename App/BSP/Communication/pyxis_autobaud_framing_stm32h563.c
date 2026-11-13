/* Pyxis M6: side-effect-free initial probe plus Titania E passive framing.
 * Hardware-specific operations are owned by the M4 Modbus transport and
 * UART transaction adapter. This file deliberately links neither Modbus
 * request processing nor response generation nor persistent register APIs.
 */
#include "pyxis_autobaud_framing_stm32h563.h"
#include "pyxis_autobaud_uart_stm32h563.h"
#include "autobaud_initial_window.h"
#include "autobaud_v2_candidate.h"
#include "modbus_engine_v2_transport.h"
#include <stddef.h>

static autobaud_e_ctx_t s_e;
static autobaud_initial_window_t s_initial;
static bool s_ready;
static bool s_initial_ready;
static bool s_started;

static uint32_t e_now(void *user)
{
  (void)user;
  return ModbusEngineV2_NowMs();
}

static bool e_fatal(void *user)
{
  (void)user;
  return PyxisAutobaudUart_IsFatal() || ModbusEngineV2_IsFatal();
}

static bool e_snapshot(void *user, autobaud_v2_profile_t *profile,
                       bool *passive)
{
  (void)user;
  return PyxisAutobaudUart_Snapshot(profile, passive);
}

static autobaud_uart_txn_result_t e_apply(
    void *user, const autobaud_v2_profile_t *profile, bool passive)
{
  (void)user;
  return PyxisAutobaudUart_Apply(profile, passive);
}

static bool e_get_session(void *user, uint32_t *session_id)
{
  uint32_t errors = 0U;
  (void)user;
  return ModbusEngineV2_SnapshotSession(session_id, &errors);
}

static autobaud_e_poll_t e_take(
    void *user, uint8_t *out, uint16_t capacity, uint16_t *length,
    autobaud_e_frame_meta_t *meta)
{
  (void)user;
  return ModbusEngineV2_TakePassiveFrame(out, capacity, length, meta);
}

static bool e_verify(void *user, const autobaud_v2_profile_t *profile,
                     uint32_t session_id, uint32_t error_generation)
{
  (void)user;
  return ModbusEngineV2_VerifyPassiveFrame(profile, session_id,
                                             error_generation);
}

static bool e_accept(void *user, const autobaud_v2_profile_t *profile,
                     const autobaud_e_frame_meta_t *meta)
{
  (void)user;
  return ModbusEngineV2_AcceptPassiveFrame(profile, meta);
}

static void e_latch(void *user)
{
  (void)user;
  ModbusEngineV2_LatchFatal();
}

static const autobaud_e_ops_t k_e_ops = {
  .now_ms = e_now,
  .fatal = e_fatal,
  .snapshot = e_snapshot,
  .apply = e_apply,
  .get_session = e_get_session,
  .take_frame = e_take,
  .verify = e_verify,
  .accept = e_accept,
  .latch_fatal = e_latch,
  .user = NULL
};

bool PyxisAutobaudFraming_Init(void)
{
  if (s_ready) return !e_fatal(NULL);
  if (!PyxisAutobaudUart_Init() || e_fatal(NULL)) return false;
  s_ready = true;
  s_initial_ready = false;
  s_started = false;
  return true;
}

bool PyxisAutobaudFraming_ResetInitial(void)
{
  autobaud_v2_profile_t profile = {0};
  uint32_t bytes = 0U, errors = 0U;
  bool passive = false;
  s_initial_ready = false;
  if (!s_ready || e_fatal(NULL) ||
      !PyxisAutobaudUart_Snapshot(&profile, &passive) || !passive ||
      profile.baud_rate != 9600U ||
      profile.framing != AUTOBAUD_UART_MODE_8E1 ||
      !ModbusEngineV2_RxActivity(&bytes, &errors)) return false;
  autobaud_initial_window_reset(&s_initial, bytes, errors);
  s_initial_ready = true;
  return true;
}

static bool initial_deadline(void *user)
{
  (void)user;
  /* Initial fast-lock is accepted as soon as a full trusted request
   * arrives. The 500 ms grace applies only to falling back to capture. */
  return s_initial_ready && !e_fatal(NULL);
}

autobaud_od_probe_result_t PyxisAutobaudFraming_ProbeInitial(
    autobaud_v2_profile_t *locked_profile)
{
  autobaud_candidate_observation_t observed = {0};
  autobaud_candidate_status_t status;
  autobaud_v2_profile_t current = {0};
  bool passive = false;
  uint32_t session = 0U;
  if (locked_profile == NULL || !s_initial_ready || e_fatal(NULL))
    return AUTOBAUD_OD_PROBE_FAULT;
  if (!PyxisAutobaudUart_Snapshot(&current, &passive) || !passive ||
      current.baud_rate != 9600U ||
      current.framing != AUTOBAUD_UART_MODE_8E1 ||
      !e_get_session(NULL, &session) || session == 0U)
    return AUTOBAUD_OD_PROBE_FAULT;

  status = autobaud_candidate_observe(&k_e_ops, session, &observed);
  if (status == AUTOBAUD_CANDIDATE_WAIT)
    return AUTOBAUD_OD_PROBE_WAITING;
  if (status == AUTOBAUD_CANDIDATE_READY &&
      autobaud_candidate_commit(&k_e_ops, &current, &observed,
                                initial_deadline, NULL)) {
    *locked_profile = current;
    s_initial_ready = false;
    return AUTOBAUD_OD_PROBE_LOCKED;
  }
  if (e_fatal(NULL)) return AUTOBAUD_OD_PROBE_FAULT;
  autobaud_initial_window_invalid_frame(&s_initial);
  return AUTOBAUD_OD_PROBE_WAITING;
}

bool PyxisAutobaudFraming_Activity(void)
{
  uint32_t bytes = 0U, errors = 0U;
  if (!s_initial_ready || e_fatal(NULL) ||
      !ModbusEngineV2_RxActivity(&bytes, &errors)) return false;
  return autobaud_initial_window_trigger(
      &s_initial, ModbusEngineV2_NowMs(), bytes, errors);
}

bool PyxisAutobaudFraming_Start(
    const hw_autobaud_measurement_t *measurement,
    uint32_t budget_ms, uint32_t dwell_ms)
{
  if (!s_ready || e_fatal(NULL) ||
      (s_started && (s_e.state == AUTOBAUD_E_STATE_APPLY ||
                     s_e.state == AUTOBAUD_E_STATE_LISTEN)))
    return false;
  s_started = autobaud_e_start_dwell(
      &s_e, &k_e_ops, measurement, HW_AUTOBAUD_MEASURE_VALID,
      budget_ms, dwell_ms);
  return s_started;
}

autobaud_e_status_t PyxisAutobaudFraming_Service(void)
{
  if (!s_ready || !s_started) return AUTOBAUD_E_INVALID_MEASUREMENT;
  return autobaud_e_service(&s_e);
}

const autobaud_e_ctx_t *PyxisAutobaudFraming_State(void)
{
  return s_started ? &s_e : NULL;
}

bool PyxisAutobaudFraming_IsFaulted(void)
{
  return e_fatal(NULL) ||
         (s_started && (s_e.status == AUTOBAUD_E_FATAL_RECOVERY ||
                        s_e.status == AUTOBAUD_E_RESTORE_FAILED));
}
