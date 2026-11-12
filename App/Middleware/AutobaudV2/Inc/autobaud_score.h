#ifndef INC_AUTOBAUD_SCORE_H_
#define INC_AUTOBAUD_SCORE_H_
#include "autobaud_v2_measurement.h"
#include <stdbool.h>
#include <stdint.h>
/* Pure timer-interval scorer, including full acceptance and runner-up checks.
 * Confidence is a heuristic score, not a statistical probability.
 */
hw_autobaud_measure_status_t autobaud_score_intervals(
    const uint32_t *intervals, uint16_t count, uint32_t timer_hz,
    hw_autobaud_measurement_t *result);
/* With a pending TIM3 UPDATE flag, 16-bit modulo subtraction could conceal
 * one or more full timer wraps. Reject that interval conservatively.
 */
bool autobaud_capture_delta16(uint16_t prev, uint16_t now,
                              bool update_seen, uint16_t *delta);
#endif
