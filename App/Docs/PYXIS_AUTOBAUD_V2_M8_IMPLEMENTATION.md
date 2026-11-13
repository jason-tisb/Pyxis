# Pyxis Unified Autobaud V2 — M8 Software Qualification Report

**Baseline:** Pyxis M7 branch `feature/pyxis-autobaud-v2-m7` at `55e4bbff1358439a17e504f0cd02e9b0407c4df6`.
**M8 branch:** `feature/pyxis-autobaud-v2-m8`.
**Titania reference:** `jason-tisb/itania_RnD` `feature/unified-autobaud` at `ea9ecbbd2b596f733c8207150b7c6746c5146918`.
**Scope:** Complete **software-only M8** and prepare a reproducible on-device qualification checklist. Physical UART/RS485/PCB testing will be performed later by the owner; no electrical pass results are claimed.

## Engineering discovery / correction

The M6 passive RX path copied each USART `HAL_UART_RXEVENT_IDLE` callback into a separate passive candidate mailbox frame. An IDLE callback can occur after approximately **one character time**, not the Modbus RTU **3.5-character silent interval**. A valid frame fragmented by UART/DMA callbacks could therefore have been rejected, or nearby frames could have been merged incorrectly if a fixed 5ms quiet window was used.

**M8 fixes this at the physical transport boundary without modifying any Titania algorithms.**

- New hardware-independent `autobaud_rtu_assembly.c/.h` assembles all IDLE fragments into a bounded **256-byte Modbus RTU candidate**, with no heap allocation or recursion.
- The assembly window uses **ceil(Modbus T3.5)** based on the currently applied UART baud/parity/stop bits. At rates above 19200, Modbus's 1.75ms fixed silent interval is conservatively rounded to 2ms for the HAL millisecond clock. At 9600 8E1 use 5ms; 9600 8N1 uses 4ms; at 19200 8E1 use 3ms.
- `portserial.c` records the callback timestamp **in the ISR** alongside its absolute DMA boundary, then carries that timestamp with the worker's fragment slot. This avoids misinterpreting an RTU interframe gap because the worker serviced queued ISR events later.
- Only the dedicated passive RX worker assembles and publishes complete candidates. Its four-slot mailbox, session/error generation and M6 atomic handoff remain unchanged. A partial frame is never fed to `ModbusEngineRuntime_ProcessFrame()`.
- Invalid fragment lengths, >256-byte assembled frames and mailbox overflow invalidate the receive session via error generation and fail closed; successful RX cannot be based on truncated/contaminated evidence.
- On DMA/session reset, stale parser and partial assembler state are cleared. The final publication is by worker on an existing bounded 1ms event wait; **no additional blocking delay** is introduced into the Modbus task or vibration acquisition.
- `ModbusEngineV2_TakePassiveFrame` now sees only T3.5-delimited candidates; its former fixed 5ms wait after each individual IDLE callback was removed.
- A new read-only `ModbusEngineV2_GetRtuDiagnostics` snapshot returns IDLE fragments, full RTU candidates published, assembly failures, passive mailbox overflow, current UART error generation, session ID and mailbox depth for later hardware diagnostics.

## Existing files modified

| Path | Exact change |
| --- | --- |
| `App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c` | Add ISR-timestamped boundaries, worker-side baud-aware reassembly, overflow/error-generation poisoning, RTU counters/diagnostic snapshot; all new logic guarded by M6 framing flag |
| `App/Middleware/ModbusEngine/Inc/modbus_engine_v2_transport.h` | Add read-only per-session RTU assembly diagnostics struct/API, only under M6 framing guard |
| `CMakeLists.txt` | Conditionally compile the new portable assembler **only** when `MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1` |

**No changes** were made to `task_modbus.c`, `task_vibration.c`, `autobaud_ondemand.c`, `autobaud_v2_framing.c`, the Titania baud scorer, any-ID CRC validator, persistence or Modbus register handlers in M8.

## New files

- `App/Middleware/AutobaudV2/Inc/autobaud_rtu_assembly.h`
- `App/Middleware/AutobaudV2/Src/autobaud_rtu_assembly.c`
- `tests/autobaud_v2/test_rtu_assembly.c`
- `tests/test_pyxis_autobaud_v2_m8.py`
- `tests/autobaud_v2/check_m8_arm_syntax.sh`
- `.github/workflows/pyxis-autobaud-v2-m8.yml`
- `App/Docs/PYXIS_AUTOBAUD_V2_M8_HARDWARE_QUALIFICATION.md`
- This report

## Automated software qualification

1. **Inherited M0–M7** host suite: unchanged portable Titania score/CRC validator/supervisor/framing tests; Pyxis native M4 UART transaction and rollback, M5 capture, M6 side-effect-free handoff, and M7 no-master/fast-lock/sticky-lock/cooldown/fatal integration tests.
2. **New 60-case RTU matrix:** 5 baud rates (9600/19200/38400/57600/115200) × 4 serial profiles (8E1/8N2/8O1/8N1) × 3 master poll intervals (100ms/1s/10s). Exercise IDLE fragmentation as **2+3+3** bytes, threshold crossing, end-to-end CRC request validation with slave ID247, and legal broadcast write evidence. All frames must remain isolated.
3. **Fragment edge/error tests:** UINT32 millisecond tick rollover, interframe gaps, incomplete candidate never published, 256-byte maximum size, attempted 257-byte overflow, invalid/null input, no stale partial stream after reset, and 8N1/8N2 parity-adjusted T3.5 boundaries.
4. **11 compile-time mode cases:** default-off, all V2 modules enabled, missing dependencies, V2 plus legacy, V2 with forced fixed override, V2 with FreeModbus fallback, invalid boolean, legacy-only, invalid M4/M5/M6 combinations. Unsupported combinations must fail at compile time.
5. **Actual ARM STM32 guarded-source compilation:** M4–M7 UART/DMA, TIM2 ISR, Modbus task and M8 assembly with all feature guards enabled; source/header dependencies qualified using arm-none-eabi-gcc.
6. **Three full STM32 ELF link configurations:** M8 V2-enabled, default V2-off and legacy startup-autobaud-only with V2 off. The Github CI run must show three successful linked images, not just syntax checks.
7. **Static safety audit:** confirmed default-off in CMake and presets, no activity at boot, fixed-mode path untouched, no synchronous TIM2 capture call from FreeRTOS, no Modbus application or persistent writes in the passive evidence code, and no HAL/FreeRTOS dependencies in portable AutobaudV2 files.

Run host suite:

```sh
python3 tests/test_pyxis_autobaud_v2_m8.py
```

Run target-source syntax check:

```sh
sh tests/autobaud_v2/check_m8_arm_syntax.sh
```

### How to enable an M8 development build

```text
MODBUS_ENGINE_ACTIVE=1
MODBUS_ENGINE_FORCE_FIXED_COMM=0
MODBUS_AUTOBAUD_ENABLE=0
MODBUS_AUTOBAUD_V2_PORT_ENABLE=1
MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=1
MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1
MODBUS_AUTOBAUD_V2_ENABLE=1
```

Saved Modbus baud selector **must be 0** and settings apply after next boot. A saved fixed baud remains fixed even in this build. Normal/default firmware uses V2=0.

## Release / hardware acceptance boundary

- **Software gate:** final M8 CI run must be green (native, ARM source compile, V2/default/legacy linked firmware). Record exact SHA, run URL and failures fixed.
- **Physical gate: NOT DONE**. See `PYXIS_AUTOBAUD_V2_M8_HARDWARE_QUALIFICATION.md` for 20 profiles × 3 poll cadences plus 15 hardware/fault checks, including PC4 AF1/AF7 restoration, DMA state, RTU receive splits, TX suppression, stop-bit alias, vibration FIFO/FFT continuity, and thermal/power-reset behavior.
- **Known inherent limitation:** CRC-valid passive requests cannot unambiguously discriminate UART 8N1 from 8N2; Titania's candidate priority and `stop_bits_independently_verified=false` are preserved. Do not report an exact match without independent evidence.
- Because no physical board was exercised, **M8 hardware qualification and production rollout remain pending**. Keep the feature PR in draft; do not merge to main or enable on deployed hardware solely because CI passes.
