/* Titania-compatible initial 500 ms 9600/8E1 observation policy.
 * Pure logic: no UART, DMA, HAL or Modbus side effects. The adapter must
 * first attempt passive CRC validation of complete current-profile frames.
 */
#ifndef PYXIS_AUTOBAUD_INITIAL_WINDOW_H
#define PYXIS_AUTOBAUD_INITIAL_WINDOW_H

#include "autobaud_activity.h"
#include <stdbool.h>
#include <stdint.h>

#define AUTOBAUD_INITIAL_GRACE_MS 500U

typedef struct {
  autobaud_activity_t error_activity;
  uint32_t baseline_bytes;
  uint32_t baseline_errors;
  uint32_t first_rx_ms;
  bool first_rx_seen;
  bool invalid_frame_seen;
} autobaud_initial_window_t;

void autobaud_initial_window_reset(autobaud_initial_window_t *window,
                                   uint32_t total_bytes,
                                   uint32_t total_errors);
void autobaud_initial_window_invalid_frame(autobaud_initial_window_t *window);
bool autobaud_initial_window_trigger(autobaud_initial_window_t *window,
                                     uint32_t now_ms,
                                     uint32_t total_bytes,
                                     uint32_t total_errors);

#endif
