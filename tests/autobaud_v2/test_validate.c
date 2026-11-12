#include "autobaud_validate.h"
#include "modbus_crc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* This target links ONLY to the read-only validator and CRC implementation.
 * It cannot access the Modbus register engine, RS485, GPIO or output drivers.
 */
static const autobaud_validate_rx_evidence_t k_clean = {
    true, true, true, true
};

static uint16_t make_adu(uint8_t *out, const uint8_t *request, uint16_t n)
{
  uint16_t crc;
  assert(out != NULL && request != NULL);
  assert(n >= 6U && n <= 254U);
  memcpy(out, request, n);
  crc = modbus_crc16(out, n);
  out[n] = (uint8_t)(crc & 0xFFU);
  out[n + 1U] = (uint8_t)(crc >> 8U);
  return (uint16_t)(n + 2U);
}

static autobaud_validate_result_t run(
    const uint8_t *req, uint16_t n, autobaud_validate_request_info_t *out)
{
  uint8_t frame[256];
  const uint16_t len = make_adu(frame, req, n);
  return autobaud_validate_request(frame, len, &k_clean, out);
}

static void test_valid_any_id_fixed_requests(void)
{
  static const uint8_t reqs[][6] = {
      {17U, 0x01U, 0x00U, 0x00U, 0x07U, 0xD0U}, /* 2000 coils */
      {88U, 0x02U, 0x00U, 0x10U, 0x00U, 0x01U},
      {247U, 0x03U, 0x00U, 0x10U, 0x00U, 0x7DU}, /* 125 regs */
      {200U, 0x04U, 0xFFU, 0xFFU, 0x00U, 0x01U},
      {31U, 0x05U, 0x00U, 0x00U, 0xFFU, 0x00U},
      {0U, 0x05U, 0x00U, 0x05U, 0x00U, 0x00U},
      {13U, 0x06U, 0x00U, 0x09U, 0xFEU, 0xB0U},
      {0U, 0x06U, 0x00U, 0x09U, 0xFEU, 0xB0U}
  };

  for (size_t i = 0U; i < sizeof(reqs) / sizeof(reqs[0]); ++i) {
    autobaud_validate_request_info_t info;
    assert(run(reqs[i], 6U, &info) == AUTOBAUD_VALIDATE_ACCEPTED);
    assert(info.slave_id == reqs[i][0]);
    assert(info.function_code == reqs[i][1]);
    assert(info.start_address == (uint16_t)(((uint16_t)reqs[i][2] << 8U) |
                                             reqs[i][3]));
    assert(info.is_broadcast == (reqs[i][0] == 0U));
    if (reqs[i][1] == 0x05U || reqs[i][1] == 0x06U) {
      assert(info.quantity == 0U);
      assert(info.value == (uint16_t)(((uint16_t)reqs[i][4] << 8U) |
                                       reqs[i][5]));
    } else {
      assert(info.value == 0U);
      assert(info.quantity == (uint16_t)(((uint16_t)reqs[i][4] << 8U) |
                                          reqs[i][5]));
    }
  }
}

static void test_multi_write_requests(void)
{
  /* FC0F, 9 coils -> two data bytes */
  static const uint8_t coils[] =
      {55U, 0x0FU, 0x00U, 0x07U, 0x00U, 0x09U, 0x02U, 0xA5U, 0x01U};
  /* FC10, 2 registers -> four data bytes; broadcast */
  static const uint8_t registers[] =
      {0U, 0x10U, 0x00U, 0x07U, 0x00U, 0x02U, 0x04U,
       0x00U, 0x01U, 0xAAU, 0x55U};
  autobaud_validate_request_info_t info;
  uint8_t max_coils[7U + 246U] = {0};
  uint8_t max_regs[7U + 246U] = {0};

  assert(run(coils, sizeof(coils), &info) == AUTOBAUD_VALIDATE_ACCEPTED);
  assert(info.quantity == 9U && info.start_address == 7U);
  assert(run(registers, sizeof(registers), &info) ==
         AUTOBAUD_VALIDATE_ACCEPTED);
  assert(info.is_broadcast && info.quantity == 2U);

  /* FC0F max quantity 1968 -> 246 payload bytes, ADU 255. */
  max_coils[0] = 247U;
  max_coils[1] = 0x0FU;
  max_coils[4] = 0x07U;
  max_coils[5] = 0xB0U;
  max_coils[6] = 246U;
  assert(run(max_coils, sizeof(max_coils), NULL) ==
         AUTOBAUD_VALIDATE_ACCEPTED);

  /* FC10 max quantity 123 -> 246 payload bytes, ADU 255. */
  max_regs[0] = 1U;
  max_regs[1] = 0x10U;
  max_regs[5] = 123U;
  max_regs[6] = 246U;
  assert(run(max_regs, sizeof(max_regs), NULL) ==
         AUTOBAUD_VALIDATE_ACCEPTED);
}

static void test_bad_frames_with_valid_crc(void)
{
  static const uint8_t invalid[][6] = {
      {0U, 0x03U, 0U, 0U, 0U, 1U},       /* broadcast read */
      {0U, 0x01U, 0U, 0U, 0U, 1U},
      {248U, 0x03U, 0U, 0U, 0U, 1U},     /* reserved slave address */
      {255U, 0x03U, 0U, 0U, 0U, 1U},
      {9U, 0x17U, 0U, 0U, 0U, 1U},       /* unsupported function */
      {9U, 0x03U, 0U, 0U, 0U, 0U},       /* zero quantity */
      {9U, 0x03U, 0U, 0U, 0U, 126U},     /* read register qty > 125 */
      {9U, 0x01U, 0U, 0U, 0x07U, 0xD1U},/* coils qty > 2000 */
      {9U, 0x03U, 0xFFU, 0xFFU, 0U, 2U},/* overflow address range */
      {9U, 0x05U, 0U, 0U, 0x12U, 0x34U} /* invalid coil value */
  };
  for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    autobaud_validate_request_info_t info = {.slave_id = 77U};
    const autobaud_validate_result_t result =
        run(invalid[i], 6U, &info);
    assert(result != AUTOBAUD_VALIDATE_ACCEPTED);
    assert(info.slave_id == 0U && info.function_code == 0U);
    assert(info.quantity == 0U && !info.is_broadcast);
  }
  {
    static const uint8_t bad_fc0f[] =
        {1U, 0x0FU, 0U, 0U, 0U, 9U, 1U, 0xFFU}; /* count must be 2 */
    static const uint8_t bad_fc10[] =
        {1U, 0x10U, 0U, 0U, 0U, 2U, 2U, 0U, 1U}; /* count must be 4 */
    static const uint8_t zero_fc10[] =
        {1U, 0x10U, 0U, 0U, 0U, 0U, 0U};
    static const uint8_t excess_fc0f[] =
        {1U, 0x0FU, 0U, 0U, 0x07U, 0xB1U, 247U};
    assert(run(bad_fc0f, sizeof(bad_fc0f), NULL) ==
           AUTOBAUD_VALIDATE_INVALID_STRUCTURE);
    assert(run(bad_fc10, sizeof(bad_fc10), NULL) ==
           AUTOBAUD_VALIDATE_INVALID_STRUCTURE);
    assert(run(zero_fc10, sizeof(zero_fc10), NULL) ==
           AUTOBAUD_VALIDATE_INVALID_STRUCTURE);
    assert(run(excess_fc0f, sizeof(excess_fc0f), NULL) ==
           AUTOBAUD_VALIDATE_INVALID_STRUCTURE);
  }
}

static void test_rx_evidence_and_crc(void)
{
  static const uint8_t req[] = {37U, 3U, 0U, 0U, 0U, 2U};
  uint8_t frame[256] = {0};
  autobaud_validate_request_info_t info;
  const uint16_t n = make_adu(frame, req, sizeof(req));
  autobaud_validate_rx_evidence_t ev = k_clean;

  assert(autobaud_validate_request(frame, n, &ev, &info) ==
         AUTOBAUD_VALIDATE_ACCEPTED);
  frame[2] ^= 1U;
  assert(autobaud_validate_request(frame, n, &ev, &info) ==
         AUTOBAUD_VALIDATE_INVALID_CRC);
  assert(info.slave_id == 0U);
  frame[2] ^= 1U;
  assert(autobaud_validate_request(frame, 7U, &ev, NULL) ==
         AUTOBAUD_VALIDATE_INVALID_LENGTH);
  assert(autobaud_validate_request(frame, 257U, &ev, NULL) ==
         AUTOBAUD_VALIDATE_INVALID_LENGTH);
  assert(autobaud_validate_request(NULL, n, &ev, NULL) ==
         AUTOBAUD_VALIDATE_NULL_ARGUMENT);
  assert(autobaud_validate_request(frame, n, NULL, NULL) ==
         AUTOBAUD_VALIDATE_NULL_ARGUMENT);

  ev.complete_rtu_frame = false;
  assert(autobaud_validate_request(frame, n, &ev, NULL) ==
         AUTOBAUD_VALIDATE_UNTRUSTED_RX);
  ev = k_clean;
  ev.uart_error_free = false;
  assert(autobaud_validate_request(frame, n, &ev, NULL) ==
         AUTOBAUD_VALIDATE_UNTRUSTED_RX);
  ev = k_clean;
  ev.transport_error_free = false;
  assert(autobaud_validate_request(frame, n, &ev, NULL) ==
         AUTOBAUD_VALIDATE_UNTRUSTED_RX);
  ev = k_clean;
  ev.fresh_after_profile_apply = false;
  assert(autobaud_validate_request(frame, n, &ev, NULL) ==
         AUTOBAUD_VALIDATE_UNTRUSTED_RX);
  ev = (autobaud_validate_rx_evidence_t){0};
  assert(autobaud_validate_request(frame, n, &ev, NULL) ==
         AUTOBAUD_VALIDATE_UNTRUSTED_RX);
}

int main(void)
{
  test_valid_any_id_fixed_requests();
  test_multi_write_requests();
  test_bad_frames_with_valid_crc();
  test_rx_evidence_and_crc();
  puts("autobaud_validate_host_tests: PASS");
  return 0;
}
