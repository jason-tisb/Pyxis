# Pyxis Unified Autobaud V2 — M6 Passive Framing Implementation

**Source baseline:** Pyxis M5 `feature/pyxis-autobaud-v2-m5` at `78f65e40c6241757171e6d6031cd45d06f5eb0eb`.
**Development branch:** `feature/pyxis-autobaud-v2-m6`.
**Titania reference:** `feature/unified-autobaud` at `ea9ecbbd2b596f733c8207150b7c6746c5146918`.

## Goal / M6 boundary

Import Titania's **passive CRC-validated, any-ID Modbus RTU evidence** logic, add a Pyxis RTOS receiver adapter and an atomic normal-receive handoff, while leaving M6 **disabled by default** and not invoking the M3 supervisor from the running FreeRTOS Modbus task. The no-master and fixed-baud application continue unchanged until M7.

## Portable source imported

- `App/Middleware/AutobaudV2/Src/autobaud_v2_framing.c` — **unchanged Titania** passive profile candidate controller, deadline guards, CRC-valid frame acceptance, restoration/failure handling, diagnostic counts, nonblocking polling. Candidate order: **8E1, 8N2, 8O1, 8N1**.
- `App/Middleware/AutobaudV2/Inc/autobaud_v2_candidate.h` and `Src/autobaud_v2_candidate.c` — **unchanged Titania** single-frame observation + session verification + token-checked commit; consumes, does not process, the evidence frame.
- `App/Middleware/AutobaudV2/Inc/autobaud_initial_window.h` and `Src/autobaud_initial_window.c` — pure, hardware-independent representation of Titania's 500 ms first-request grace, error-activity qualification, invalid-frame trigger, and rollover-safe elapsed-time arithmetic.
- `App/Middleware/AutobaudV2/Inc/autobaud_v2.h` — restores the upstream default `AUTOBAUD_V2_FRAMING_TIMEOUT_MS=2000` for the historical `autobaud_e_start` API; **the on-demand path calls `autobaud_e_start_dwell` with 52s global and 12s candidate budgets**, matching M3.

## Board and physical transport integration

- `App/Middleware/ModbusEngine/Inc/modbus_engine_v2_transport.h` — M6 task-only interface for RX activity counts, reading copied passive frames, verifying current session+error generation, performing atomic passive-to-normal handoff and latching communication fault.
- `App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c` — extend the **M4 opt-in receive worker** only under `MODBUS_AUTOBAUD_V2_FRAMING_ENABLE`:
  1. Reserve a four-frame bounded passive SPSC mailbox. The worker (sole producer) copies completed frame bytes and publishes data with a memory barrier; the foreground Modbus task is the sole consumer. A full mailbox **drops a frame and increments RX error generation**, invalidating the candidate receive session rather than silently trusting a partial trace.
  2. In passive mode, drain the existing worker-completed frame queue **into this mailbox only**, never to `ModbusEnginePort_OnRxByte` / the normal Modbus request parser. All evidence traffic, including FC06/FC16 to the device's own slave ID and Modbus broadcast writes, is quarantined.
  3. Require at least 5ms observed RX silence after the last UART IDLE callback before exposing a completed candidate. This is conservative because an IDLE interrupt alone is only approximately one character and need not prove the 3.5-character RTU boundary.
  4. Include a nonzero session ID, monotonically changing nonzero frame ID, error generation, complete RTU reception and four positive trust flags with every candidate.
  5. Reject missing/stale session, DMA stop, UART FE/ORE/PE/NE generation changes, worker recovery pending, RX overflow, bad frame length, mismatch of fresh profile and TX-inactive/DE-receive.
  6. On accepted valid request, **claim and park the worker again**, recheck candidate token/session/error generation/UART hardware, quiesce and discard all evidence traffic, restart and verify the normal UART/DMA receive session, and release exclusive ownership **only after verified normal-mode restart**.
  7. If the hardware handoff fails after mutation, latch fatal communication fault, never reprocess the evidence request. If contention occurs before mutation, fail closed without asserting success.
  8. Preserve existing normal Modbus TX protection from M4: no transmit while passive/claimed/fatal.

- `App/BSP/Communication/pyxis_autobaud_framing_stm32h563.h/.c` — board-level bridge for M3/M6 interface:
  - `Init` and `ResetInitial` require known **9600 8E1** passive session.
  - `ProbeInitial` validates any-ID CRC request and immediately verifies/commits it using Titania's candidate observation; no response or register writes. Invalid frames trigger capture activity instead.
  - `Activity` waits 500ms from first received byte while allowing a complete initial request to finish; duplicated qualifying UART errors or a definitively invalid frame can trigger measurement earlier.
  - `Start/Service/State` run the unchanged Titania framing controller with the measured baud, 52s framing window and 12s candidate dwell (caller-supplied).
  - `IsFaulted` reflects the UART transport fatal latch and framing restoration error.
  - On a failed framing search that successfully restores the default receive profile, reset initial RX byte/error baselines **once** so counter resets and old session traffic cannot immediately cause another capture without fresh RX activity.

## Source/default build settings

Add `MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=0` (OFF). Setting it to 1 requires the M4 UART transaction adapter and M5 TIM2 capture enabled. It conditionally compiles portable validator, evidence, candidate, framing, initial observation and Pyxis board framing code. The new V2 state machine **is not yet connected to `task_modbus.c`**, so a build with the flag enabled is still not an end-to-end autobaud implementation.

No changes to `task_modbus.c`, `task_vibration.c`, Modbus application register handlers, EEPROM persistence, IIS3DWB SPI1 DMA or output control.

## Titania behavior compatibility

- **No slave-ID match** for detection: any 1..247, regardless of Pyxis configured ID. Valid broadcast **write** requests (address 0) may prove framing; broadcast reads cannot.
- **CRC-valid, structurally valid RTU requests only**, verified in a clean fresh RX session.
- **No execution or response** for the detection request, even FC06/FC16 to Pyxis itself; normal request processing begins with the **next** request after lock.
- **First valid 9600 8E1 request** can lock without timer capture. Allow up to 500ms after initial RX activity before trying capture.
- **No automatic boot scanner** is introduced. M7 connects M3 to this port and M5 only after an on-demand trigger.
- **No blind passive 8N1/8N2 distinction** from a CRC-correct request; the core preserves the not-independently-verified flag.
- **Sticky lock** remains owned by the M3 supervisor and must not reset on master silence.

## Tests added

1. `tests/autobaud_v2/test_framing.c` — upstream Titania native framing tests for 20 baud/mode combinations, any-ID/broadcast, invalid frames, stale sessions, deadlines, RX faults, restore and randomized untrusted evidence.
2. `tests/autobaud_v2/test_initial_window.c` — 500ms grace, invalid frame, UART error qualification, tick wrap and indefinite idle.
3. `tests/autobaud_v2/test_pyxis_framing_port.c` — real Pyxis board framing bridge linked with a mock UART/transport: direct 9600/8E1 any-ID/broadcast fast-lock, CRC rejection, 500ms grace, measured candidate acceptance, zero application/TX/persistence side effects, and fresh-activity rearming after an unsuccessful framing search.
4. `tests/test_pyxis_autobaud_v2_m6.py` — M0–M5 regressions, strict portable interface checks and native M6 C11 tests.
5. `tests/autobaud_v2/check_m6_arm_syntax.sh` — actual guarded STM32 portserial, TIM2, UART transaction and passive framing source syntax check with all M4/M5/M6 flags enabled.
6. `.github/workflows/pyxis-autobaud-v2-m6.yml` — separate native and cross-compiler CI jobs.

Run:
```sh
python3 tests/test_pyxis_autobaud_v2_m6.py
sh tests/autobaud_v2/check_m6_arm_syntax.sh
```

## Remaining qualification / next milestone

- [ ] Record final M6 native and ARM source CI results.
- [ ] Full ARM embedded image linking (opt-in M4–M6) and MCU runtime/hardware testing remain distinct from syntax compilation.
- [ ] M7 must connect `PyxisAutobaudFraming_{ResetInitial,ProbeInitial,Activity,Start,Service}` and `PyxisAutobaudCapture_{Begin,Poll}` to the M3 `autobaud_od_t` supervisor in `task_modbus.c`, with a separate **default-OFF on-demand flag**, only for saved `baud_rate == 0` and with existing fixed/legacy selections unchanged.
- [ ] M7 must explicitly turn on a verified initial **9600 8E1 passive UART receive session**, call `ResetInitial`, and suppress normal processing until locked; after capture recovery call the appropriate activity-session reset.
- [ ] Hardware qualification at M8 must test frame segmentation across 9600..115200, DMA ISR races, PC4 restoration, 100ms..10s master polling, fixed-mode regressions, no silence reacquisition and vibration FIFO independence.

**Do not merge into `main` or claim physical UART operation until M7 and M8 qualification.**
