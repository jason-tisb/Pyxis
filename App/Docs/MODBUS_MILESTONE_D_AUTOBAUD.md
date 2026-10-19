# MODBUS MILESTONE D — AUTOBAUD PROFILE SCANNER COMPLETION REPORT

Project: Pyxis 3-Axis Vibration Sensor  
MCU: STM32H563RIT6  
Milestone: D — profile fallback scanner  
Status: COMPLETE (software scope)  
Date: 2026-10-15

## 1. Implementation summary

Milestone D adds a nonblocking, caller-budgeted profile scanner. It is not
connected to startup and therefore does not change the existing fixed
115200/8N1 commissioning behavior. A future Milestone E controller can start
the scanner after measurement/framing validation has failed.

The scanner applies the existing Milestone C transactional UART API one
profile at a time, observes the existing Modbus runtime validation result, and
locks only on a newer CRC-valid, structurally valid request addressed to slave
ID 1. It never parses bytes, calculates CRC, creates RTOS objects, or owns DMA.

## 2. Profile table and timing

The constant table contains all 20 profiles in the required order:

1. 19200/8E1, 9600/8E1, 38400/8E1, 57600/8E1, 115200/8E1
2. 19200/8N2, 9600/8N2, 38400/8N2, 57600/8N2, 115200/8N2
3. 19200/8O1, 9600/8O1, 38400/8O1, 57600/8O1, 115200/8O1
4. 19200/8N1, 9600/8N1, 38400/8N1, 57600/8N1, 115200/8N1

Each candidate uses a maximum 1200 ms dwell. `Start(budget_ms)` rejects a
zero budget and never extends the supplied budget. Candidate timeout is the
minimum of 1200 ms and the remaining scan budget. Profile index wraps from 20
back to 1 while budget remains.

## 3. State machine and recovery

`ModbusAutobaudScan_Start()` saves the known-good UART configuration and
starts profile 1. `Service()` is nonblocking; the caller owns scheduling and
may yield between calls. It advances on candidate timeout, locks on validator
success, and reports an explicit error for configuration/recovery failure.

The scanner records elapsed time, profiles attempted, unique profiles visited,
completed cycles, selected profile, and recovery failure. A timeout or
cancellation restores the saved pre-scan configuration. A successful lock
retains the selected profile. No profile is started while the previous
transition is incomplete.

The validator received a narrow `StartWithTimeout()` extension so the scanner
can enforce 1200 ms without changing Milestone C's normal 2000 ms default.
Every candidate still uses the same UART/DMA rollback path and generation-safe
runtime observation.

## 4. Files

| File | Change | Purpose |
|---|---|---|
| `App/Middleware/ModbusEngine/Inc/modbus_autobaud_scan.h` | Added | Scanner states, result and API |
| `App/Middleware/ModbusEngine/Src/modbus_autobaud_scan.c` | Added | 20-profile cooperative state machine |
| `App/Middleware/ModbusEngine/Inc/modbus_autobaud_validate.h` | Modified | Configurable single-candidate timeout API |
| `App/Middleware/ModbusEngine/Src/modbus_autobaud_validate.c` | Modified | `StartWithTimeout()` implementation |
| `CMakeLists.txt` | Modified | Scanner source registration |
| `Debug/App/Middleware/ModbusEngine/Src/subdir.mk` | Modified | CubeIDE source/object/dependency registration |
| `Debug/objects.list` | Modified | CubeIDE link registration |
| `tests/test_modbus_autobaud_scan.py` | Added | Host scanner policy and deadline tests |
| `tests/test_modbus_autobaud_scan.c` | Added | Dependency-free native-style profile fixture |

## 5. Software verification

The host tests passed:

```text
python tests/test_modbus_autobaud_scan.py
modbus autobaud scan policy: PASS

python tests/test_modbus_autobaud_score.py
modbus autobaud score fixtures: PASS

python tests/test_modbus_autobaud_validate.py
modbus autobaud validation policy: PASS
```

The scanner tests cover all framing groups, profiles 1/5/10/15/20, failed
candidates, 1200 ms dwell, wraparound, zero-budget rejection and the 21-second
remaining-budget example. The C fixture is included for a native test harness;
the current environment has no host C compiler, so it was not linked here.

CubeIDE Debug firmware build completed successfully:

```text
Finished building target: Vimot_H563RITX_Professional.elf
text 193176, data 152, bss 472272
0 errors, 0 warnings
```

## 6. Scope compliance and deferrals

Milestones A, B and C capture/scoring/transport/parser implementations were
not redesigned. `task_modbus.c` startup flow, `uart3_autobaud_lock()`, flash
persistence, profile auto-start, register mapping, vibration processing, TX/DE
timing and Milestone F diagnostics were left unchanged. Physical RS485 testing
and live framing lock remain deferred to Milestone G. Milestone E must provide
the authorized Modbus-task integration and startup policy.
