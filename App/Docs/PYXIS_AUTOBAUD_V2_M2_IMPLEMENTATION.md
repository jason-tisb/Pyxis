# Pyxis Unified Autobaud V2 — Milestone M2 implementation report

**Scope:** Titania-derived *portable* activity, scoring, request validation, evidence and optional 16-bit timestamp-integrity algorithms. No changes to active MCU firmware paths, task scheduling, UART, timer, DMA, Modbus map, or persistent settings.

- Pyxis source branch: `feature/pyxis-autobaud-v2-m1` at `d58cdc890dec8e850cfb51d3b9fde1f437d68eb2`.
- Implementation branch: `feature/pyxis-autobaud-v2-m2`.
- Titania reference: `feature/unified-autobaud` at `ea9ecbbd2b596f733c8207150b7c6746c5146918`.
- Titania host CI source reference: https://github.com/jason-tisb/itania_RnD/actions/runs/42206444529

## New portable firmware files

| Source and header under `App/Middleware/AutobaudV2/{Src,Inc}/` | Behavior | Modification vs Titania |
| --- | --- | --- |
| `autobaud_activity` | Qualifies at least 3 bytes or 2 UART errors in a poll, or 2 corroborating weak observations within 60 s | Copied exactly |
| `autobaud_score` | Scores timer edge intervals against 9600, 19200, 38400, 57600, 115200 baud; confidence and ambiguity checks; optional `delta16` helper | Header uses `autobaud_v2_measurement.h` instead of `hw_autobaud_capture.h`; no timer-specific include |
| `autobaud_validate` | Strict passive CRC16, Modbus RTU structure, supported FC01/02/03/04/05/06/0F/10, any slave 1..247 and legal broadcast writes (address 0) | Reuses existing pure Pyxis `modbus_crc.h` + `modbus_crc.c`; no new CRC implementation, only comments adjusted |
| `autobaud_v2_evidence` | Rejects stale/missing RX sessions, zero frame IDs, incomplete frames and RX/transport faults | Copied exactly |
| `autobaud_timestamp32` | Fail-closed 16-bit timer-overflow epoch tracking and reference consistency arithmetic | Copied implementation; documented as *optional* because Pyxis capture uses 32-bit TIM2 |

Source is intentionally **not registered in the firmware CMake target**; M2 cannot alter active Modbus behavior.

## Added software tests

- `tests/autobaud_v2/test_activity.c`: Titania native filter fixture, idle/noise and tick-wrap scenarios.
- `tests/autobaud_v2/test_score.c`: Titania native five-baud tests, invalid intervals, overflow guard and ambiguity.
- `tests/autobaud_v2/test_validate.c`: Titania native any-ID, legal broadcast, valid/invalid FC structure, CRC and evidence test; links **only** the pure existing Pyxis CRC implementation.
- `tests/autobaud_v2/test_timestamp32.c`: Titania normal, overflow, ambiguous ordering, DWT reference, fault injection and randomized reference-model fixture.
- `tests/autobaud_v2/test_evidence.c`: Pyxis-specific negative cases for session, frame ID, error flags and incomplete frames.
- `tests/test_pyxis_autobaud_v2_m2.py`: compiles each C fixture with `-std=c11 -O2 -Wall -Wextra -Werror -pedantic`, executes them, checks portable includes and M7 activation remains disabled, and reruns M0/M1 + six existing Pyxis startup-policy Python tests.
- `.github/workflows/pyxis-autobaud-v2-m2.yml`: opt-in branch/PR native Linux CI for the same runner.

To rerun on a developer checkout:
```sh
python3 tests/test_pyxis_autobaud_v2_m2.py
```
Or use the CI workflow on the M2 branch. The C tests have no STM32 HAL, FreeRTOS, UART, field-I/O driver, Modbus request handler or sensor dependencies.

## Behavior and limitations carried forward from Titania

1. An interval classifier output must be `HW_AUTOBAUD_MEASURE_VALID` **and** pass the minimum confidence gate (750 permille) and sample-count gate before attempting framing. Valid intervals alone never lock Modbus.
2. A CRC-correct request is only passive *evidence*; it must never be passed to Pyxis's application runtime, register writes, persistence or TX path.
3. The any-ID validator supports broadcast **write** requests as evidence; broadcast reads are structurally invalid.
4. Metadata trust requires a fresh, nonzero RX session, a nonzero frame ID and four positive evidence flags. The hardware adapter must supply real fault counters, not optimistic true flags.
5. The 16-bit timestamp-extension implementation is preserved as optional reference code; do **not** apply it blindly to Pyxis's 32-bit TIM2 capture path.
6. `autobaud_v2_candidate` remains for M6 because it depends on the framing engine and passive transaction ownership port, which have not been introduced yet.
7. The on-demand supervisor remains a declaration-only contract from M1; its implementation is reserved for M3.

## Software sign-off

- [x] Imported all scoped portable source/header pairs, pinning Titania upstream.
- [x] Added native regression fixtures, including negative evidence and unchanged legacy policy checks.
- [x] Added CI entry point that does not need a cross-compiler or physical PCB.
- [x] GitHub diff review must confirm only new M2 sources/tests/docs/workflow.
- [ ] Confirm GitHub Actions portable host test conclusion (record run URL).
- [ ] ARM cross-build (not meaningful until M7 registers new sources).
- [ ] On-target electrical and RTU qualification (user explicitly deferred).

**Next M3:** import the Titania on-demand supervisor and its portable framing/transaction contracts with mocked callbacks; no live USART3, PC4/TIM2 or DMA changes.
