# Pyxis Unified Autobaud V2 — M7 FreeRTOS On-Demand Integration

**Source baseline:** Pyxis M6 `feature/pyxis-autobaud-v2-m6` at `21665b35e0c4adb39f31241eda59a925fcfc79d8`.
**Working branch:** `feature/pyxis-autobaud-v2-m7`.
**Reference engine:** Titania `feature/unified-autobaud` at `ea9ecbbd2b596f733c8207150b7c6746c5146918`.
**Scope:** Connect the already host-tested M3–M6 modules to the real Pyxis Modbus task without rewriting the Titania portable on-demand algorithms. All V2 controls are **default-OFF** and require explicitly selected, persisted baud selector 0 at boot. Hardware testing remains deferred per user request.

## Existing firmware modifications

1. `App/Application/Tasks/task_modbus.c`:
   - Under `#if MODBUS_AUTOBAUD_V2_ENABLE`, choose automatic mode **only when** `config_valid && cfg.baud_rate == 0U`.
   - Initial UART setup for V2 automatic mode is **9600 8E1**, including one stop bit. Other fixed configurations follow the existing settings, not the V2 path.
   - Exclude the old `uart3_autobaud_lock()` blocking loop when the V2 coordinator is compiled. Legacy startup autobaud under `MODBUS_AUTOBAUD_ENABLE` remains unchanged as an alternate, mutually exclusive build option.
   - On initialized UART and selected Auto, `PyxisAutobaudV2_Start()` arms the coordinator without measuring or waiting for a master. A failed arm latches the Modbus communication fault.
   - In the existing forever loop, run `PyxisAutobaudV2_Service()` **once per iteration**, and process normal Modbus frames only if the selected mode is fixed or V2 has positively locked. This ensures a frame captured during initial detection cannot reach FC06/FC16 handlers, respond, or trigger EEPROM persistence.
   - Keep the existing vibration-adapter config updates, Modbus persistence, system command flow and `osDelay(1)` task pacing intact.
   - Do not modify the configured slave ID or rewrite baud/parity persistent storage on detection. Normal slave-ID handling resumes after the verified RX handoff.

2. `App/Application/Modules/Modbus/modbus_module.c`: allow persisted baud selector **0** when either legacy autobaud or the new V2 engine is explicitly compiled. Default builds still reject selector 0 and fall back to the original factory communication defaults. Existing fixed-rate validation stays unchanged.

3. `App/Middleware/ModbusEngine/Inc/modbus_engine_config.h`: add `MODBUS_AUTOBAUD_V2_ENABLE=0` default. Compile-time guards enforce boolean selection, M4/M5/M6 dependencies, custom Modbus engine, legacy mutual exclusion and no forced fixed-communication override when V2 is enabled.

4. `CMakeLists.txt`: add default-disabled M7 option, compile the unchanged `autobaud_ondemand.c` and new Pyxis coordinator only when enabled, and pass the selection to firmware sources. Also probe the compiler for `-fcyclomatic-complexity` support: keep it with supporting STM32CubeIDE GCC versions but omit it on older Linux GNU Arm compiler versions to permit an actual ELF link. No source-level change to any vibration or Modbus protocol algorithm.

## New target coordinator

- `App/BSP/Communication/pyxis_autobaud_v2_stm32h563.h/.c`: Pyxis-specific bridge, **no** new HAL or peripheral dependencies added to the portable M3 engine.
- `Start()` only arms state; actual verified 9600 8E1 passive receiver transaction occurs once in the Modbus task service loop. If the worker is momentarily BUSY, retry boundedly for up to **2000 ms** without allowing normal Modbus application traffic. If the transaction cannot be verified, fail closed instead of starting an unsafe responder.
- `WAIT_ACTIVITY`: try complete CRC-valid default-profile request first, consuming/quarantining it via M6; otherwise allow Titania's initial 500ms observation grace and wait for qualifying RX activity.
- `CAPTURING`: use M5 asynchronous TIM2 Begin/Poll (12s maximum), not the legacy synchronous capture. Invalid, ambiguous, timeout and safely rolled-back begin failures reset initial RX activity baselines so stale events do not retrigger without a new request.
- `FRAMING`: use M6 passive `autobaud_e_start_dwell` with Titania on-demand **52s framing budget** and **12s candidate dwell**, order **8E1, 8N2, 8O1, 8N1**, accept any legal slave ID and valid broadcasts on CRC-correct, error-free, fresh RTU request evidence only.
- `LOCKED`: M6 physically verifies UART/PC4/DMA, consumes evidence frame, starts fresh normal RX session, then the M3 supervisor latches success. **No reacquisition on master silence**; stay locked until reboot.
- `COOLDOWN`: 1000 ms after recoverable failures, resume WAIT_ACTIVITY, only from new observed RX evidence.
- `FAULT`: fail-closed latched Modbus receive/transmit disable on unsafe restore/restart.
- `GetDiagnostics()` offers active profile, state, initial attempts, capture attempts, retries, lock/failure counts for debugging without changing Modbus register map.

## Build modes

| Feature | Default/current product | M7 developer opt-in |
| --- | --- | --- |
| `MODBUS_ENGINE_ACTIVE` | 1 | 1 |
| `MODBUS_AUTOBAUD_ENABLE` | 0 | 0 |
| `MODBUS_AUTOBAUD_V2_PORT_ENABLE` | 0 | 1 |
| `MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE` | 0 | 1 |
| `MODBUS_AUTOBAUD_V2_FRAMING_ENABLE` | 0 | 1 |
| `MODBUS_AUTOBAUD_V2_ENABLE` | **0** | **1** |
| Config baud selector | Existing fixed baud | **0** to activate V2 at next boot |

When compiled with V2=1, a *fixed* configured baud continues through the existing fixed-mode path and does not start capture. V2 build and legacy autobaud flags cannot both be 1.

## New tests and CI

- `tests/autobaud_v2/test_pyxis_v2_coordinator.c`: real board coordinator linked to mocked UART/capture/framing/worker adapters. Separate process per scenario (sticky supervisor state is deliberately reboot-only): no-master long idle, direct first-request lock, valid capture/framing lock, inconclusive capture/cooldown, temporary START-BUSY vs bounded fault budget, and fatal capture restoration. Checks no lock without a verified passive event and no restart after lock.
- `tests/test_pyxis_autobaud_v2_m7.py`: execute M0–M6 regression suite, static default-off/fixed-mode source invariants and M7 C11 mocks, with warnings as errors.
- `tests/autobaud_v2/check_m7_arm_syntax.sh`: compile the real guarded M4–M7 STM32 transport, USART3, TIM2, Modbus startup task and configured module; added existing `Drivers/Flash_Storage` include.
- `.github/workflows/pyxis-autobaud-v2-m7.yml`: native suite, ARM targeted source syntax, and **complete linked ELF firmware image builds with V2 on and with default V2 off** (GCC version support permitting).
- Inherited M0–M4 historical tests now assert the **new** default-disabled and conditional M7 activation rather than their older pre-M7 "no V2 reference exists" expectation. Their real portable algorithm and legacy communication regression cases remain active.

## Verification and limitations

Native host tests demonstrate coordinator state-machine policy with mocked physical I/O, not real CAN/UART signals.
ARM cross-compiler syntax validates STM32 headers and calls, not timing on a board.
A complete ELF link catches unresolved dependencies but does not qualify live DMA, IRQ latency, TX/DE turnaround, UART parity or PC4 alternate-function restoration.
`8N1` vs `8N2` is not always independently distinguishable from a passive CRC-valid request. The portable Titania engine intentionally flags that limitation.
The active communication runtime retains the configured selector 0 as the boot configuration; the detected physical UART mode is available in the V2 diagnostics, without modifying persisted communication settings.
This M7 implementation leaves `main` untouched; its branch targets the M6 feature branch for staged review.

**M8 hardware qualification:** 5 baud x 4 framing, 100ms..10s poll spacing, wrong slave ID, legal broadcast, invalid CRC, 500ms initial fast-lock grace, pin-mux/DMA recovery, FIFO/FFT independence, default/fixed-mode smoke testing, power-reset and locked-mode silence.

**Software sign-off:** Record the final GitHub Actions run and exact commit before marking verified; hardware acceptance deferred to M8.
