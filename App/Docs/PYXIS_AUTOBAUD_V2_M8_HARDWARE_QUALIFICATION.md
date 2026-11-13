# Pyxis Unified Autobaud V2 — M8 Physical Qualification Matrix

**Status: NOT EXECUTED.** This is an on-device test procedure for later use, not evidence of a passed hardware test.

**Target:** STM32H563, USART3 RX on PC4/AF7, TIM2 CH4 capture on PC4/AF1, GPDMA1 Channel2 RX, isolated RS-485/Modbus RTU. Keep other sensor IIS3DWB/SPI1 DMA and vibration FFT running. Repeat after any capture driver or clock change.

## Before energizing DUT

- Record PCB revision, MCU exact variant, firmware git SHA, build flags, UART peripheral clock, GPDMA configuration, RS-485 DE polarity/termination, and HSE/System clock; record master adapter, tool version, RS-485 wiring and supply voltage.
- Compile **only a dedicated M8 development image** with custom Modbus engine, `MODBUS_AUTOBAUD_ENABLE=0`, all M4–M7 V2 feature gates =1, no force-fixed override.
- Prepare software persisted configuration with `baud_rate=0` (Auto) and a known slave ID, e.g. 3; apply settings **after reboot** per existing persistence policy. If the current image has the default fixed 9600 8N1 configuration, simply enabling the build flag will NOT activate V2.
- Attach a debugger with breakpoints/counters for `PyxisAutobaudV2_GetDiagnostics`, `PyxisAutobaudUart_GetDiagnostics`, `ModbusEngineV2_IsFatal`, `g_uart3_rx_callback_count`, `g_uart3_dma_overruns`, RX error generation, and vibration sample/FIFO counters. Avoid live breakpoints during timing qualification (they distort IRQ timing); use bounded snapshots.
- For destructive FC06/FC16, use a dedicated isolated bench unit and a safe writable test register only; NEVER send potentially destructive system commands or affect a production machine.

## Expected base behavior

1. **Cold boot, no master:** main/vibration tasks start and continue. No startup 20-profile scan, no 12s capture, no communication replies, no 30s boot delay. Monitor at least 2 minutes.
2. **Initial valid request at 9600/8E1:** first *complete CRC-valid* request to **another legal slave ID** (e.g. ID1) immediately confirms framing; that evidence request gets no reply. A subsequent request to configured Pyxis ID gets the normal reply. A wrong-ID request after lock gets no reply.
3. **500ms grace:** a partial but otherwise valid request to the initial 9600/8E1 listener may finish within the observation window. Capture must not start solely because a previous valid request was incomplete. Invalid CRC or qualifying serial errors may trigger a measurement attempt.
4. **Other supported settings:** after observed RX activity and subsequent master traffic, TIM2 acquires 10MHz edge intervals, releases PC4/AF1, restores PC4/AF7, and passive CRC validation confirms active UART. No command is ever executed or answered as detection evidence.
5. **Sticky lock:** after confirmation, disconnect master for 2 minutes, reconnect using unchanged profile and check normal responses. No re-acquisition on silence.
6. **Safe recovery:** no traffic, malformed ADU, wrong baud/framing, CRC errors, RX FIFO/DMA overflow and timed-out capture must never produce a false lock or response. Recoverable attempts cool down and wait for *fresh* activity; UART restore failure latches a fault and suppresses Modbus replies, but vibration acquisition continues.
7. **Fixed configuration mode:** with persisted baud nonzero, V2 must not start or switch UART; confirm at 9600/8N1 and at one other allowed fixed setting. Legacy mode is separately build-selectable and must remain unchanged.
8. **Reboot:** validated profile lock lasts only for that boot; persisted selector remains `0` and next boot starts initial 9600/8E1 again.

## Full 20-profile matrix

For EACH of 5 baud rates and 4 framing modes, perform a fresh reboot with saved Auto selector 0, then repeat test with polling intervals **100ms, 1s, 10s**:

| Baud | 8E1 | 8N2 | 8O1 | 8N1 |
| --- | --- | --- | --- | --- |
| 9600 | Pending | Pending | Pending | Pending |
| 19200 | Pending | Pending | Pending | Pending |
| 38400 | Pending | Pending | Pending | Pending |
| 57600 | Pending | Pending | Pending | Pending |
| 115200 | Pending | Pending | Pending | Pending |

For each cell/interval record: lock or timeout, first-request fast lock vs capture, elapsed milliseconds, `autobaud_od_t` state, captured baud/confidence/sample count, verified active framing, USART BRR/CR1/CR2, GPIOC MODER/AFR, DMA CCR and UART CR3 DMAR, last UART errors, number of RX sessions, number of candidate restarts, TX response success, Modbus application side effects (must remain zero for evidence requests), and vibration sample loss/FFT cadence.

**Protocol limitation:** Passive CRC-valid Modbus traffic does not conclusively distinguish 8N1 and 8N2. Titania V2 attempts 8N2 before 8N1. Record the selected framing and mark stop bits *not independently verified*; do not claim physical proof of exact 8N1/8N2 compatibility solely from CRC success.

## Specific fault-injection cases

| Case | Stimulus | Required evidence |
| --- | --- | --- |
| F01 | No master for 2 minutes | Immediate normal application boot, no capture, no false lock |
| F02 | Master request to ID1 while Pyxis ID3 | Can confirm profile, first detection frame discarded |
| F03 | Valid Modbus broadcast FC06/FC16 to safe test register | Evidence accepted, no response, no write applied |
| F04 | Invalid broadcast read FC03 | Must not confirm |
| F05 | CRC corruption on correct UART | No lock, no register writes or reply |
| F06 | Only one IDLE fragment (split 2+3+3 bytes across callbacks) | Must assemble full RTU request before CRC validation |
| F07 | Two back-to-back valid ADUs separated by T3.5 | Must be treated as two requests, never concatenated |
| F08 | 257-byte candidate, passive queue overflow, or UART ORE | Session invalidated, no trusted lock |
| F09 | Begin capture then stop all traffic | Timeout/retry, no permanent RTOS stall |
| F10 | UART error while candidate is pending | Discard stale frame and verify recovery/new session |
| F11 | Inject DMA abort/restart error in controlled fault test | Fatal latch; normal Modbus TX inhibited; other tasks alive |
| F12 | Leave master silent after successful lock | No reacquire until power reset |
| F13 | Fixed 9600 8N1 selector with V2 compiled | Exact fixed behavior; no capture |
| F14 | Reboot after detected 115200 8E1 | Start again at 9600 8E1; configured selector still Auto |
| F15 | Continuous vibration sampling during RS-485 stress | No unexplained FIFO overrun, sampling gap or FFT degradation |

For F06/F07 record the actual RX IDLE callback timing and Modbus T3.5: 9600 8E1 ≈4.01ms (millisecond ceiling 5ms), 19200 8E1 ≈2.01ms (ceil 3ms), rates above 19200 use standard fixed 1.75ms (conservative ceil 2ms). The 1ms SysTick clock cannot substitute for an oscilloscope in borderline interframe cases; verify split/merge behavior on real traffic.

## Pass/fail criteria and exit gate

- Firmware does not hang, reboot repeatedly, disable vibration sampling, or leak DMA ownership.
- Only a fresh, complete, CRC-correct legal request can authorize lock. **Any legal slave ID can confirm baud/framing**; normal responses honor the configured slave ID.
- Detection requests cause **zero** Modbus protocol responses and **zero** application writes/persistence side effects, including broadcast writes.
- Capture stops and physically restores PC4 AF7, USART3 CR1/CR2/BRR, RX DMA and RS-485 DE. All untrusted session metadata or restoration failures fail closed.
- 5×4×3 profile/poll matrix results recorded. Confirm any 8N1/8N2 alias independently or document as an unresolved limitation; do not represent it as unique detection.
- At least one **10-minute** vibration FIFO/FFT baseline vs active RS-485 stress comparison without increase in unexpected sample loss.
- Confirm fixed-mode and default-off build smoke tests with power cycle.

**Only after these measurements pass** should the M8 branch be described as hardware-qualified or considered for an operational release. No hardware measurements are included in M8 software CI.
