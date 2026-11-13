#include "autobaud_v2_candidate.h"
#include <stddef.h>
#include <string.h>

autobaud_candidate_status_t autobaud_candidate_observe(
    const autobaud_e_ops_t *ops, uint32_t expected_session,
    autobaud_candidate_observation_t *out)
{
  uint8_t frame[AUTOBAUD_VALIDATE_MAX_ADU_LENGTH] = {0};
  uint16_t length = 0U;
  autobaud_e_poll_t status;
  if (out == NULL) return AUTOBAUD_CANDIDATE_RX_FAULT;
  *out = (autobaud_candidate_observation_t){0};
  if (ops == NULL || ops->take_frame == NULL ||
      expected_session == 0U) return AUTOBAUD_CANDIDATE_RX_FAULT;

  status = ops->take_frame(ops->user, frame, (uint16_t)sizeof(frame),
                           &length, &out->frame);
  switch (status) {
    case AUTOBAUD_E_POLL_NONE: return AUTOBAUD_CANDIDATE_WAIT;
    case AUTOBAUD_E_POLL_BAD_FRAME: return AUTOBAUD_CANDIDATE_REJECTED;
    case AUTOBAUD_E_POLL_FRAME: break;
    default: return AUTOBAUD_CANDIDATE_RX_FAULT;
  }
  if (out->frame.session_id != expected_session ||
      out->frame.frame_id == 0U) {
    out->stale_session = true;
    return AUTOBAUD_CANDIDATE_REJECTED;
  }
  if (!autobaud_e_evidence_trustworthy(&out->frame, expected_session)) {
    out->validation = AUTOBAUD_VALIDATE_UNTRUSTED_RX;
    return AUTOBAUD_CANDIDATE_REJECTED;
  }
  out->validation = autobaud_validate_request(
      frame, length, &out->frame.evidence, &out->request);
  return out->validation == AUTOBAUD_VALIDATE_ACCEPTED
      ? AUTOBAUD_CANDIDATE_READY : AUTOBAUD_CANDIDATE_REJECTED;
}

bool autobaud_candidate_commit(
    const autobaud_e_ops_t *ops, const autobaud_v2_profile_t *profile,
    const autobaud_candidate_observation_t *observed,
    bool (*deadline_guard)(void *), void *guard_user)
{
  uint32_t live_session = 0U;
  if (ops == NULL || profile == NULL || observed == NULL ||
      !autobaud_v2_profile_supported(profile) ||
      observed->validation != AUTOBAUD_VALIDATE_ACCEPTED ||
      !autobaud_e_evidence_trustworthy(&observed->frame,
                                       observed->frame.session_id) ||
      ops->get_session == NULL || ops->verify == NULL ||
      ops->accept == NULL || ops->fatal == NULL ||
      deadline_guard == NULL || ops->fatal(ops->user))
    return false;
  if (!ops->get_session(ops->user, &live_session) ||
      live_session != observed->frame.session_id ||
      !ops->verify(ops->user, profile, live_session,
                   observed->frame.uart_error_generation))
    return false;
  if (!deadline_guard(guard_user) || ops->fatal(ops->user))
    return false;
  return ops->accept(ops->user, profile, &observed->frame);
}
