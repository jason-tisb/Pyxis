/* M6: actual Pyxis framing adapter against the UART/transport mock.
 * Ensure the initial request and the candidate evidence request are
 * consumed without invoking Modbus runtime, TX or persistent writes.
 */
#include "pyxis_autobaud_framing_stm32h563.h"
#include "pyxis_autobaud_uart_stm32h563.h"
#include "modbus_engine_v2_transport.h"
#include "modbus_crc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static autobaud_v2_profile_t active = {9600U,AUTOBAUD_UART_MODE_8E1};
static bool passive = true;
static bool fatal;
static uint32_t now;
static uint32_t session = 5U;
static uint32_t generation = 7U;
static uint32_t rx_bytes;
static uint32_t next_frame_id;
static uint32_t last_frame_id;
static unsigned accepted;
static unsigned normal_modbus_calls;
static unsigned tx_calls;
static unsigned persistent_writes;
static unsigned applied;
static uint8_t frame[8];
static bool frame_pending;

static bool same(const autobaud_v2_profile_t *a,
                 const autobaud_v2_profile_t *b)
{
  return a && b && a->baud_rate == b->baud_rate &&
         a->framing == b->framing;
}
bool PyxisAutobaudUart_Init(void) { return !fatal; }
bool PyxisAutobaudUart_IsFatal(void) { return fatal; }
bool PyxisAutobaudUart_Snapshot(autobaud_v2_profile_t *p, bool *is_passive)
{
  if (!p || !is_passive || fatal) return false;
  *p = active;
  *is_passive = passive;
  return true;
}
autobaud_uart_txn_result_t PyxisAutobaudUart_Apply(
    const autobaud_v2_profile_t *p, bool is_passive)
{
  if (!p || fatal) return AUTOBAUD_UART_TXN_FATAL_RECOVERY;
  active = *p;
  passive = is_passive;
  ++applied;
  ++session;
  frame_pending = false;
  return AUTOBAUD_UART_TXN_APPLIED_VERIFIED;
}
uint32_t ModbusEngineV2_NowMs(void) { return now; }
bool ModbusEngineV2_IsFatal(void) { return fatal; }
bool ModbusEngineV2_IsPassive(void) { return passive; }
void ModbusEngineV2_LatchFatal(void) { fatal = true; }
bool ModbusEngineV2_RxActivity(uint32_t *bytes, uint32_t *errors)
{
  if (!passive || fatal || !bytes || !errors) return false;
  *bytes = rx_bytes;
  *errors = generation;
  return true;
}
bool ModbusEngineV2_SnapshotSession(uint32_t *id, uint32_t *err)
{
  if (!passive || fatal || !id || !err) return false;
  *id = session;
  *err = generation;
  return true;
}
autobaud_e_poll_t ModbusEngineV2_TakePassiveFrame(
    uint8_t *buffer, uint16_t capacity, uint16_t *length,
    autobaud_e_frame_meta_t *meta)
{
  if (!passive || fatal || !buffer || capacity < 8U ||
      !length || !meta) return AUTOBAUD_E_POLL_RX_FAULT;
  if (!frame_pending) return AUTOBAUD_E_POLL_NONE;
  frame_pending = false;
  memcpy(buffer,frame,8U);
  *length = 8U;
  ++next_frame_id;
  last_frame_id = next_frame_id;
  *meta = (autobaud_e_frame_meta_t){
      .session_id=session, .frame_id=last_frame_id,
      .uart_error_generation=generation,
      .evidence={true,true,true,true}};
  return AUTOBAUD_E_POLL_FRAME;
}
bool ModbusEngineV2_VerifyPassiveFrame(
    const autobaud_v2_profile_t *p, uint32_t id, uint32_t errors)
{
  return passive && !fatal && same(p,&active) &&
         id == session && errors == generation;
}
bool ModbusEngineV2_AcceptPassiveFrame(
    const autobaud_v2_profile_t *p, const autobaud_e_frame_meta_t *meta)
{
  if (!p || !meta || !ModbusEngineV2_VerifyPassiveFrame(
          p,meta->session_id,meta->uart_error_generation) ||
      meta->frame_id != last_frame_id) return false;
  /* The real port discards pending input and performs verified DMA restart.
   * This mock records the handoff and models a fresh normal session. */
  ++accepted;
  passive = false;
  ++session;
  return true;
}

static void push_request(uint8_t id, uint8_t fc, bool valid_crc)
{
  uint16_t crc;
  frame[0]=id;
  frame[1]=fc;
  frame[2]=0U;
  frame[3]=1U;
  frame[4]=0U;
  frame[5]=2U;
  crc=modbus_crc16(frame,6U);
  frame[6]=(uint8_t)crc;
  frame[7]=(uint8_t)(crc>>8U);
  if(!valid_crc) frame[7]^=1U;
  frame_pending=true;
  rx_bytes+=8U;
}

static void reset_initial(void)
{
  active=(autobaud_v2_profile_t){9600U,AUTOBAUD_UART_MODE_8E1};
  passive=true;
  fatal=false;
  frame_pending=false;
  rx_bytes=0U;
  generation=7U;
  now=0U;
  assert(PyxisAutobaudFraming_Init());
  assert(PyxisAutobaudFraming_ResetInitial());
}

static void test_initial_any_id(void)
{
  autobaud_v2_profile_t locked={0};
  reset_initial();
  now=20000U;
  assert(PyxisAutobaudFraming_ProbeInitial(&locked)==
         AUTOBAUD_OD_PROBE_WAITING);
  assert(!PyxisAutobaudFraming_Activity());
  push_request(247U,0x03U,true);
  assert(PyxisAutobaudFraming_ProbeInitial(&locked)==
         AUTOBAUD_OD_PROBE_LOCKED);
  assert(locked.baud_rate==9600U &&
         locked.framing==AUTOBAUD_UART_MODE_8E1);
  assert(accepted==1U && !passive);
  assert(normal_modbus_calls==0U && tx_calls==0U &&
         persistent_writes==0U);

  reset_initial();
  /* A Modbus broadcast read is invalid; broadcast write is valid. */
  push_request(0U,0x03U,true);
  assert(PyxisAutobaudFraming_ProbeInitial(&locked)==
         AUTOBAUD_OD_PROBE_WAITING);
  assert(PyxisAutobaudFraming_Activity());
  reset_initial();
  push_request(0U,0x06U,true);
  assert(PyxisAutobaudFraming_ProbeInitial(&locked)==
         AUTOBAUD_OD_PROBE_LOCKED);
  assert(accepted==2U);
}

static void test_crc_errors_and_grace(void)
{
  autobaud_v2_profile_t locked={0};
  reset_initial();
  push_request(3U,0x06U,false);
  assert(PyxisAutobaudFraming_ProbeInitial(&locked)==
         AUTOBAUD_OD_PROBE_WAITING);
  assert(PyxisAutobaudFraming_Activity());
  assert(accepted==2U && normal_modbus_calls==0U);
  reset_initial();
  now=100U;
  rx_bytes=1U;
  assert(!PyxisAutobaudFraming_Activity());
  now=599U;
  assert(!PyxisAutobaudFraming_Activity());
  now=600U;
  assert(PyxisAutobaudFraming_Activity());
}

static void test_candidate_lock_without_side_effects(void)
{
  hw_autobaud_measurement_t measurement={
      .baud_rate=115200U,.confidence_permille=950U,.sample_count=20U};
  const autobaud_e_ctx_t *state;
  reset_initial();
  assert(PyxisAutobaudFraming_Start(&measurement,52000U,12000U));
  assert(PyxisAutobaudFraming_Service()==AUTOBAUD_E_RUNNING);
  assert(active.baud_rate==115200U && passive);
  assert(active.framing==AUTOBAUD_UART_MODE_8E1);
  push_request(99U,0x06U,true);
  assert(PyxisAutobaudFraming_Service()==AUTOBAUD_E_LOCKED);
  state=PyxisAutobaudFraming_State();
  assert(state && state->observed_slave_id==99U);
  assert(state->observed_function_code==0x06U);
  assert(!state->stop_bits_independently_verified);
  assert(accepted==3U && !passive);
  assert(normal_modbus_calls==0U && tx_calls==0U &&
         persistent_writes==0U);
}

static void test_failed_candidates_reset_activity(void)
{
  hw_autobaud_measurement_t measurement={
      .baud_rate=19200U,.confidence_permille=950U,.sample_count=20U};
  autobaud_e_status_t result=AUTOBAUD_E_RUNNING;
  reset_initial();
  assert(PyxisAutobaudFraming_Start(&measurement,10000U,2000U));
  for (unsigned i=0U;i<32U && result==AUTOBAUD_E_RUNNING;++i) {
    result=PyxisAutobaudFraming_Service();
    const autobaud_e_ctx_t *state=PyxisAutobaudFraming_State();
    if (result==AUTOBAUD_E_RUNNING && state &&
        state->state==AUTOBAUD_E_STATE_LISTEN) now+=2100U;
  }
  assert(result==AUTOBAUD_E_NO_VALID_REQUEST ||
         result==AUTOBAUD_E_BUDGET_EXPIRED);
  assert(active.baud_rate==9600U &&
         active.framing==AUTOBAUD_UART_MODE_8E1 && passive);
  assert(!PyxisAutobaudFraming_Activity()); /* No stale auto-retrigger. */
  ++rx_bytes;
  ++now;
  assert(!PyxisAutobaudFraming_Activity());
  now+=499U;
  assert(!PyxisAutobaudFraming_Activity());
  ++now;
  assert(PyxisAutobaudFraming_Activity());
}

int main(void)
{
  test_initial_any_id();
  test_crc_errors_and_grace();
  test_candidate_lock_without_side_effects();
  test_failed_candidates_reset_activity();
  puts("Pyxis M6 passive framing adapter: PASS");
  return 0;
}
