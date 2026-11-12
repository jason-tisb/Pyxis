/* Hardware-neutral capture result types derived from Titania's capture API.
 * M5 supplies the real Pyxis capture backend.  A baud estimate is not a lock;
 * the passive request validator must verify an intact Modbus request.
 */
#ifndef PYXIS_AUTOBAUD_V2_MEASUREMENT_H
#define PYXIS_AUTOBAUD_V2_MEASUREMENT_H

#include <stdbool.h>
#include <stdint.h>

/* Titania-compatible result identifiers used by the M2-M3 portable engines.
 * No device handles or peripheral details are present in this header. */
typedef enum {
  HW_AUTOBAUD_MEASURE_VALID = 0,
  HW_AUTOBAUD_MEASURE_AMBIGUOUS,
  HW_AUTOBAUD_MEASURE_UNAVAILABLE,
  HW_AUTOBAUD_MEASURE_TIMEOUT,
  HW_AUTOBAUD_MEASURE_ERROR
} hw_autobaud_measure_status_t;

typedef struct {
  uint32_t baud_rate;
  uint16_t confidence_permille;
  uint16_t sample_count;
} hw_autobaud_measurement_t;

#define HW_AUTOBAUD_MIN_VALID_INTERVALS 8U

typedef enum {
  HW_AUTOBAUD_ASYNC_PENDING = 0,
  HW_AUTOBAUD_ASYNC_READY,
  HW_AUTOBAUD_ASYNC_FAILED
} hw_autobaud_async_poll_t;

typedef enum {
  HW_AUTOBAUD_RECOVERY_RELEASED_VERIFIED = 0,
  HW_AUTOBAUD_RECOVERY_CAPTURE_OWNED,
  HW_AUTOBAUD_RECOVERY_RESTORE_FATAL
} hw_autobaud_recovery_state_t;

#endif /* PYXIS_AUTOBAUD_V2_MEASUREMENT_H */
