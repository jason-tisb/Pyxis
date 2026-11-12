# Pyxis Unified Autobaud V2 — Milestone M4 implementation report

**Scope:** Source-level Pyxis USART3/GPDMA1 Channel2 hardware transaction port, isolated from the active Modbus behavior by a default-OFF build gate. Hardware-on-target testing deferred by the user. M5–M7 still required for full on-demand operation.

- Baseline: Pyxis `feature/pyxis-autobaud-v2-m3` commit `1014804bef084a3e6b50364491cbf22878a5ace2`.
- Working branch: `feature/pyxis-autobaud-v2-m4`.
- Portable transaction core: Titania `feature/unified-autobaud` at `ea9ecbbd2b596f733c8207150b7c6746c5146918`, imported in M3 without an algorithm rewrite.

## Existing files modified

1. `App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c` — under `#if MODBUS_AUTOBAUD_V2_PORT_ENABLE`:
   - Add worker park-request and parked acknowledgement so the existing high-priority RX worker is not processing rings/frames when the claimant modifies UART or DMA.
   - Introduce bounded claim delay (20 ms maximum). Reject an already-owned worker, active TX, non-receive DE, invalid UART/DMA instance or a previous fatal latch.
   - Quiesce receive-to-idle DMA, interrupt sources, parser/ring/boundary/completed-frame queues. Verify DMA disabled. Do not conflate the current RxState with a fatal error solely because HAL returns BUSY_RX during DMA.
   - Snapshot and translate UART configuration to Titania's 8E1/8O1/8N2/8N1 profile modes. Distinguish those from the *different* Pyxis persisted parity register codes.
   - Apply UART profile with existing HAL configuration helper and update RTU framing timing.
   - Verify USART3 pin selection PC4/AF7 via GPIO register readback; verify USART CR1 framing bits, CR2 stop bits, nonzero BRR, UART RX+TX enables, DMA receiver enable and expected receive session before releasing the worker.
   - On verified restart, publish a monotonically increasing nonzero RX session token and passive ownership state.
   - Count UART error callback generations. In passive mode the RX worker cannot feed normal Modbus request processing; passive candidate extraction is an M6 addition.
   - On failed rollback or physical recovery, latch fatal, inhibit receive, park worker and deassert software DE; do not touch the independent vibration task.
   - Existing custom RX transport remains unchanged when the gate is 0.

2. `App/Middleware/ModbusEngine/Src/modbus_engine_tx.c` — add guarded TX check: suppress normal Modbus transmission when V2 has claimed USART3, when passive candidate reception is active, or after fatal fault. With gate=0 existing TX logic is unaffected.
3. `App/Middleware/ModbusEngine/Inc/modbus_engine_config.h` — add default-OFF `MODBUS_AUTOBAUD_V2_PORT_ENABLE=0` compile switch; reject invalid values, FreeModbus fallback and simultaneous legacy startup autobaud.
4. `CMakeLists.txt` — expose the default-OFF M4 compile option, conditionally register only `autobaud_v2.c`, `autobaud_uart_transaction.c` and the new Pyxis board facade, and add required include paths. No M3 on-demand supervisor is registered; on-demand remains dormant.
5. `tests/test_pyxis_autobaud_v2_m1.py`, `tests/test_pyxis_autobaud_v2_m2.py`, `tests/test_pyxis_autobaud_v2_m3.py` — update inherited source-isolation checks to require M4's explicit compile-only default-OFF gate instead of forbidding *any* reference to V2 sources in CMake.

## New files

- `App/Middleware/ModbusEngine/Inc/modbus_engine_v2_transport.h`: Pyxis target transport interface. Only includes portable profile types and C standard types.
- `App/BSP/Communication/pyxis_autobaud_uart_stm32h563.h` and `.c`: board binding for Titania's transaction callbacks, keeps hardware code delegated to the existing UART worker transport; no hardcoded USART3 resources in portable middleware.
- `tests/autobaud_v2/test_pyxis_uart_adapter.c`: mock port exercising 20 profiles, passive gating, busy claim, rollback and fatal restoration failure.
- `tests/test_pyxis_autobaud_v2_m4.py`: Python runner for all M0–M4 native tests plus static default-OFF/dormant-source checks.
- `tests/autobaud_v2/check_m4_arm_syntax.sh`: compile actual guarded STM32 port and board binding using `arm-none-eabi-gcc` with the M4 flag enabled, **syntax-only** (no link).
- `.github/workflows/pyxis-autobaud-v2-m4.yml`: native host regression and ARM target syntax jobs.

## How to build/test

Default production build **unchanged**:
```text
MODBUS_ENGINE_ACTIVE=1
MODBUS_AUTOBAUD_ENABLE=0
MODBUS_AUTOBAUD_V2_PORT_ENABLE=0
```

Opt-in M4 compilation for **developer-only** ARM validation:
```text
MODBUS_ENGINE_ACTIVE=1
MODBUS_AUTOBAUD_ENABLE=0
MODBUS_AUTOBAUD_V2_PORT_ENABLE=1
```

On a developer checkout:
```sh
python3 tests/test_pyxis_autobaud_v2_m4.py
sh tests/autobaud_v2/check_m4_arm_syntax.sh
```
ARM compiler must be installed and accessible as `arm-none-eabi-gcc`. GitHub Actions runs the native and ARM jobs, but **neither flashes or tests a physical PCB**.

## Important limitations / future gates

- M4 alone **does not enable autobaud**, even with `PORT_ENABLE=1`. The Modbus task never calls the new facade. Do not claim that the MCU has learned a new UART profile yet.
- `ModbusEngineV2_SnapshotSession` provides session+error generation for M6 evidence; a passive frame-consumer API and validated handoff are still required in M6.
- M5 must provide a separately managed asynchronous TIM2 capture/PC4 pin-mux transaction; do not call the inherited synchronous `autobaud_uart_txn_capture` from the on-demand task.
- M6 must implement the initial 500 ms grace, strict CRC-based any-ID request validation, quarantine of evidence frames and no-response/no-write policy.
- M7 must gate Modbus task processing until verified lock and ensure all V2 start/services run only from the Modbus task.
- The M4 worker parking uses a bounded 20-ms task-context handshake; it **must not** be invoked from an ISR or worker itself. Physical stress tests for pending UART callbacks and DMA abort state are deferred.
- BRR nonzero and HAL configuration readback are sanity checks, not a substitute for electrical baud validation or a formal independently calculated kernel-clock/BRR check. Consider strengthening during M8 if needed.
- Fail-closed fault behavior intentionally disables the communication path, not vibration analysis.
- M4 source is not a claim of firmware image link success or physical safety verification until those checks have been run and results recorded.

## Software acceptance

- [x] New adapter and real guarded transport transaction API committed.
- [x] Existing transport remains default-OFF.
- [x] Native mock tests and inherited regressions have a CI workflow.
- [x] Cross-compiler syntax test added for the real source with gate=1.
- [ ] Record final-commit native test and cross-compiler conclusions.
- [ ] Full production firmware link comparison (deferred; no M4 runtime activation).
- [ ] Real UART/DMA/RS485 tests on hardware (deferred as requested).
