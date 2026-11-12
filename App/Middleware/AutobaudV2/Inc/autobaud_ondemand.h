/* Titania-compatible M3 on-demand supervisor contract; M1 declarations only.
 * No implementation is linked into the product image at this stage.
 */
#ifndef PYXIS_AUTOBAUD_ONDEMAND_H
#define PYXIS_AUTOBAUD_ONDEMAND_H

#include "autobaud_v2.h"
#include "autobaud_v2_measurement.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  AUTOBAUD_OD_WAIT_ACTIVITY = 0,
  AUTOBAUD_OD_CAPTURING,
  AUTOBAUD_OD_FRAMING,
  AUTOBAUD_OD_COOLDOWN,
  AUTOBAUD_OD_LOCKED,
  AUTOBAUD_OD_FAULT
} autobaud_od_state_t;

typedef enum {
  AUTOBAUD_OD_PROBE_WAITING = 0,
  AUTOBAUD_OD_PROBE_LOCKED,
  AUTOBAUD_OD_PROBE_FAULT
} autobaud_od_probe_result_t;

typedef enum {
  AUTOBAUD_OD_CAPTURE_PENDING = 0,
  AUTOBAUD_OD_CAPTURE_VALID,
  AUTOBAUD_OD_CAPTURE_INCONCLUSIVE,
  AUTOBAUD_OD_CAPTURE_FATAL
} autobaud_od_capture_t;

typedef enum {
  AUTOBAUD_OD_FRAME_RUNNING = 0,
  AUTOBAUD_OD_FRAME_LOCKED,
  AUTOBAUD_OD_FRAME_NO_LOCK,
  AUTOBAUD_OD_FRAME_FATAL
} autobaud_od_frame_t;

/* Callbacks must be invoked from task context, never ISR. Each callback
 * must return after bounded work. The platform supplies UART/DMA ownership
 * and verified receiver restoration; the supervisor never operates hardware.
 */
typedef struct {
  uint32_t (*now_ms)(void *user);
  bool (*activity)(void *user);
  /* Optional passive 9600/8E1 CRC-correct first-request fast lock.
   * This must consume, not execute, the detection frame. */
  autobaud_od_probe_result_t (*probe_initial)(
      void *user, autobaud_v2_profile_t *profile);
  bool (*capture_begin)(void *user, uint32_t timeout_ms);
  autobaud_od_capture_t (*capture_poll)(
      void *user, hw_autobaud_measurement_t *out);
  bool (*framing_begin)(void *user,
                        const hw_autobaud_measurement_t *measurement,
                        uint32_t budget_ms, uint32_t dwell_ms);
  autobaud_od_frame_t (*framing_poll)(
      void *user, autobaud_v2_profile_t *verified_profile);
  bool (*fatal)(void *user);
  void (*latch_fault)(void *user);
  void *user;
} autobaud_od_ops_t;

typedef struct {
  autobaud_od_ops_t ops;
  autobaud_od_state_t state;
  autobaud_v2_profile_t active;
  uint32_t entered_ms;
  uint32_t attempts;
  uint32_t failures;
  uint32_t locks;
  bool initialized;
} autobaud_od_t;

/* Policy intentionally matches Titania's H-SW on-demand supervisor.
 * Measured time begins only after qualified activity, never at MCU boot. */
#define AUTOBAUD_OD_CAPTURE_MS       (12000U)
#define AUTOBAUD_OD_FRAMING_MS       (52000U)
#define AUTOBAUD_OD_FRAMING_DWELL_MS (12000U)
#define AUTOBAUD_OD_COOLDOWN_MS      (1000U)

/* Implementations imported/tested in M3. */
bool autobaud_od_init(autobaud_od_t *ctx, const autobaud_od_ops_t *ops);
void autobaud_od_service(autobaud_od_t *ctx);
bool autobaud_od_locked(const autobaud_od_t *ctx);

#endif /* PYXIS_AUTOBAUD_ONDEMAND_H */
