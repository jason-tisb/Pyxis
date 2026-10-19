# Modbus Dynamic Autobaud — Milestone E Completion Report

## Scope

Milestone E integrates the completed A–D capture, scoring, framing-validation,
and profile-scanner modules into one startup policy for the custom
`ModbusEngine`. It is disabled by default; the existing fixed/configured
startup remains unchanged unless `MODBUS_AUTOBAUD_ENABLE=1` is defined.

Hardware RS485/autobaud verification remains deferred to the planned hardware
milestone.

## Implementation

### New coordinator

- `App/Middleware/ModbusEngine/Inc/modbus_autobaud_startup.h`
- `App/Middleware/ModbusEngine/Src/modbus_autobaud_startup.c`

The coordinator owns `IDLE`, `MEASURING`, `SCORING`, `VALIDATING`, `SCANNING`,
`RESTORING`, `LOCKED`, `FALLBACK`, and `ERROR` states. Its public API is
`Start`, cooperative `Service`, `Stop`, and `GetResult`.

The result distinguishes `LOCKED_MEASURED`, `LOCKED_SCANNED`, `FALLBACK`, and
`ERROR`, while retaining the applied baud, framing, confidence, elapsed time,
profile count, and measurement-attempt count.

### Policy and timing

- Three measurement attempts, each capped at 1500 ms.
- Titania framing order: 8E1, 8N2, 8O1, then Pyxis 8N1.
- Per-framing validation remains capped at 2000 ms.
- One wrap-safe 30,000 ms global deadline with a 500 ms recovery reserve.
- Remaining budget is passed directly to the existing Milestone D scanner.
- Existing A–D modules are reused; no second capture, scoring, UART, or
  profile-table implementation was added.
- Low-confidence/ambiguous measurement retries up to the fixed attempt limit,
  then transitions to scanning and finally fallback.

Fallback stops active temporary ownership, restores USART3 to 115200 8N1,
restores slave ID 1 in the custom runtime, and leaves normal Modbus polling to
continue. Capture cleanup is guarded so an inactive capture is not disturbed.

### Task integration

`App/Application/Tasks/task_modbus.c` now initializes the existing USART3/DMA
transport once, runs the coordinator only when autobaud is enabled, services
the existing Modbus frame path during startup, and enters the normal loop after
a terminal result. The legacy `uart3_autobaud_lock()` is bypassed only for the
enabled custom-engine build and remains available for rollback.

### Compile-time control

`App/Middleware/ModbusEngine/Inc/modbus_engine_config.h` defines
`MODBUS_AUTOBAUD_ENABLE` as `0` by default, validates its value, and rejects
enabling it when the custom engine is disabled. `CMakeLists.txt` exposes the
cache variable and registers the coordinator source. FreeModbus remains a
separate compile-time path when `MODBUS_ENGINE_ACTIVE=0`.

## Verification

Host policy fixtures passed:

```text
python tests/test_modbus_autobaud_startup.py   PASS
python tests/test_modbus_autobaud_score.py     PASS
python tests/test_modbus_autobaud_validate.py  PASS
python tests/test_modbus_autobaud_scan.py      PASS
```

The dependency-free C startup fixture passed ARM GCC syntax compilation with
`-Wall -Wextra -Werror -fsyntax-only`.

The CubeIDE-generated Debug configuration, with autobaud default disabled,
built successfully with the STM32 GNU toolchain:

```text
Finished building target: Vimot_H563RITX_Professional.elf
text 193176, data 152, bss 472272
0 errors, 0 warnings
```

The standalone CMake enabled-profile configure reached compiler identification
but its bare-metal compiler-ID link test lacked the project's embedded
`nosys`/startup linker context. Therefore an enabled CMake linked image is not
claimed until configured through the project's CubeIDE embedded toolchain or an
equivalent linker preset.

## Scope compliance and limitations

No vibration DSP, IIS3DWB driver, register map, or RS485 TX timing code was
changed by Milestone E. No permanent RTOS task was added. Live TIM2/PC4
capture, framing detection, and RS485 bus behavior still require hardware
verification in the next hardware milestone.
