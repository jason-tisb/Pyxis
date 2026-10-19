# MODBUS MILESTONE C — AUTOBAUD FRAMING VALIDATION COMPLETION REPORT

Project: Pyxis 3-Axis Vibration Sensor  
MCU: STM32H563RIT6  
Milestone: C — UART and framing validation  
Status: COMPLETE (software scope)  
Date: 2026-10-15

## 1. Implementation summary

Milestone C adds a transactional USART3 reconfiguration API and a cooperative
framing validator. It supports 9600, 19200, 38400, 57600 and 115200 baud and
the four trial formats 8E1, 8N2, 8O1 and 8N1. Reconfiguration stops and
restarts the existing Receive-to-Idle DMA transport; it does not recreate the
RX worker, event flags, parser, register map or TX driver.

The validator observes the normal `ModbusEngineRuntime_ProcessFrame()` result.
It therefore relies on the existing CRC, structural and slave-address checks,
and never parses or responds to a frame a second time. A generation is
represented by the processed-frame baseline captured at candidate start. Only
a later `MODBUS_PROC_OK` frame addressed to slave 1 can validate a candidate.
Broadcast is intentionally not accepted as a lock event.

## 2. Files changed

| File | Change | Purpose |
|---|---|---|
| `App/Middleware/ModbusEngine/Inc/modbus_engine_port.h` | Modified | Reconfigure/get-current-config APIs |
| `App/Middleware/FreeModbus/port/cmsis_rtos2/portserial.c` | Modified | Transactional HAL/DMA transition and rollback |
| `App/Middleware/ModbusEngine/Inc/modbus_engine_runtime.h` | Modified | Read-only validation observation APIs |
| `App/Middleware/ModbusEngine/Src/modbus_engine_runtime.c` | Modified | Last-result/address accessors |
| `App/Middleware/ModbusEngine/Inc/modbus_autobaud_validate.h` | Added | Validator state/result API |
| `App/Middleware/ModbusEngine/Src/modbus_autobaud_validate.c` | Added | Cooperative candidate validator |
| `CMakeLists.txt` | Modified | Registers validator source |
| `Debug/App/Middleware/ModbusEngine/Src/subdir.mk` | Modified | CubeIDE source/object/dependency registration |
| `Debug/objects.list` | Modified | CubeIDE link registration |
| `tests/test_modbus_autobaud_validate.py` | Added | Host policy tests |

## 3. UART reconfiguration

`ModbusEnginePort_ReconfigureUart()` rejects unsupported settings or an active
TX operation, clears the parser, disables RX, waits for queued DMA boundaries,
deinitializes and reinitializes only USART3, restores software DE receive
state, recalculates RTU timing, clears UART flags, verifies the USART3/DMA
association, and restarts the existing DMA worker. On failure it applies the
previous `UART_InitTypeDef` and stop-bit setting. A rollback failure returns
false and leaves the caller with an explicit failure rather than claiming a
working transport.

`ModbusEnginePort_UartInit()` remains the one-time initialization path. The
validator never calls it repeatedly, so RTOS resources are not duplicated.

## 4. Framing and validation behavior

The central table is ordered 8E1, 8N2, 8O1, 8N1. Parity modes use STM32 9-bit
word length for eight data bits plus parity; no-parity modes use 8-bit word
length. A 2-stop-bit configuration is accepted as a communication-compatible
setting, not claimed as a uniquely measured physical property.

`ModbusAutobaudValidate_Start()` applies one candidate and captures the current
processed-frame count. `ModbusAutobaudValidate_Service()` is nonblocking and
must be called by the Modbus task, with the caller free to yield between calls.
It returns VALID after one newer, addressed, CRC/structure-valid request has
been processed by the normal runtime. It returns TIMEOUT after 2000 ms and
restores the previous configuration. `Stop()` restores the previous
configuration for an unfinished trial. Recoverable UART errors remain owned
by the existing `HAL_UART_ErrorCallback()` DMA restart path.

## 5. Verification

Host test:

```text
python tests/test_modbus_autobaud_validate.py
modbus autobaud validation policy: PASS
```

The tests cover current-generation success, stale events, CRC/processing
failure, wrong address, broadcast rejection and framing order. They are policy
tests; they do not prove live UART electrical framing.

CubeIDE Make build (Debug, custom engine active) completed successfully:

```text
Finished building target: Vimot_H563RITX_Professional.elf
text 193176, data 152, bss 472272
0 errors, 0 warnings
```

## 6. Scope and deferrals

Milestones A/B capture and scoring code was not changed. No profile scanner,
startup integration, fallback sequence, flash persistence or replacement of
`uart3_autobaud_lock()` was added. TX behavior, sensor/DSP code, LED service,
DMA receive ownership and register mapping remain on their existing paths.
Live framing lock and physical RS485 verification remain deferred to the later
hardware milestone (G).
