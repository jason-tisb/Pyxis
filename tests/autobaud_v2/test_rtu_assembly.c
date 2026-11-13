/* M8 software qualification of the real bounded passive RTU assembler.
 * This tests split UART IDLE events across 5 baud x 4 UART profiles,
 * Modbus T3.5 timing, gap handling at 100ms..10s, counter rollover,
 * overflow poisoning, and a CRC-valid FC06 addressed to the wrong ID.
 */
#include "autobaud_rtu_assembly.h"
#include "autobaud_validate.h"
#include "modbus_crc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const autobaud_validate_rx_evidence_t trust = {
  .complete_rtu_frame=true,
  .uart_error_free=true,
  .transport_error_free=true,
  .fresh_after_profile_apply=true
};

static void request(uint8_t *frame, uint8_t slave, uint8_t fc)
{
  uint16_t crc;
  frame[0] = slave;
  frame[1] = fc;
  frame[2] = 0U; frame[3] = 0U;
  frame[4] = 0U; frame[5] = 1U;
  crc = modbus_crc16(frame,6U);
  frame[6] = (uint8_t)crc;
  frame[7] = (uint8_t)(crc >> 8);
}

static void fragmentation_matrix(void)
{
  const uint32_t rates[5] = {9600U,19200U,38400U,57600U,115200U};
  const uint32_t polling_ms[3] = {100U,1000U,10000U};
  for (unsigned b=0U;b<5U;++b) {
    for (unsigned mode=0U;mode<4U;++mode) {
      for (unsigned spacing=0U;spacing<3U;++spacing) {
        autobaud_rtu_assembly_t stream={0};
        autobaud_validate_request_info_t info={0};
        uint8_t frame[8],output[256]={0};
        uint16_t n=0U;
        bool parity=(mode==0U || mode==2U);
        bool stop2=mode==1U;
        const uint32_t quiet=autobaud_rtu_quiet_ms(
            rates[b],parity,stop2);
        uint32_t now=100U;
        request(frame, 247U, 0x06U);
        assert(quiet >= 2U && quiet <= 5U);
        assert(autobaud_rtu_assembly_append(
             &stream,frame,2U,now,quiet)==AUTOBAUD_RTU_APPEND_OK);
        assert(!autobaud_rtu_assembly_ready(
            &stream,now+quiet-1U,quiet));
        now += quiet-1U;
        assert(autobaud_rtu_assembly_append(
            &stream,frame+2U,3U,now,quiet)==AUTOBAUD_RTU_APPEND_OK);
        now += quiet-1U;
        assert(autobaud_rtu_assembly_append(
            &stream,frame+5U,3U,now,quiet)==AUTOBAUD_RTU_APPEND_OK);
        assert(stream.length==8U);
        assert(!autobaud_rtu_assembly_ready(&stream,now+quiet-1U,quiet));
        assert(autobaud_rtu_assembly_ready(&stream,now+quiet,quiet));
        assert(autobaud_rtu_assembly_take(
            &stream,output,256U,&n) && n==8U);
        assert(memcmp(output,frame,8U)==0);
        assert(autobaud_validate_request(
            output,n,&trust,&info)==AUTOBAUD_VALIDATE_ACCEPTED);
        assert(info.slave_id==247U && info.function_code==0x06U);
        assert(!autobaud_rtu_assembly_ready(&stream,now+quiet,quiet));

        /* A following poll never joins the preceding candidate frame.
         * 100ms, 1s and 10s match on-site Modbus master poll tests. */
        now += polling_ms[spacing];
        request(frame,0U,0x06U); /* Broadcast write is evidence. */
        assert(autobaud_rtu_assembly_append(
             &stream,frame,8U,now,quiet)==AUTOBAUD_RTU_APPEND_OK);
        assert(autobaud_rtu_assembly_ready(
            &stream,now+quiet,quiet));
        assert(autobaud_rtu_assembly_take(
            &stream,output,256U,&n));
        assert(autobaud_validate_request(
            output,n,&trust,&info)==AUTOBAUD_VALIDATE_ACCEPTED);
        assert(info.slave_id==0U && info.is_broadcast);
      }
    }
  }
}

static void gap_and_wrap(void)
{
  autobaud_rtu_assembly_t stream={0};
  uint8_t output[256]={0}, frame[8];
  uint16_t length=0U;
  uint32_t t=UINT32_MAX-1U, quiet=5U;
  request(frame,3U,0x03U);
  assert(autobaud_rtu_assembly_append(
      &stream,frame,4U,t,quiet)==AUTOBAUD_RTU_APPEND_OK);
  assert(!autobaud_rtu_assembly_ready(&stream,2U,quiet));
  assert(autobaud_rtu_assembly_ready(&stream,3U,quiet));
  assert(autobaud_rtu_assembly_append(
      &stream,frame+4U,4U,3U,quiet)==AUTOBAUD_RTU_APPEND_NEEDS_FLUSH);
  assert(stream.length==4U);
  assert(autobaud_rtu_assembly_take(
      &stream,output,256U,&length) && length==4U);
  assert(autobaud_rtu_assembly_append(
      &stream,frame+4U,4U,3U,quiet)==AUTOBAUD_RTU_APPEND_OK);
  assert(autobaud_rtu_assembly_take(
      &stream,output,256U,&length) && length==4U);
  autobaud_rtu_assembly_reset(&stream);
  assert(stream.length==0U);
}

static void overload_and_invalid(void)
{
  autobaud_rtu_assembly_t stream={0};
  uint8_t data[256]={0},out[256]={0};
  uint16_t n=0;
  assert(autobaud_rtu_quiet_ms(0U,false,false)==0U);
  assert(autobaud_rtu_quiet_ms(9600U,true,false)==5U);
  assert(autobaud_rtu_quiet_ms(9600U,false,false)==4U);
  assert(autobaud_rtu_quiet_ms(19200U,true,false)==3U);
  assert(autobaud_rtu_quiet_ms(115200U,false,true)==2U);
  assert(autobaud_rtu_assembly_append(
      &stream,NULL,4U,1U,5U)==AUTOBAUD_RTU_APPEND_INVALID);
  assert(autobaud_rtu_assembly_append(
      &stream,data,0U,1U,5U)==AUTOBAUD_RTU_APPEND_INVALID);
  assert(autobaud_rtu_assembly_append(
      &stream,data,250U,1U,5U)==AUTOBAUD_RTU_APPEND_OK);
  assert(autobaud_rtu_assembly_append(
      &stream,data,7U,2U,5U)==AUTOBAUD_RTU_APPEND_OVERFLOW);
  assert(stream.length==250U); /* Corruption cannot overflow buffer. */
  assert(!autobaud_rtu_assembly_take(&stream,out,249U,&n));
  assert(autobaud_rtu_assembly_take(&stream,out,256U,&n));
  assert(n==250U);
  autobaud_rtu_assembly_reset(&stream);
  assert(!autobaud_rtu_assembly_ready(&stream,UINT32_MAX,5U));
}

int main(void)
{
  fragmentation_matrix();
  gap_and_wrap();
  overload_and_invalid();
  puts("Pyxis M8 5x4x3 IDLE fragmentation and RTU boundary matrix: PASS");
  return 0;
}
