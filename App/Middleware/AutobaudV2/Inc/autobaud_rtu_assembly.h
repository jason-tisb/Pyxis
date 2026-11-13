/* Pyxis M8: hardware-neutral Modbus RTU candidate fragment assembly.
 * A UART IDLE callback is NOT an RTU frame delimiter (it can occur after
 * one character of silence). Only ≥3.5-char silence establishes a boundary.
 * Single RX-worker producer; consume/reset only while worker owns stream.
 */
#ifndef PYXIS_AUTOBAUD_RTU_ASSEMBLY_H
#define PYXIS_AUTOBAUD_RTU_ASSEMBLY_H

#include <stdbool.h>
#include <stdint.h>

#define AUTOBAUD_RTU_MAX_FRAME 256U

typedef struct {
  uint8_t bytes[AUTOBAUD_RTU_MAX_FRAME];
  uint16_t length;
  uint32_t last_ms;
} autobaud_rtu_assembly_t;

typedef enum {
  AUTOBAUD_RTU_APPEND_OK = 0,
  AUTOBAUD_RTU_APPEND_NEEDS_FLUSH,
  AUTOBAUD_RTU_APPEND_OVERFLOW,
  AUTOBAUD_RTU_APPEND_INVALID
} autobaud_rtu_append_result_t;

/* Returns a whole-millisecond conservative Modbus T3.5 threshold:
 * 3.5 chars at rates <= 19200; 1.75 ms (ceil=2ms) above 19200.
 * Valid only for one start bit, 8 data bits, and zero/one parity bit.
 */
uint32_t autobaud_rtu_quiet_ms(uint32_t baud, bool parity,
                               bool two_stop_bits);
void autobaud_rtu_assembly_reset(autobaud_rtu_assembly_t *state);
bool autobaud_rtu_assembly_ready(const autobaud_rtu_assembly_t *state,
                                 uint32_t now_ms, uint32_t quiet_ms);
autobaud_rtu_append_result_t autobaud_rtu_assembly_append(
    autobaud_rtu_assembly_t *state,
    const uint8_t *data, uint16_t length, uint32_t ended_ms,
    uint32_t quiet_ms);
bool autobaud_rtu_assembly_take(autobaud_rtu_assembly_t *state,
                                uint8_t *out, uint16_t capacity,
                                uint16_t *length);
#endif
