# Pyxis Unified Autobaud V2 — M1 Portable Interface Contracts

**Status:** Source integration implemented on the dedicated M1 branch; native host execution is separately recorded when performed. Physical/electrical tests intentionally deferred by the product owner.

**Source:** Titania `feature/unified-autobaud` commit `ea9ecbbd2b596f733c8207150b7c6746c5146918`.
**Pyxis starting point:** M0 branch `feature/pyxis-autobaud-v2`, commit `eabcc2b946e93d192c1f8a5ac35c584551590a37`.

## Scope and files

- `App/Middleware/AutobaudV2/Inc/autobaud_v2.h` — self-contained Titania-compatible baud/framing/profile/state/result types, with no dependency on Titania's `autobaud.h`.
- `App/Middleware/AutobaudV2/Src/autobaud_v2.c` — verbatim-style portable profile/state helpers from Titania, with comments tailored to Pyxis; not added to embedded build.
- `App/Middleware/AutobaudV2/Inc/autobaud_v2_measurement.h` — physical-measurement result and async capture status *types only*, extracted from Titania's `hw_autobaud_capture.h` without its HAL-adjacent `hw_common.h`.
- `App/Middleware/AutobaudV2/Inc/autobaud_ondemand.h` — portable on-demand supervisor state/operations contract and timing constants; implementation arrives in M3.
- `App/Application/Ports/pyxis_autobaud_port.h` — product-facing facade declarations; implementation arrives in M4–M7.
- `tests/test_pyxis_autobaud_v2_m1.c` and `.py` — native GCC/Clang contract test and portability/source-wiring audit.

## Hardware independence contract

1. Generic `AutobaudV2/Inc` and `AutobaudV2/Src` must never include STM32 HAL, CMSIS, FreeRTOS, board pin definitions, application runtime, Modbus port or transport headers.
2. UART, timer, DMA, GPIO and RS-485 hardware work is owned by Pyxis-specific adapters to be implemented in M4–M6. The existing protocol engine owns responses, register writes and persistence.
3. A captured baud estimate is never sufficient to lock the Modbus profile; a fresh complete CRC-valid, structurally accepted and error-free request is required.
4. Do not execute or respond to passive evidence frames.
5. A hardware restoration failure must be reported as a latched communication fault, without taking over the independent vibration task.
6. Do not alter user configuration, stored baud/parity/slave ID or Modbus register semantics.
7. Preserve the Titania on-demand contract: passive listen at boot, qualified RX triggers measurement, first-default-profile fast lock, cooldown/retry, sticky lock until reset.

## Important compatibility details

- Titania `autobaud_uart_mode_t` enum is **8N1=0, 8E1=1, 8O1=2, 8N2=3**, which is NOT the Pyxis Modbus holding-register parity code (**8E1=0, 8O1=1, 8N2=2, 8N1=3**). Conversion stays in the platform adapter, never the portable controller.
- The M1 profile helper recognizes five baud rates, including 57600. This does **not** change the product's current persisted/holding-register validation, which is separately addressed later.
- The earlier Titania boot scanner is **not** connected. The on-demand timing limits are 12s capture, 52s framing budget and 12s framing dwell.
- M1 does **not** provide a runnable on-demand engine. The `autobaud_ondemand.h` supervisor function declarations will gain their implementation in M3.

## Verify

```sh
python3 tests/test_pyxis_autobaud_v2_m0.py
python3 tests/test_pyxis_autobaud_v2_m1.py
```

The M1 test compiles and executes portable C code with `-std=c11 -Wall -Wextra -Werror -pedantic` and validates the public callback type compatibility and no embedded target references. The embedded target remains unchanged because M1 sources are not added to `CMakeLists.txt`.

## M1 acceptance

- [x] Platform-free types and Titania-compatible callback contract introduced.
- [x] Profile helper implementation is standalone, no HAL dependencies.
- [x] New code is not linked into production firmware.
- [x] M0 baseline branch left unchanged; work is isolated on M1 branch.
- [ ] Execute M0 + M1 host checks and record results.
- [ ] Source diff review and branch promotion.
- [ ] Physical hardware qualification (explicitly deferred to M8).

**Next M2:** Move Titania scoring/activity/validation/evidence algorithms into the portable module with native fixtures; leave active USART3/TIM2/DMA and `task_modbus.c` untouched.
