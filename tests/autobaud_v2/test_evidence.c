/* Pyxis M2: prove immutable passive evidence cannot authorize a lock with
 * stale, missing, inconsistent or error-marked receive-session metadata.
 * This file links only the portable evidence filter, not a Modbus responder.
 */
#include "autobaud_v2_evidence.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
  const uint32_t session = 4U;
  autobaud_e_frame_meta_t meta = {
      .session_id = session,
      .frame_id = 1U,
      .uart_error_generation = 7U,
      .evidence = {
          .complete_rtu_frame = true,
          .uart_error_free = true,
          .transport_error_free = true,
          .fresh_after_profile_apply = true,
      }};
  assert(autobaud_e_evidence_trustworthy(&meta, session));
  assert(!autobaud_e_evidence_trustworthy(NULL, session));
  assert(!autobaud_e_evidence_trustworthy(&meta, 0U));
  assert(!autobaud_e_evidence_trustworthy(&meta, session + 1U));
  meta.frame_id = 0U;
  assert(!autobaud_e_evidence_trustworthy(&meta, session));
  meta.frame_id = 1U;
  meta.session_id = 0U;
  assert(!autobaud_e_evidence_trustworthy(&meta, session));
  meta.session_id = session;

  meta.evidence.complete_rtu_frame = false;
  assert(!autobaud_e_evidence_trustworthy(&meta, session));
  meta.evidence.complete_rtu_frame = true;
  meta.evidence.uart_error_free = false;
  assert(!autobaud_e_evidence_trustworthy(&meta, session));
  meta.evidence.uart_error_free = true;
  meta.evidence.transport_error_free = false;
  assert(!autobaud_e_evidence_trustworthy(&meta, session));
  meta.evidence.transport_error_free = true;
  meta.evidence.fresh_after_profile_apply = false;
  assert(!autobaud_e_evidence_trustworthy(&meta, session));

  meta.evidence.fresh_after_profile_apply = true;
  assert(autobaud_e_evidence_trustworthy(&meta, session));
  assert(meta.uart_error_generation == 7U);
  puts("Pyxis Autobaud V2 M2 evidence test: PASS");
  return 0;
}
