# Pyxis Unified Autobaud V2 — M5 Implementation Report

**Source baseline:** M4 `feature/pyxis-autobaud-v2-m4` at `37bb3ccd7ea8d71c948b8141911adda943ec438e`.
**Working branch:** `feature/pyxis-autobaud-v2-m5`.
**Titania V2 reference:** `feature/unified-autobaud` at `ea9ecbbd2b596f733c8207150b7c6746c5146918`.

## Scope

Implement a **dormant, default-disabled, asynchronous** TIM2 Channel 4 input-capture adapter for Pyxis STM32H563 and USART3 RX pin PC4, without activating V2 in the Modbus task. Existing Pyxis legacy capture and vibration task remain unchanged in the default firmware. All captured evidence is scored by the **same portable Titania V2** algorithm imported in M2.

## New files

| File | Purpose |
| --- | --- |
| `App/Middleware/AutobaudV2/Inc/autobaud_edge_stream.h` | Portable fixed 64-interval single-writer ISR buffer and quiet-window API |
| `App/Middleware/AutobaudV2/Src/autobaud_edge_stream.c` | Pure constant-time timestamp ingestion; modulo-32-bit edge intervals; glitch rejection; fail-closed overcapture |
| `App/BSP/Communication/pyxis_autobaud_capture_stm32h563.h` | Task-level `Begin/Poll/Cancel/IsActive/IsFatal` API, with bounded ISR callbacks |
| `App/BSP/Communication/pyxis_autobaud_capture_stm32h563.c` | Real TIM2/PC4 edge capture, verified UART receiver restoration using the M4 transport ownership seam |
| `tests/autobaud_v2/test_edge_stream.c` | C11 test: five baud candidates, timer wraparound, quiet time, short glitches, overflow boundedness and integrity latch |
| `tests/test_pyxis_autobaud_v2_m5.py` | Full M0–M5 native software regression and dormant-path audit |
| `tests/autobaud_v2/check_m5_arm_syntax.sh` | ARM source syntax build with guarded M4 and M5 enabled |
| `.github/workflows/pyxis-autobaud-v2-m5.yml` | Two CI jobs: native test + cross-compiler check of actual STM32 interrupt/capture source |

## Existing files modified

- `App/Middleware/ModbusEngine/Src/modbus_autobaud_capture.c` — reuses its one existing HAL TIM2 input-capture callback. When M5 is explicitly enabled and owns capture, dispatches the interrupt to `PyxisAutobaudCapture_OnCaptureIrq()`; otherwise retains the legacy handler unchanged.
- `Core/Src/stm32h5xx_it.c` — under the M5 compile guard, calls `PyxisAutobaudCapture_OnIrqBeforeHal()` before `HAL_TIM_IRQHandler()` so overcapture and timer update flags can be seen **before HAL clears them**.
- `App/Middleware/ModbusEngine/Inc/modbus_engine_config.h` — declares `MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=0` by default; validates 0/1, and requires M4 port switch to be enabled.
- `CMakeLists.txt` — adds the explicit default-OFF capture option, conditionally links the edge stream, M2 scorer and real Pyxis TIM2 capture adapter. No supervisor/task integration is linked or enabled.

## Capture behavior

1. Validate the requested timeout (1..12000 ms) and derive an **exact 10 MHz** TIM2 counter clock from current APB1 clock and prescaler. Reject an unsupported clock instead of silently measuring at the wrong frequency.
2. Claim the M4 USART3/GPDMA1 RX worker transaction (existing bounded ownership/DE checks), snapshot current UART framing and previous passive ownership, quiesce RX DMA and clear stale frames.
3. Initialize the existing global TIM2 handle in both-edge capture mode, channel 4, using a 32-bit auto-reload period. Switch PC4 from USART3 RX AF7 to TIM2 CH4 AF1 and **read back GPIO mux and TIM2 registers**.
4. Start TIM2 interrupt capture. **No task delay or wait-for-request loop occurs**. Edge IRQ work is constant time; it records up to 64 full 32-bit interval values in the portable stream.
5. Each foreground `Poll()` returns immediately while the capture window is still pending. A result becomes available when the stream fills, after a minimum-eight-interval burst with at least 5 ms inactivity, upon integrity error, or at the time budget.
6. Before the timer is shut down, compare the 10 MHz TIM2 counter with the independent HAL millisecond clock (with 10 ms tolerance for scheduling/tick quantization). Overcapture (CC4OF) or an unexpected TIM2 update (UIF) invalidates evidence.
7. Stop and deinitialize TIM2, remap PC4 back to AF7 and **verify physical mux restoration**. Use M4's re-quiesce/apply/timing/restart/verify/release sequence to restore the exact previous UART profile and previous passive status. Only then deliver the pure scorer's result.
8. A reliable capture produces `HW_AUTOBAUD_ASYNC_READY` with `HW_AUTOBAUD_MEASURE_VALID` and the measured baud/confidence. A recoverable missing/ambiguous/error measurement also produces READY **only after verified UART restoration**. An unsafe restoration returns `HW_AUTOBAUD_ASYNC_FAILED` and **latches communication fatal**.
9. `Cancel()` restores the old receive path even when a capture is in progress. It has no effect on vibration SPI1 DMA, FFT processing or sample flow.
10. **No Modbus frame is interpreted or executed by M5.** M6 implements any-ID CRC verification and quarantine. M7 connects the supervisor to the task, including Titania's initial 500 ms listening grace.

## Hardware independence and compatibility

- The portable `AutobaudV2` stream and score engine has no HAL, FreeRTOS, UART/DMA instance or board dependencies.
- STM32H563 TIM2 and USART3 details appear **only in the board adapter**, existing legacy capture callback and ISR routing under compile-time guard.
- The old `MODBUS_AUTOBAUD_ENABLE` behavior cannot be enabled at the same time as M4/M5. The new source is inert in a default build.
- The M3 upstream synchronous transaction capture helper is **not called** by M5. M5's `Begin/Poll` is asynchronous across a potentially 12-second acquisition, while M4's short claim/quiesce and UART re-init occur in task context.
- The worker is parked during capture; this is a Modbus receive worker only, not the vibration processing task.
- A detected baud is not a lock: the M3 supervisor/M6 passive CRC validator must confirm a complete valid Modbus RTU request before normal responses resume.

## Build selections

| Build option | Default | M5 development syntax build |
| --- | --- | --- |
| `MODBUS_ENGINE_ACTIVE` | 1 | 1 |
| `MODBUS_AUTOBAUD_ENABLE` | 0 | 0 |
| `MODBUS_AUTOBAUD_V2_PORT_ENABLE` | 0 | 1 |
| `MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE` | 0 | 1 |

Native test:
```sh
python3 tests/test_pyxis_autobaud_v2_m5.py
```

Guarded STM32 source cross-compilation:
```sh
sh tests/autobaud_v2/check_m5_arm_syntax.sh
```

## Acceptance and remaining work

- [x] Source added for asynchronous M5 capture without a busy loop.
- [x] Existing IRQ/callback reused with a default-disabled compile guard.
- [x] M4 exclusive-ownership and verified rollback used for every successful or failed measurement.
- [x] Native host and ARM syntax CI jobs defined.
- [ ] Record final M5 CI run conclusions.
- [ ] Full ARM firmware link with M4+M5 opt-in (distinct from syntax-only source compile).
- [ ] Electrical capture/IRQ saturation, max-rate RS485, GPIO restoration and vibration FIFO tests (explicitly postponed until after software milestones).

**Next milestone M6:** passive request framing/evidence, never process a capture request, fresh session/error-generation validation, atomic UART handoff, and the initial 500ms 9600 8E1 grace policy. **M7:** connect nonblocking supervisor only when automatic baud selector is enabled.
