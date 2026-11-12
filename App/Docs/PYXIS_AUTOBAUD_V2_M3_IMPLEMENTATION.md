# Pyxis Unified Autobaud V2 — Milestone M3 Implementation

**Status:** Software-only M3 source and host tests committed; run exact workflow on M3 head before claiming pass.

**Source baseline:** Pyxis `feature/pyxis-autobaud-v2-m2` commit `9bdf893eadedb31cff898fe62ba4ae09f740e4ab`.
**Upstream:** Titania `feature/unified-autobaud` commit `ea9ecbbd2b596f733c8207150b7c6746c5146918`.
**Working branch:** `feature/pyxis-autobaud-v2-m3`.

## Scope and exact changes

New portable middleware:
- `App/Middleware/AutobaudV2/Src/autobaud_ondemand.c`: exact Titania cooperative event-driven supervisor logic; uses M1 `autobaud_ondemand.h` (no `hw_autobaud_capture.h`).
- `App/Middleware/AutobaudV2/Inc/autobaud_uart_transaction.h`: Titania transaction result/status, callback, diagnostics and ownership contract. Adapted include `hw_autobaud_capture.h` -> M1 `autobaud_v2_measurement.h`; removed UART5 wording from comments. No HAL headers.
- `App/Middleware/AutobaudV2/Src/autobaud_uart_transaction.c`: Titania portable claim/snapshot/quiesce/apply/timing/restart/verify/rollback controller, unchanged algorithm.
- `App/Middleware/AutobaudV2/Inc/autobaud_v2_framing.h`: Titania passive framing interface/types and callback contract only. **M6 imports actual framing implementation** and its candidate parser; no unresolved framing references are linked in M3.

One existing M1 header comment updated:
- `App/Middleware/AutobaudV2/Inc/autobaud_ondemand.h`: status now accurately describes M3 implementation as *not yet linked*.

New tests and CI:
- `tests/autobaud_v2/test_ondemand.c`: imported upstream host tests for no-master idle, single sticky lock, direct default-profile lock, ordinary capture/framing and retry after cooldown.
- `tests/autobaud_v2/test_uart_transaction.c`: upstream transactional host tests for all 20 profiles, claim/snapshot failures, rollback, fault latch, passive gate, stale-frame clearance, capture restoration and randomized recovery.
- `tests/autobaud_v2/test_ondemand_edges.c`: Pyxis-specific negative tests: invalid callbacks/profiles, invalid measured baud, low confidence, too few samples, capture/framing failures, fatal latch, 32-bit cooldown wrap, repeated no-master service and terminal lock.
- `tests/test_pyxis_autobaud_v2_m3.py`: builds and executes three C11 mock fixtures and reruns the entire M0-M2 Python/C suite; checks dormant build and hardware isolation.
- `.github/workflows/pyxis-autobaud-v2-m3.yml`: CI on M3 push and PR to M2; runs native C11 and inherited regression tests.

## State machine now implemented (not active in firmware)

```text
                 +-----------------------------+
                 | WAIT_ACTIVITY (no boot wait)|
                 +-----------------------------+
                     |         |
          CRC-valid initial    qualifying RX event
          passive request      |
                     |         v
                     |    CAPTURING (async poll)
                     |         |
                     |         v
                     |    FRAMING (async poll)
                     |         |
                     v         v
                    LOCKED (sticky until reboot)
                              ^
                              |
    recoverable error --> COOLDOWN (1000 ms) --> WAIT_ACTIVITY
    fatal port error ---------------------------> FAULT (latched)
```

- `autobaud_od_init` validates required callbacks and initializes `WAIT_ACTIVITY` without a capture.
- `autobaud_od_service` does one bounded state operation per call; it contains no explicit delays or loops and never invokes Pyxis's Modbus responder.
- A valid default-profile request can lock through `probe_initial` **only when the port callback positively proves the complete request and has quarantined it**. The supervisor itself does not inspect CRC; the adapter's M6 validator must enforce this.
- Meaningful RX activity calls `capture_begin(12000 ms)`; a valid capture requires a supported baud, at least 8 measured intervals and confidence >= 750 before `framing_begin(52000 ms, 12000 ms dwell)`.
- Capture/framing results are delivered by polling; recoverable failures enter `COOLDOWN`; terminal fatal state latches once; successful lock never re-arms on silence.
- All timeout, recovery, cleanup and application-traffic isolation during each asynchronous operation are **port adapter responsibilities**. A perpetual `CAPTURE_PENDING` or `FRAME_RUNNING` callback is not timed out by this supervisor in isolation; the future adapter must implement finite deadlines.

## UART transaction isolation (portable M3 only)

The `autobaud_uart_transaction` controller owns software ordering and verified rollback. Physical `claim/quiesce/apply/timing/restart/verify/release` callbacks will be implemented by M4 on Pyxis USART3 + GPDMA1 Ch2.

**Important:** The inherited `autobaud_uart_txn_capture` API is a *synchronous* callback contract for legacy capture transactions. Do not call it from Pyxis's M7 on-demand task if capture can last up to 12 seconds. The on-demand supervisor uses distinct **asynchronous** `capture_begin`/`capture_poll` callbacks and M5 must maintain bounded task service. The transaction coordinator can still be used for bounded candidate UART profile switches.

## Scope deliberately excluded

- No changes to `CMakeLists.txt`, `CMakePresets.json`, preprocessor flags, existing UART/DMA handling, `task_modbus.c`, `task_vibration.c`, Modbus runtime, register map or persistence.
- No Titania hardware port and no STM32H523-specific source copied.
- No live Modbus request execution, no RS485 TX, no flash writes from the new engine.
- No new port implementation, no configured baud 0 policy change, no persistent baud changes.
- No integration of the framing engine/candidate validation or on-chip TIM2 capture yet.
- No full ARM MCU cross-build and no physical hardware tests in M3, per user's explicit preference to complete software first.

## Verification

Run on a complete Pyxis checkout:
```sh
python3 tests/test_pyxis_autobaud_v2_m3.py
```

The runner compiles with `-std=c11 -O2 -Wall -Wextra -Werror -pedantic` and executes:
1. Existing M0 + M1 + M2 software and legacy Pyxis autobaud fixtures.
2. Titania on-demand supervisor fixture.
3. Pyxis M3 supervisor negative/edge cases.
4. Titania portable UART transaction/rollback mock fixture.
5. Hardware independence and dormant CMake/Modbus task policy checks.

**Exit gate:** GitHub Actions on the final M3 commit must be successful; GitHub diff must contain no active firmware modifications. Hardware acceptance remains separately deferred.

## Next milestone — M4

Implement the **Pyxis-specific UART/DMA transaction adapter** behind a disabled build option:
- Exclusive USART3/GPDMA1 Ch2 ownership, TX/DE idle check, task-worker coordination.
- Snapshot, quiesce, serial profile application, safe restart and concrete register/state readback.
- Verified RX session handoff and fail-closed rollback/fatal communication latch.
- Host mock and compile-time STM32 checks without enabling on-demand capture during normal firmware operation.

Only M7 activates automatic detection within the FreeRTOS Modbus task.
