#ifndef INC_AUTOBAUD_TIMESTAMP32_H_
#define INC_AUTOBAUD_TIMESTAMP32_H_

/* Optional 16-bit hardware capture extension; Pyxis TIM2 is 32-bit.\n * Preserved for portable engine integrity/reference coverage.\n * 16-bit capture extended by verified update events.
 * Pure C: no MCU, HAL or GPIO register dependency. Fail closed on uncertainty.
 */
#include <stdbool.h>
#include <stdint.h>

#define AUTOBAUD_TS32_WRAP_TICKS              65536UL
#define AUTOBAUD_TS32_MAX_CAPTURE_LAG_TICKS     512UL
#define AUTOBAUD_TS32_MAX_UPDATE_LAG_TICKS      512UL
#define AUTOBAUD_TS32_REFERENCE_TOLERANCE_TICKS       512UL
#define AUTOBAUD_TS32_MAX_ATTEMPT_TICKS    16000000UL

typedef enum {
  AUTOBAUD_TS32_OK = 0,
  AUTOBAUD_TS32_OVERCAPTURE,
  AUTOBAUD_TS32_AMBIGUOUS_EPOCH,
  AUTOBAUD_TS32_LATE_INTERRUPT,
  AUTOBAUD_TS32_DISCONTINUITY,
  AUTOBAUD_TS32_REFERENCE_FAULT,
  AUTOBAUD_TS32_CONTROL_FAULT
} autobaud_ts32_fault_t;

typedef struct {
  uint32_t epoch;
  uint32_t last_capture;
  uint32_t update_events;
  uint32_t captured_edges;
  bool have_previous;
  autobaud_ts32_fault_t fault;
} autobaud_ts32_state_t;

typedef struct {
  bool update_pending;
  bool capture_pending;
  bool overcapture;
  uint16_t captured_count;
  uint16_t counter;
} autobaud_ts32_event_t;

typedef struct {
  bool captured;
  bool has_interval;
  uint32_t timestamp;
  uint32_t interval;
} autobaud_ts32_result_t;

void autobaud_ts32_reset(autobaud_ts32_state_t *state);

autobaud_ts32_fault_t autobaud_ts32_process(
    autobaud_ts32_state_t *state, const autobaud_ts32_event_t *event,
    autobaud_ts32_result_t *result);

/* An update-pending indication represents at most one unaccounted wrap.
 * Reject a snapshot if multiple wraps may have been missed. Verify against
 * an independent running reference counter before trusting the timestamp.
 */
bool autobaud_ts32_snapshot(const autobaud_ts32_state_t *state,
                            uint16_t counter, bool update_pending,
                            uint32_t *timestamp);

/* Compare elapsed reference-counter counts with elapsed capture ticks.
 * Inputs are modulo-32-bit elapsed durations, bounded to <1.6 s.
 * Use 64-bit conversion; the reference is an integrity cross-check, never
 * the source of input edge timestamps or proof of oscillator independence.
 */
bool autobaud_ts32_reference_consistent(uint32_t reference_elapsed,
                                          uint32_t timer_elapsed_ticks,
                                          uint32_t reference_hz, uint32_t timer_hz,
                                  uint32_t tolerance_ticks,
                                  uint32_t *error_ticks);

#endif
