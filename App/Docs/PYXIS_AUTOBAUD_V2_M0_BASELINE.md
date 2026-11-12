# Pyxis Unified Autobaud V2 — M0 Baseline and Migration Gate

**Status:** M0 source audit and documentation implemented; hardware/ARM build qualification pending.
**Date:** 2026-11-12
**Pyxis baseline:** `main` at `76cc40b50fd38eb1a53972e8c8cc0f4bb92edfa4`
**Titania source:** `feature/unified-autobaud` at `ea9ecbbd2b596f733c8207150b7c6746c5146918`
**Working branch:** `feature/pyxis-autobaud-v2`

## M0 boundary

M0 is **audit + reproducible baseline documentation only**. No executable firmware, CMake flags, ISR, UART, DMA, Modbus register map, or vibration code may be modified. Do not copy Titania source in M0. Subsequent changes must be individually reviewable, testable and reversible; do not merge into `main` until M8 release qualification.

## As-inspected active Pyxis configuration

From `CMakeLists.txt`, `CMakePresets.json`, `modbus_engine_config.h` and task sources:

| Setting | Current value | Meaning |
| --- | --- | --- |
| `MODBUS_ENGINE_ACTIVE` | 1 | Custom Modbus engine |
| `MODBUS_ENGINE_FORCE_FIXED_COMM` | 0 | Use saved/default communication settings |
| `MODBUS_AUTOBAUD_ENABLE` | 0 | Existing startup autobaud disabled |
| `MODBUS_PYXIS_DMA_ABSOLUTE` | 0 | Existing RX ring/worker transport |
| `MODBUS_PYXIS_HW_RTO` | 0 | HW RTO not enabled |
| `MODBUS_PYXIS_TX_IT` | 1 | Interrupt-driven transmit |
| `MODBUS_PYXIS_HW_DE` | 0 | Software DE on PB14 |

Default saved-configuration recovery in `App/Application/Modules/Modbus/modbus_module.c`: slave ID **3**, baud **9600**, parity code **3 = 8N1**. This is different from older fixed-commissioning notes (slave 1, 115200 8N1). Actual devices can use persisted values; neither documentation nor a build preset overrides valid stored settings.

Existing custom autobaud is enabled only when `MODBUS_AUTOBAUD_ENABLE=1` **and** saved baud equals zero. The current `task_modbus.c` starts a startup coordinator and loops over `ModbusAutobaudStartup_Service()` before entering its regular Modbus task processing loop. The vibration task is separately scheduled, but Modbus initialization is delayed. While `MODBUS_AUTOBAUD_ENABLE=0`, saved baud zero is normally rejected by `ModbusModule_ValidateConfig()`; an old blocking UART hardware-autobaud path nevertheless exists in the source behind that build guard.

## Protected firmware paths

Do not modify these during M0–M3:
- `App/Application/Tasks/task_modbus.c`
- `App/Application/Tasks/task_vibration.c`
- `App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c`
- `App/Middleware/ModbusEngine/Src/modbus_engine_runtime.c`
- `App/Middleware/ModbusEngine/Src/modbus_persistence.c`
- `App/Product/RegisterMap/Modbus/`
- `Core/Src/main.c`, `Core/Src/stm32h5xx_it.c`, `Core/Src/stm32h5xx_hal_msp.c`
- `CMakeLists.txt`, `CMakePresets.json`

Later milestones may touch these only where scoped, with explicit comparative tests.

## Existing hardware/transport (verified in source)

- STM32H563RIT6; USART3 TX PB10, RX PC4, RS485 DE PB14.
- Existing autobaud temporarily changes PC4 from USART3_RX AF7 to TIM2_CH4 AF1. Restore-and-verify policy will be revisited in M4–M5.
- GPDMA1 Channel2 used for USART3 RX; `HAL_UARTEx_ReceiveToIdle_DMA` with circular buffer, callback/worker queue and task-side frame assembly.
- Vibration IIS3DWB is on separate SPI1 DMA channels, but performance and concurrency still require physical qualification.
- Modbus request processing and response-before-persistence semantics belong to existing Pyxis runtime; must never be executed from passive autobaud evidence.

## Build and test evidence at M0

- **Verified by repository inspection:** original baseline commit, above compile-time defaults, existing host fixtures, source structure and latest Titania reference commit.
- **Not verified:** clean local ARM GCC build on the current commit, live Modbus RTU, sensor FFT/FIFO performance, electrical capture and restore behavior.
- No GitHub Actions workflow runs were found for Pyxis `main` at inspection; historical build/test claims in `App/Docs/MODBUS_MILESTONE_E_AUTOBAUD.md` are **not** a fresh M0 test run.
- The current GitHub connector provides read/write repository access; the execution environment did not expose a checkout of this private repository or an ARM GCC toolchain. Accordingly, no new on-target or full ARM build pass is claimed.
- `tests/test_pyxis_autobaud_v2_m0.py` supplies a repeatable, read-only baseline policy audit for a developer checkout. Execute and record its result before M1.

## Proposed build selections (planning only; do NOT add flags in M0)

| Mode | Custom | Legacy autobaud | New V2 | Intended use |
| --- | --- | --- | --- | --- |
| A | 1 | 0 | 0 | Known-good default/fixed settings |
| B | 1 | 1 | 0 | Existing startup autobaud regression |
| C | 1 | 0 | 1 | New on-demand V2 (M7 only) |
| D | 0 | 0 | 0 | FreeModbus fallback |

For M7 propose a distinct compile-time switch `MODBUS_AUTOBAUD_V2_ENABLE` with default 0. A guard must reject concurrent legacy + V2, or V2 with custom engine disabled. Explicit `baud_rate == 0` must remain the user's automatic-selection semantics. Preserve existing fixed-mode defaults. Initial automatic listener should be deliberate (Titania 9600 8E1) and never confused with the Pyxis fixed 9600 8N1 recovery defaults.

## M0 completion / next gate

1. Source baseline and vendor reference commits pinned — **PASS**.
2. Transport, task, sensor, persistence and build policy mapped — **PASS**.
3. Dependency inventory and regression plan checked in — **PASS**.
4. No firmware or build path changes in M0 — verify via branch diff before review.
5. Fresh host audit test result and ARM build — **PENDING execution on a suitable checkout/toolchain**.
6. Live hardware qualification — **NOT in M0**.

Only begin M1 when the audit is reviewed and the fixed/default Pyxis baseline is reproducible. Do not merge V2 until final live hardware qualification.
