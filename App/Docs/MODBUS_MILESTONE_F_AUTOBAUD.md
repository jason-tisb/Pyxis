# Pyxis Dynamic Autobaud — Milestone F Completion Report

Status: software implementation complete; hardware qualification deferred to Milestone G.

## Scope

Milestone F adds deterministic diagnostics and qualification evidence around the existing A–E autobaud pipeline. It does not change the Modbus register map, flash persistence, RS485 pin assignment, or the established capture/scoring/validation/scanning/startup algorithms.

## Implemented files

- `App/Middleware/ModbusEngine/Inc/modbus_autobaud_diag.h`
- `App/Middleware/ModbusEngine/Src/modbus_autobaud_diag.c`

......


















- `App/Middleware/ModbusEngine/Src/modbus_autobaud_startup.c`
- `App/Middleware/ModbusEngine/Src/modbus_autobaud_validate.c`
- `App/Middleware/ModbusEngine/Src/modbus_autobaud_scan.c`
- `App/Application/Tasks/task_modbus.c`
- `CMakeLists.txt`
- `cmake/gcc-arm-none-eabi.cmake`

The diagnostics module provides a consistent snapshot API and debugger-visible `volatile` globals. It records measurement valid/ambiguous/timeout/error/unavailable outcomes, capture overruns, validation rejects/timeouts, UART recovery/failure, scan use/lock, measured lock, terminal failure, selected baud/confidence, active state, last profile/error, elapsed time, and scan progress. Counters are cumulative; per-session state is reset by `OnStart()`.

All diagnostic updates are task-context operations. `GetSnapshot()` uses a sequence-stable read, does not allocate memory, print, or touch UART/DMA from an ISR.

## Startup and failure handling

`modbus_autobaud_startup.c` now records every terminal path. Stale score data is cleared before each capture result is evaluated. Capture-unavailable, timeout, overrun, invalid timer frequency, scoring failure, scan budget exhaustion, UART configuration failure, rollback failure, and fallback failure are distinguished. Measured lock and scanned lock are counted once. Validation timeout restores the previous UART profile and records recovery status.

`modbus_autobaud_validate.c` counts rejected validation evidence once per newly processed frame and records UART recovery after timeout/rollback. `modbus_autobaud_scan.c` records candidate validation timeouts. `task_modbus.c` initializes diagnostics in all build modes and keeps legacy fixed-rate autobaud symbols out of enabled builds to avoid dead code warnings.

## Timing and budget policy (reconciled)

The implementation has three distinct timing layers; the earlier report wording was ambiguous:

1. TIM2 measurement capture attempts are capped at **1500 ms** each, with three attempts maximum.
2. The Milestone D scanner uses **1200 ms per profile**. It tests five baud rates × four framing profiles (20 candidates). The 1200 ms value is passed to validation as the candidate dwell; it is not replaced by 1500 ms.
3. Measured-baud framing validation is capped at **2000 ms per framing candidate**.

All work is governed by a wrap-safe **30,000 ms global deadline**. The startup coordinator computes the remaining budget dynamically and subtracts the **500 ms recovery reserve** before starting further work. Thus the frequently observed ~17,000 ms value is a runtime remainder after earlier measurement/validation work, not a hardcoded total scan limit. No unbounded loop or blocking diagnostic path was introduced.

## Verification performed

Python model/regression tests:

```
python tests/test_modbus_autobaud_diag.py
python tests/test_modbus_autobaud_integration.py
python tests/test_modbus_autobaud_score.py
python tests/test_modbus_autobaud_validate.py
python tests/test_modbus_autobaud_scan.py
python tests/test_modbus_autobaud_startup.py
```

Results: all reported `PASS`, including cumulative-session behavior, tick wrap, terminal outcome accounting, five-baud matrix policy, valid-frame acceptance, budget calculation, and compile-definition precedence.

Dependency-free ARM-GCC syntax fixtures also passed:

```
arm-none-eabi-gcc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only tests/test_modbus_autobaud_diag.c
arm-none-eabi-gcc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only tests/test_modbus_autobaud_integration.c
```

The unsupported combination guard was verified: `MODBUS_AUTOBAUD_ENABLE=1` with `MODBUS_ENGINE_ACTIVE=0` is rejected at compile time.

## Build matrix

- Default CubeIDE Debug build: completed with 0 errors and 0 warnings. Current ELF size is `text 193,576`, `data 152`, `bss 472,464` bytes (`666,192` total).
- CMake custom/autobaud-enabled configuration: configure succeeded with `MODBUS_ENGINE_ACTIVE=1`, `MODBUS_AUTOBAUD_ENABLE=1`, and fixed-communication override enabled; every new autobaud and task object compiled successfully with ARM GCC.
- CMake FreeModbus/fallback configuration: configure succeeded with `MODBUS_ENGINE_ACTIVE=0`, `MODBUS_AUTOBAUD_ENABLE=0`; relevant fallback objects compiled successfully.

The full parallel enabled CMake link was interrupted after the build driver stopped making progress; therefore no enabled-CMake linked ELF is claimed here. The production CubeIDE Debug link and all enabled source-object compilations passed.

## Explicitly unchanged/deferred

Milestones A–E remain the functional implementation. No hardware RS485 test, live baud capture, watchdog/power-cycle test, LED/vibration coexistence test, DE timing measurement, or physical fallback test was performed in this milestone. Those are deferred to Milestone G. No Modbus register-map or flash-persistence behavior was changed.

## Milestone G acceptance checklist

Use the diagnostic snapshot while testing each of the 20 baud/framing profiles, no-traffic startup, noisy/partial frames, capture overrun, UART recovery, RS485 DE release, LED/vibration operation, watchdog servicing, and repeated power cycles. Confirm the selected baud/framing and terminal outcome against the debugger counters before enabling autobaud as the field default.
