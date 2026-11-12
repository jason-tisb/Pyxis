/* Titania Unified Autobaud V2 - Milestone B passive RTU request validator.
 *
 * Pure, read-only parser. No HAL, UART, Modbus register, or TX operations.
 * The eventual coordinator must pass a NEW, completely assembled RTU frame
 * from the active UART profile and positively establish RX integrity.
 * Merely passing a CRC does not prove parity or exact stop-bit configuration.
 */
#ifndef INC_AUTOBAUD_VALIDATE_H_
#define INC_AUTOBAUD_VALIDATE_H_

#include <stdbool.h>
#include <stdint.h>

#define AUTOBAUD_VALIDATE_MIN_ADU_LENGTH 8U
#define AUTOBAUD_VALIDATE_MAX_ADU_LENGTH 256U

typedef enum {
  AUTOBAUD_VALIDATE_ACCEPTED = 0,
  AUTOBAUD_VALIDATE_NULL_ARGUMENT,
  AUTOBAUD_VALIDATE_UNTRUSTED_RX,
  AUTOBAUD_VALIDATE_INVALID_LENGTH,
  AUTOBAUD_VALIDATE_INVALID_CRC,
  AUTOBAUD_VALIDATE_INVALID_SLAVE_ADDRESS,
  AUTOBAUD_VALIDATE_UNSUPPORTED_FUNCTION,
  AUTOBAUD_VALIDATE_INVALID_STRUCTURE
} autobaud_validate_result_t;

/* All four positive conditions are REQUIRED.
 * In Milestone E, the coordinator must derive these from RTU frame assembly,
 * UART error flags/counters, DMA/RTO state, and the active RX session.
 * Never pass optimistic defaults when evidence is unavailable.
 */
typedef struct {
  bool complete_rtu_frame;
  bool uart_error_free;
  bool transport_error_free;
  bool fresh_after_profile_apply;
} autobaud_validate_rx_evidence_t;

typedef struct {
  uint8_t slave_id;
  uint8_t function_code;
  uint16_t start_address;
  uint16_t quantity;       /* Read/multi-write count; zero for FC05/FC06. */
  uint16_t value;          /* FC05/FC06 value; zero otherwise. */
  bool is_broadcast;
} autobaud_validate_request_info_t;

/* Does NOT filter against Pyxis's ID, interpret the register map, transmit,
 * or invoke any Modbus application callbacks. Only recognized, structurally
 * valid requests (FC01/02/03/04/05/06/0F/10) are eligible lock evidence.
 * out_info is optional; on rejection it is cleared when supplied.
 */
autobaud_validate_result_t autobaud_validate_request(
    const uint8_t *adu, uint16_t adu_len,
    const autobaud_validate_rx_evidence_t *evidence,
    autobaud_validate_request_info_t *out_info);

#endif /* INC_AUTOBAUD_VALIDATE_H_ */
