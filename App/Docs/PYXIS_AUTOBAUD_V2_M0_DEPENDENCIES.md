# Pyxis Unified Autobaud V2 — M0 Migration Inventory

Source of truth: Titania `feature/unified-autobaud` commit `ea9ecbbd2b596f733c8207150b7c6746c5146918`, Pyxis `main` commit `76cc40b50fd38eb1a53972e8c8cc0f4bb92edfa4`. This is an audit, not a statement that any modules have already been copied.

## Portable Titania modules (M1–M3 / M6)

| Titania source/header stem under Core/{Inc,Src} | Role | Target | Reuse strategy |
| --- | --- | --- | --- |
| `autobaud_v2` | Bauds, modes, result contracts | M1 | Reuse; eliminate dependency on Titania-specific `autobaud.h` and `hw_autobaud_capture.h` |
| `autobaud_activity` | Qualify RX/error evidence | M2 | Copy/test; adapter supplies counters |
| `autobaud_score` | Timer-interval classification | M2 | Copy/test; portable measurement type |
| `autobaud_validate` | Any-ID CRC/structure validator | M2 | Copy/test; adapt to existing Pyxis `modbus_crc` or independent CRC contract |
| `autobaud_v2_evidence` | Fresh, error-free receive evidence | M2 | Copy/test; define portable RX metadata |
| `autobaud_v2_candidate` | Single framing candidate verification | M2/M6 | Copy/test; uses passive port callbacks |
| `autobaud_timestamp32` | Timer overflow/epoch integrity arithmetic | M2 | Copy/test; board-independent |
| `autobaud_ondemand` | Cooperative WAIT/MEASURE/FRAME/COOLDOWN/LOCK/FAULT | M3 | Copy/test; replace hardware capture header dependency |
| `autobaud_uart_transaction` | Claim/quiesce/apply/verify/rollback supervisor | M3/M4 | Copy portable core; implement separate Pyxis adapter |
| `autobaud_v2_framing` | Passive candidate scheduling and lock | M3/M6 | Copy portable engine; Pyxis passive frame API |
| `autobaud_v2_scan` and `autobaud_v2_search` | Older 20-profile scanner and boot search | Defer | **Not invoked** by Titania's current on-demand supervisor. Do not accidentally reintroduce boot scanning |
| `autobaud_startup` and `autobaud_boot_safety` | Titania boot policy / physical DO safety | Do not copy | Not appropriate for Pyxis vibration product |
| `autobaud_hw_diag` | Titania diagnostics | M7 optional | Map to Pyxis telemetry instead of copying board-specific application policy |

Titania reusable sources live at `Core/Src/<stem>.c` and headers `Core/Inc/<stem>.h`. Some headers are superficially HAL-independent yet include board-oriented types indirectly; remove/transplant those contracts into the new platform-neutral interface in M1.

## Hardware/transport-specific ports (must be rewritten)

| Titania file | Pyxis replacement/owner | Milestone |
| --- | --- | --- |
| `hw_autobaud_ondemand_port_stm32h523.c` | Pyxis on-demand adapter to existing Modbus task/transport | M4/M6 |
| `hw_autobaud_capture_stm32h523.c` | TIM2 CH4 + PC4 AF1↔AF7, physical capture, measured clock | M5 |
| `autobaud_uart_transaction_platform.c` | USART3 + GPDMA1 Ch2 transactional adapter | M4 |
| `autobaud_v2_framing_port.c` | Passive RX session and CRC evidence bridge | M6 |
| `autobaud_v2_search_port.c`, `autobaud_startup_port.c` | No direct V2 on-demand counterpart | None |
| `hw_platform_stm32h523.c`, `hw_uart_stm32.c`, `rs485.c` | Existing Pyxis STM32H563 hardware facilities | M4/M5 |

## Existing Pyxis files and integration ownership

| Pyxis source | Current responsibility | Changes allowed |
| --- | --- | --- |
| `App/Application/Tasks/task_modbus.c` | Modbus task boot/communication orchestration | M7 only; replace startup V2 policy with on-demand service |
| `App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c` | USART3 RX DMA/worker, RX events, UART reconfiguration | M4/M6; ensure IRQ/task ownership safety |
| `App/Middleware/ModbusEngine/Src/modbus_engine_port.c` | RTU framing/port engine | M6 only if needed |
| `App/Middleware/ModbusEngine/Src/modbus_engine_tx.c` | TX and software RS-485 DE | M4 verification; prefer no changes |
| `App/Middleware/ModbusEngine/Src/modbus_engine_runtime.c` | Existing request execution and active settings | M6 or M7 only if required; no calls from passive evidence |
| `App/Middleware/ModbusEngine/Src/modbus_autobaud_capture.c` | Existing TIM2 backend | M5 reference, do not remove until after M8 |
| `App/Application/Modules/Modbus/modbus_module.c` | Stored config and baud validation | M7: auto-selector compatibility; explicitly review 57600 |
| `App/Product/RegisterMap/Modbus/modbus_app_adapter.c` | Baud register encoding/decoding | M7 or later: 57600 support requires register map compatibility review |
| `App/Middleware/ModbusEngine/Src/modbus_persistence.c` | Dual-slot settings and atomic write lifecycle | Preserve |
| `App/Application/Tasks/task_vibration.c` | Sensor FIFO, SPI DMA, DSP | Preserve; performance acceptance in M8 |
| `Core/Src/stm32h5xx_hal_msp.c`, `Core/Src/stm32h5xx_it.c` | Peripheral setup/interrupt routing | M5 if needed |

## Confirmed mismatches / decisions required

1. **Initial listener:** Pyxis saved/default settings are 9600 8N1, whereas Titania V2 listens at 9600 8E1. A special automatic-mode listener must not overwrite persistent fixed settings.
2. **Baud configuration:** Pyxis scorer/scanner/UART switching support 57600, while `modbus_module.c` and `modbus_app_adapter.c` do not expose 57600 as a normal persisted selection. Decide whether only automatic detection supports 57600 or a register-map-compatible fixed selection is required.
3. **CRC evidence:** Pyxis startup validator currently requires an addressed successful runtime request. Titania V2 allows a valid request addressed to any legal ID and does not execute it. Create a **separate passive RX pipeline** for on-demand evidence.
4. **First-frame quarantine:** No candidate request may perform register writes, trigger resets, persist config, or send a response. Clear stale queues and require a fresh verified RX session.
5. **TX/DMA ownership:** Pyxis uses FreeRTOS worker + ReceiveToIdle DMA; Titania uses its own bare-metal cooperative port. Explicitly synchronize callback, worker, frame queue, TX busy, DE and ring state before switching UART ownership.
6. **8N1/8N2 distinction:** A CRC-valid received frame alone cannot independently prove a 1-vs-2 stop-bit transmitter format. Preserve the source's diagnostic caveat and avoid false certainty.
7. **On-demand timing:** Titania captures up to 12s and validates framing over 52s with 12s candidate dwell. Confirm poll-cycle behavior from 100ms to 10s+ and preserve uninterrupted sensor tasks.
8. **Reacquisition:** Titania lock is sticky until reboot; master silence does not automatically rearm. Treat explicit rearm or framing-error reacquisition as a separate feature request, not part of the migration.
9. **Legacy scanner:** Titania on-demand path does not call its older 20-profile fallback search. Defer scanner porting unless measurements on live hardware justify it.
10. **Build policy:** Titania source declares V2 OFF then forces it ON in CMake. Do not carry that conflicting policy into Pyxis; V2 must be default-OFF until qualified.
11. **Error policy:** RX recovery failures may latch a communication fault, but vibration acquisition must not be stopped merely because RS-485 is absent or faulty.
12. **HW confidence:** Titania feature-branch CI passed, but full physical RS-485/ISR/PB5 qualification remains open. Hardware timing and recovery should not be assumed equivalent on STM32H563.

## Proposed directory (M1; not yet created)

```text
App/Middleware/AutobaudV2/Inc/
App/Middleware/AutobaudV2/Src/
App/Application/Ports/pyxis_autobaud_port.h
App/Application/Ports/pyxis_autobaud_port.c
```

No STM32 HAL headers, HAL types, UART instance names, TIM2, DMA channel, GPIO pin or application-specific objects in `AutobaudV2/Inc` or portable `AutobaudV2/Src`. Test portability with standalone native GCC `-Wall -Wextra -Werror` builds and multiple mock adapters. Move hardware code to the adapter layer.

## Safety/rollback

- Keep custom-engine fixed communication, old startup autobaud and FreeModbus selectable until M8.
- No source-file deletion or existing-symbol renaming before safe cutover.
- Each milestone must include its own test evidence, diff review and rollback commit.
- Use `main` baseline SHA for restoration or compare the feature branch with `main` at any point.
