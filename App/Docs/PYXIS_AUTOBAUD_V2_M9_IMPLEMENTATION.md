# Pyxis Autobaud V2 M9 — on-demand robustness and guarded reacquisition

**Base:** `feature/pyxis-autobaud-v2-m8` (M8 software baseline).  
**Feature branch:** `feature/pyxis-autobaud-v2-m9`.  
**Status:** CODE CHANGE FOR REVIEW; native and ARM CI plus live MCU qualification are required before any production merge or device flashing.

## Goals and preserved policy

- No startup timer capture, no blocking 20-profile scan, and no requirement for a Modbus master. Pyxis vibration processing remains independent.
- In Auto mode (persisted baud selector 0), provisional passive USART3 listener is 9600 8E1. A **complete trusted CRC-valid request to any legal slave ID** can lock that profile immediately. It is consumed and not executed or answered. Only requests addressed to Pyxis receive normal replies after lock.
- No detection request may cause Modbus writes, commands, persistence, or responses.
- The initial profile's first RX activity **arms** the 500ms observation window. Mere timeout after the first request does not initiate capture. Capture requires **fresh qualifying traffic after that window**. A CRC-valid first request still locks immediately.
- M9 post-lock reacquisition, when explicitly compiled in, is driven only by *repeated USART3 FE/PE callback evidence*, not missing Modbus polls, CRC-only failures, or elapsed idle time.
- Never update persisted baud/parity/slave ID during detection; settings remain APPLY_ON_BOOT only. Device boot remains under the H563 bootloader in the first 128 KB.

## Changes

1. `autobaud_initial_window.c`: after a first RX/error/invalid candidate, 500ms grace is granted for CRC-valid fast lock. No capture merely on expiry. A later RX/error burst must pass the existing `autobaud_activity_observe` qualification. Invalid stale candidate flags never replay as second activity.
2. `autobaud_reacquire.h/.c`: new portable, bounded FE/PE correlation guard. Default thresholds: at least **8** UART framing/parity error callbacks in **two separated observations** (50ms or more apart) within **30s**. A successful normal Modbus response resets suspicion. A lack of new errors cannot trigger.
3. `portserial.c`: isolated, monotonic FE/PE callback counter. Does not confuse protocol CRC failures or unrelated DMA overruns with framing-change evidence.
4. `pyxis_autobaud_v2_stm32h563.c`: opt-in LOCKED to REARMING to LISTENING path. Reject normal Modbus requests while changing UART; reuse M4 hardware claim/quiesce/apply/timing/restart/verify transaction. On verified successful switch: reset passive listener and reinitialize on-demand supervisor. On BUSY, retry at 1ms task cadence for up to 2s then preserve old locked profile; on verified rollback, preserve old locked profile; on unverified recovery, latch communication fault. Do not interrupt vibration sampling or apply flash writes.
5. `task_modbus.c`: notify the guard after a successful Modbus reply, preventing isolated noise from accumulating across healthy communication.
6. Tests/CI: host tests for no traffic, one burst then silence, later burst, UART error grouping, wraparound, guard cancellation, verified rearm, BUSY timeout, verified rollback, unrecoverable error; inherited M0–M8; full ARM firmware links and application flash-address assertion.

## Build gate

Existing Pyxis M8 Auto requires:
```text
MODBUS_ENGINE_ACTIVE=1
MODBUS_ENGINE_FORCE_FIXED_COMM=0
MODBUS_AUTOBAUD_ENABLE=0
MODBUS_AUTOBAUD_V2_PORT_ENABLE=1
MODBUS_AUTOBAUD_V2_CAPTURE_ENABLE=1
MODBUS_AUTOBAUD_V2_FRAMING_ENABLE=1
MODBUS_AUTOBAUD_V2_ENABLE=1
```
M9 adds **one independent, default-OFF opt-in**:
```text
MODBUS_AUTOBAUD_V2_REACQUIRE_ENABLE=1
```
This flag is illegal without full on-demand V2. No default or CMake preset enables it. When omitted, the historical M8 post-lock sticky behavior is retained, while the M9 first/second traffic fix is present in V2 Auto builds.

## Software qualification

```sh
python3 tests/test_pyxis_autobaud_v2_m9.py
```
GitHub Actions workflow: `.github/workflows/pyxis-autobaud-v2-m9.yml`.

Software tests do **not** establish live STM32 USART errors, DMA ownership, Modbus RTU timing, or FFT continuity.

## Required hardware gates before enabling recovery

1. Confirm correct MCU device ID 0x484, restored V1.3 bootloader vector table 0x08000000, and M9 app vector table 0x08020000. No mass erase or unrelated binaries.
2. No master for two minutes: vibration/SYS heartbeats continue; capture_attempts and rearm_requests stay zero.
3. First request to *another* valid ID at 9600 8E1 locks without response. Next request to Pyxis ID responds.
4. Wrong-baud first request then silence: **no capture** after 500ms, 12s or 2min. Fresh follow-up master traffic starts capture; a legal CRC-valid any-ID frame establishes profile with no response.
5. Repeat all 20 profiles at 100ms, 1s and 10s polls; verify end-to-end response only to configured slave ID and report 8N1/8N2 ambiguity.
6. Locked master disconnected and reconnected unchanged: no reacquisition on silence or single FE/PE disturbance.
7. Change master from locked 9600 8E1 to 115200 8O1 and verify repeated FE/PE qualification, nonblocking rearm, restored PC4 USART3 AF7 and GPDMA2 ownership, followed by CRC lock; repeat at different poll periods. **If the physical UART produces too few FE/PE callback events at slow polls, the opt-in recovery may stay locked: adjust based on measurements, not guesses.**
8. Force UART claim BUSY, verified rollback, unverified restart and injected IRQ/overflow faults. Only verified old/new UART state may respond.
9. Run vibration FIFO/FFT stress alongside RS-485 at all phases; record sampling overruns and max rearm latency.
10. Power cycle with stored Auto and fixed modes; fixed settings never rearm or alter persisted communication.

**Important:** No firmware is flashed by these GitHub edits. Preserve the known-good M8 device and 2 MB recovery backup until on-device qualification passes. Do not merge M9 into main or enable rearm for production based on software tests alone.
