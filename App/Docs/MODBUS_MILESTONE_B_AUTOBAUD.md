# Modbus Autobaud Milestone B — Dynamic Baud Scoring

Project: Pyxis 3-axis vibration sensor  
MCU: STM32H563RIT6  
Status: COMPLETE (software scope)  
Hardware verification: PENDING — Milestone G

## 1. Implementation summary

Milestone B adds a hardware-independent baud classifier. It consumes a stable
completed-capture snapshot from Milestone A and does not access HAL, FreeRTOS,
USART3, GPIO, DMA, or TIM2. The implementation preserves Titania's integer
multiple, residual, acceptance-ratio, candidate-ranking, margin, and confidence
math. No UART setting is changed and no autobaud startup policy is enabled.

## 2. Files changed

| File | Change | Purpose |
|---|---|---|
| `App/Middleware/ModbusEngine/Inc/modbus_autobaud_score.h` | Added | Status, diagnostic result, and scoring API |
| `App/Middleware/ModbusEngine/Src/modbus_autobaud_score.c` | Added | Five-baud Titania-compatible classifier |
| `CMakeLists.txt` | Modified | Registers scoring source |
| `Debug/App/Middleware/ModbusEngine/Src/subdir.mk` | Modified | CubeIDE compiler source/dependency registration |
| `Debug/objects.list` | Modified | CubeIDE link registration |
| `tests/test_modbus_autobaud_score.c` | Added | Embedded-style deterministic test vectors |
| `tests/test_modbus_autobaud_score.py` | Added | Host-runnable deterministic fixture tests |

Milestone A capture files were not changed.

## 3. API and capture integration

Public API:

```c
modbus_autobaud_score_status_t ModbusAutobaudScore_Evaluate(
    const uint32_t *intervals,
    uint16_t interval_count,
    uint32_t timer_frequency_hz,
    modbus_autobaud_score_result_t *result);
```

`ModbusAutobaudCapture_GetResult()` already exposes the complete 64-element
interval array, valid interval count, timer frequency, timeout state, and
overcapture state. Scoring must therefore be called only after capture service
has completed and TIM2 has stopped. No live ISR-owned data is read.

## 4. Algorithm configuration

Supported baud rates:

```text
9600, 19200, 38400, 57600, 115200
```

Preserved constants:

| Parameter | Value |
|---|---:|
| Maximum intervals | 64 |
| Minimum accepted intervals | 8 |
| Maximum run length | 12 bits |
| Minimum capture interval | 65 ticks |
| Maximum residual | 250 permille |
| Minimum acceptance ratio | 650 permille |
| Minimum winner margin | 150 permille |
| Confidence threshold | 750 permille |

Expected bit ticks are calculated with Titania's rounded integer division:

```text
round(timer_frequency_hz / candidate_baud)
```

Each measured interval is assigned to its nearest integer bit multiple. The
candidate score is:

```text
rounded_average_residual + rounded_rejected_ratio
```

Lower scores win; equal scores prefer the candidate with more accepted
intervals. Confidence is the average of acceptance ratio and winner margin.

## 5. Synthetic scoring evidence

The host fixture timer frequency was 10,666,667 Hz.

| Input fixture | Best baud | Best score | Runner-up | Runner-up score | Accepted | Confidence | Result |
|---:|---:|---:|---:|---:|---:|---:|---|
| 9600 | 9600 | 0 | 19200 | 3 | 12 | 1000 | VALID |
| 19200 | 19200 | 0 | 38400 | 333 | 12 | 1000 | VALID |
| 38400 | 38400 | 0 | 115200 | 349 | 12 | 1000 | VALID |
| 57600 | 57600 | 0 | 115200 | 349 | 12 | 1000 | VALID |
| 115200 | 115200 | 11 | 57600 | 583 | 12 | 990 | VALID |

The fixtures contain long and short synthetic edge intervals so that the
candidate set is not indistinguishable by simple harmonic multiples alone.

## 6. Negative and ambiguity tests

The host test also covers:

- null interval pointer → error;
- zero timer frequency → error;
- fewer than eight usable intervals → insufficient;
- intervals below 65 ticks → insufficient/ambiguous, never accepted;
- noisy/harmonic-like sequence with winner margin below the threshold →
  ambiguous;
- repeated deterministic evaluation → identical result.

Run:

```powershell
python tests/test_modbus_autobaud_score.py
```

Observed result:

```text
modbus autobaud score fixtures: PASS
```

## 7. Firmware build verification

CubeIDE-generated build was executed with the existing Debug configuration:

```powershell
make -C Debug -j6 all
```

Result:

```text
Build completed successfully
0 new compiler errors
0 new compiler warnings
FLASH: 193176 bytes
RAM:   472272 bytes
```

The scoring translation unit was also compiled directly with the STM32 GCC
toolchain. The existing fixed communication settings remain unchanged in the
build configuration; this milestone does not call the scoring API from the
startup path.

## 8. Scope compliance

The following were not implemented or changed:

- UART baud/parity/stop-bit reconfiguration;
- framing validation;
- CRC-based locking;
- profile fallback scanning;
- startup integration;
- persistent configuration;
- Modbus parser or register map;
- vibration processing;
- RS485 DE/TX timing;
- USART3/DMA ownership logic;
- FreeModbus fallback behavior.

## 9. Hardware verification deferred

The following remain PENDING for Milestone G:

- live TIM2 edge capture;
- actual PC4/RS485 waveform validation;
- live baud classification from RS485 traffic;
- UART/DMA recovery after capture;
- end-to-end automatic Modbus lock;
- noise and overcapture behavior on hardware.

Milestone B is software-complete and ready for review before Milestone C.
