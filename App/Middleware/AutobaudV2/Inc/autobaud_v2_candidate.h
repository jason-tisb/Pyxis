#ifndef INC_AUTOBAUD_V2_CANDIDATE_H_
#define INC_AUTOBAUD_V2_CANDIDATE_H_
/* Single-profile passive RTU request evidence, shared by E and F.1.
 * HAL-free: the adapter supplies the transport and exact-session checks.
 * Observation never calls Modbus application handlers and never transmits.
 */
#include "autobaud_v2_framing.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  AUTOBAUD_CANDIDATE_WAIT = 0,
  AUTOBAUD_CANDIDATE_REJECTED,
  AUTOBAUD_CANDIDATE_RX_FAULT,
  AUTOBAUD_CANDIDATE_READY
} autobaud_candidate_status_t;

typedef struct {
  autobaud_e_frame_meta_t frame;
  autobaud_validate_request_info_t request;
  autobaud_validate_result_t validation;
  bool stale_session;
} autobaud_candidate_observation_t;

/* Consumes no more than one candidate frame from the current RX session.
 * READY requires a copied complete frame, matching session, four positive
 * evidence flags, and Milestone B CRC/structure validity. No lock yet.
 */
autobaud_candidate_status_t autobaud_candidate_observe(
    const autobaud_e_ops_t *ops, uint32_t expected_session,
    autobaud_candidate_observation_t *out);

/* Deadline guard executes after verified active UART/DMA/session state and
 * immediately BEFORE attempting the token-checked physical lock handoff.
 * The owning E/F controller must reject an expired candidate rather than
 * executing or accepting its frame.
 */
bool autobaud_candidate_commit(
    const autobaud_e_ops_t *ops, const autobaud_v2_profile_t *profile,
    const autobaud_candidate_observation_t *observed,
    bool (*deadline_guard)(void *), void *guard_user);

#endif
