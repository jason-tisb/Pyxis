#include "autobaud_v2_evidence.h"
#include <stddef.h>

bool autobaud_e_evidence_trustworthy(const autobaud_e_frame_meta_t *meta,
                                     uint32_t expected_session)
{
  return meta != NULL && expected_session != 0U &&
         meta->session_id == expected_session && meta->frame_id != 0U &&
         meta->evidence.complete_rtu_frame &&
         meta->evidence.uart_error_free &&
         meta->evidence.transport_error_free &&
         meta->evidence.fresh_after_profile_apply;
}
