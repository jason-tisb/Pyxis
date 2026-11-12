#ifndef INC_AUTOBAUD_V2_EVIDENCE_H_
#define INC_AUTOBAUD_V2_EVIDENCE_H_

#include "autobaud_validate.h"
#include <stdbool.h>
#include <stdint.h>

/* Immutable, copied frame metadata; not a borrowed DMA buffer. */
typedef enum {
  AUTOBAUD_E_POLL_NONE = 0,
  AUTOBAUD_E_POLL_FRAME,
  AUTOBAUD_E_POLL_BAD_FRAME,
  AUTOBAUD_E_POLL_RX_FAULT
} autobaud_e_poll_t;

typedef struct {
  uint32_t session_id;
  uint32_t frame_id;
  uint32_t uart_error_generation;
  autobaud_validate_rx_evidence_t evidence;
} autobaud_e_frame_meta_t;

bool autobaud_e_evidence_trustworthy(const autobaud_e_frame_meta_t *meta,
                                     uint32_t expected_session);

#endif
