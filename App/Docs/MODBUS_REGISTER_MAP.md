# PiLoT-V Modbus Register Map

This document maps the **current firmware implementation** to a clean Modbus register view.

It is based on the active code in:

- [App/Middleware/FreeModbus/port/user_mb_app.h](/c:/Users/PC%20YH/STM32CubeIDE/workspace_2.1.0/Vimot_H563RITX_Professional_Cmake_Systemize/App/Middleware/FreeModbus/port/user_mb_app.h)
- [App/Middleware/FreeModbus/port/user_mb_app.c](/c:/Users/PC%20YH/STM32CubeIDE/workspace_2.1.0/Vimot_H563RITX_Professional_Cmake_Systemize/App/Middleware/FreeModbus/port/user_mb_app.c)
- [App/Application/Tasks/task_modbus.c](/c:/Users/PC%20YH/STM32CubeIDE/workspace_2.1.0/Vimot_H563RITX_Professional_Cmake_Systemize/App/Application/Tasks/task_modbus.c)
- [App/Application/Tasks/task_vibration.c](/c:/Users/PC%20YH/STM32CubeIDE/workspace_2.1.0/Vimot_H563RITX_Professional_Cmake_Systemize/App/Application/Tasks/task_vibration.c)

## Important Current Behavior

- The firmware allocates `2958` canonical input registers and `12797` canonical holding registers.
- Holding registers and input registers use **separate buffers**:
  - holding: `usSRegHoldBuf[]`
  - input: `usSRegInBuf[]`
- Function code `03` reads holding (`4xxxx`) and `04` reads input (`3xxxx`) as separate data images.
- Internally, register index `0` maps to Modbus address `1`.
- After a valid Modbus configuration is loaded at startup, the application displays the slave ID, baud code, and parity setting once on the status LEDs. See [LED_BEHAVIOR.md](LED_BEHAVIOR.md) for the encoding and timing.

Recommended interpretation for documentation:

- holding (`4xxxx` / `5xxxx`) = intended configuration / command usage
- input (`3xxxx`) = intended runtime telemetry usage

## Addressing Convention

- Internal index `0` = `40001` or `30001`
- Internal index `9` = `40010` or `30010`
- Internal index `2624` = `32625`
- Internal index `2640` = `32641`

## 1. Holding Register Map

These are the registers currently used as writable configuration fields.

| Internal Index | Modbus Address | Name | Type | Current Firmware Status | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0` | `40001` | Slave ID | `UINT16` | Active | Validated in `ValidateHoldingRegister()` |
| `1` | `40002` | Baud Rate | `UINT16` | Active | `0=Auto, 1=9600, 2=19200, 3=38400, 4=115200` |
| `2` | `40003` | Parity / Stop Bit | `UINT16` | Active | `0=Even/1sb, 1=Odd/1sb, 2=None/2sb, 3=None/1sb` |
| `3` | `40004` | Response Delay | `UINT16` | Reserved | Planned communication timing control |
| `4` | `40005` | Watchdog | `UINT16` | Declared | Implemented as `MB_REG_MACHINE_PROFILE` in firmware today |
| `5` | `40006` | System Command | `UINT16` | Implemented | One-shot command word; baseline capture/reset actions are encoded here |
| `6` | `40007` | Analog Data Format | `UINT16` | Reserved | Implemented as `MB_REG_TACH_ENABLE` in firmware today |
| `7` | `40008` | Tach Source | `UINT16` | Reserved | Planned timer/capture source select |
| `8` | `40009` | Reserved | `UINT16` | Unused | Spare |
| `12544` | `52545` | Gear Ratio (x100) | `UINT16` | Declared | Default `100`; Range `1..5000` (0.01..50.00) |
| `12545` | `52546` | Base Frequency (Hz x100) | `UINT16` | Declared | Default `5000`; Range `1..65535` (0.01..655.35 Hz); stored as shaft base frequency in Hz x100 |
| `12546` | `52547` | Bearing Ball Count | `UINT16` | Declared | Default `8`; Range `3..32` |
| `12547` | `52548` | Bearing Ball Diameter (mm x100) | `UINT16` | Declared | Default `1200`; Range `1..5000` (0.01..50.00 mm) |
| `12548` | `52549` | Bearing Pitch Diameter (mm x100) | `UINT16` | Declared | Default `7200`; Range `1..7200` (0.01..72.00 mm); must be > ball diameter |
| `12549` | `52550` | Bearing Contact Angle (deg x10) | `UINT16` | Declared | Default `0`; Range `0..900` (0.0..90.0°) |
| `12550` | `52551` | High Pass Filter Cutoff (Hz) | `UINT16` | Declared | Default `10`; Range `1..65535` (firmware also enforces `high_pass < low_pass`) |
| `12551` | `52552` | Low Pass Filter Cutoff (Hz) | `UINT16` | Declared | Default `13333`; Range `1..65535` (firmware also enforces `low_pass <= sample_rate/2`) |
| `12552` | `52553` | Refresh / Apply Command | `UINT16` | Declared | Default `0`; Range `0..1` (currently not used by firmware logic) |
| `12553` | `52554` | Gear Teeth Number | `UINT16` | Declared | Default `8`; Range `1..1024`; used for gear-mesh analysis |
| `12672` - `12796` | `52673` - `52797` | Remapping Address Table | `UINT16[125]` | Declared | 125-word table starting at `MB_REG_REMAPPING_ADDR` |
| `19` - `29` | `40020` - `40030` | Reserved | `UINT16` | Unused | Available for future config |

## 2. Current Runtime Input Register Map

These are the telemetry values currently written by the vibration and Modbus tasks.

### 2.1 Communication Mirror / Basic Status

| Internal Index | Modbus Address | Name | Type | Source |
| :--- | :--- | :--- | :--- | :--- |
| `0` | `30001` | Device Code | `UINT16` | `task_modbus.c` |
| `1` | `30002` | Hardware Version | `UINT16` | `task_modbus.c` |
| `2` | `30003` | Firmware Version | `UINT16` | `task_modbus.c` |
| `3` | `30004` | Manufacturing Date | `UINT16` | `task_modbus.c` |
| `4` | `30005` | Serial Number (Upper) W0 | `UINT16` | `task_modbus.c` |
| `5` | `30006` | Serial Number (Upper) W1 | `UINT16` | `task_modbus.c` |
| `2624` | `32625` | Sensor Temperature x10 | `INT16/UINT16` | `task_vibration.c` |

Notes:

- `32625` stores temperature in `x10` scale.
- Example: `253` means `25.3 C`.

### 2.2 Vibration Analytics Result Blocks

The vibration task writes `3 x 50` words into three separate 50-word blocks:

- X-axis block: internal `2640` to `2689` -> `32641` to `32690`
- Y-axis block: internal `2736` to `2785` -> `32737` to `32786`
- Z-axis block: internal `2832` to `2881` -> `32833` to `32882`

Each axis block uses the same layout.

#### Per-Axis Layout

| Offset In Axis Block | Address Example (X-Axis) | Name | Current Meaning |
| :--- | :--- | :--- | :--- |
| `0` | `32641` | Acc RMS | `acc_rms_g * 1000` |
| `1` | `32642` | Acc Peak | `acc_peak_g * 1000` |
| `2` | `32643` | Acc Peak-to-Peak | `acc_p2p_g * 1000` |
| `3` | `32644` | Crest Factor | `crest_factor * 1000` |
| `4` | `32645` | Vel RMS | `mm/s` scaled directly into `UINT16` |
| `5` | `32646` | Vel Peak | `mm/s` scaled directly into `UINT16` |
| `6` | `32647` | Disp Peak-to-Peak | `mm` scaled directly into `UINT16` |
| `7` | `32648` | Envelope Kurtosis | `envelope_kurtosis * 100` |
| `8` | `32649` | ISO Analysis | `1..4` ISO severity band |
| `9` | `32650` | Fault Flags | Bitmask |
| `10` - `29` | `32651` - `32670` | Peak Frequency Bins | Top 20 peak frequencies |
| `30` - `49` | `32671` - `32690` | Peak Magnitudes | Top 20 peak magnitudes |

#### Axis Address Ranges

| Axis | Internal Index Range | Modbus Address Range |
| :--- | :--- | :--- |
| X | `2640` - `2689` | `32641` - `32690` |
| Y | `2736` - `2785` | `32737` - `32786` |
| Z | `2832` - `2881` | `32833` - `32882` |

### 2.3 Timestamp / Sampling Timing Block

The vibration task also writes three 64-bit timestamps, each split across four `UINT16` words.

| Internal Index Range | Modbus Address Range | Name | Description |
| :--- | :--- | :--- | :--- |
| `200` - `203` | `30201` - `30204` | Previous Timestamp | Previous vibration processing time in microseconds |
| `204` - `207` | `30205` - `30208` | Current Timestamp | Current vibration processing time in microseconds |
| `208` - `211` | `30209` - `30212` | Delta Timestamp | Time difference in microseconds |

Word order:

- low word first
- then upper words

## 3. Current Fault Flag Bit Map

The per-axis fault field at:

- X: `30060`
- Y: `30110`
- Z: `30160`

uses the following bit definition from the current vibration analyzer:

| Bit | Mask | Meaning |
| :--- | :--- | :--- |
| `0` | `0x0001` | Unbalance |
| `1` | `0x0002` | Misalignment |
| `2` | `0x0004` | Looseness |
| `3` | `0x0008` | Bearing Fault |
| `4` | `0x0010` | Gear Mesh Fault |
| `5` | `0x0020` | Resonance |
| `6` | `0x0040` | Speed Unstable |

## 4. Bearing And Machine Data Used By The Current Analyzer

These values are currently part of the firmware calculation path:

| Item | Current Source | Notes |
| :--- | :--- | :--- |
| Shaft base frequency | `FREQ_BASE` | Currently fixed at compile time unless tach becomes valid |
| Bearing ball count | `BEARING_NUMBER_OF_ROLLING_ELEMENTS` | Compile-time default exists |
| Bearing pitch diameter | `BEARING_PITCH_DIAMETER` | Compile-time default exists |
| Bearing rolling diameter | `BEARING_ROLLING_DIAMETER` | Compile-time default exists |
| Bearing contact angle | `BEARING_CONTACT_ANGLE` | Compile-time default exists |
| Gear teeth number | `40019` (`MB_REG_GEAR_TEETH_NUMBER`) | Runtime config with compile-time default fallback |

Important note:

- Holding registers `40010` to `40017` are declared for runtime configuration.
- But the current analyzer still depends heavily on compile-time values from [vibration_product_config.h](/c:/Users/PC%20YH/STM32CubeIDE/workspace_2.1.0/Vimot_H563RITX_Professional_Cmake_Systemize/App/Product/Vibration/vibration_product_config.h).
- So today the register map is only **partly connected** to the real calculation pipeline.

## 5. Recommended Professional Expansion Map

The following addresses fit your current firmware layout cleanly and are the best place to grow next without breaking the existing axis blocks.

### 5.1 Current And Planned Holding Register Expansion

| Modbus Address | Name | Suggested Use |
| :--- | :--- | :--- |
| `40004` | Response Delay | Communication timing |
| `40005` | Machine Type Profile | `0=Generic, 1=Motor, 2=Pump, 3=Fan, 4=Compressor, 5=Gearbox` |
| `40006` | System Command | `0=Idle, 1=Restart, 2=System Reset, 4=Capture Baseline, 5=Reset Baseline, 0xDEAD=Factory Reset` |
| `40007` | Tach Enable | `0=Disable, 1=Enable` |
| `40008` | Tach Source | Timer/capture channel selection |
| `40009` | Reserved | Spare |

### 5.2 Recommended Advanced Input Register Expansion

Use the free area after `30212`.

| Internal Index | Modbus Address | Name | Suggested Source |
| :--- | :--- | :--- | :--- |
| `220` | `30221` | Current RPM | `tach_state.rpm_filtered` |
| `221` | `30222` | Tach Quality Score | `tach_state.quality * 100` |
| `222` | `30223` | Speed State | steady/run-up/coast-down |
| `223` | `30224` | Order 1X Amplitude | dominant axis `order_1` |
| `224` | `30225` | Envelope Kurtosis | dominant axis advanced metric |
| `225` | `30226` | Sideband Energy Density | dominant axis advanced metric |
| `226` | `30227` | Harmonic Consistency | dominant axis advanced metric |
| `227` | `30228` | Cepstrum Gear Index | dominant axis advanced metric |
| `228` | `30229` | Spectral Kurtosis | dominant axis advanced metric |
| `229` | `30230` | Cyclostationary Index | dominant axis advanced metric |
| `230` | `30231` | Remaining Useful Life (days) | derived from `remaining_useful_life_hours / 24` |
| `231` | `30232` | Failure Probability | `0-100%` |
| `232` | `30233` | Degradation Index | scaled health index |
| `233` | `30234` | Time To Maintenance | days |
| `234` | `30235` | Baseline Update Count | from baseline record |
| `235` | `30236` | Baseline Active Flag | `0/1` |
| `236` | `30237` | Active Machine Profile | from vibration config |
| `237` | `30238` | Aging Factor | reserved for future learning model |

## 6. Current Discrete Inputs And Coils

Current allocation:

- discrete inputs: `16`
- coils: `64`

Current status:

- memory is allocated in FreeModbus
- no stable application-level map is implemented yet

Recommended future discrete input map:

| Modbus Address | Name | Meaning |
| :--- | :--- | :--- |
| `10001` | Baseline Active | baseline has been established |
| `10002` | Speed Tracking Lock | tach/order lock valid |
| `10003` | Maintenance Warning | maintenance should be scheduled |
| `10004` | Fault Unbalance | aggregated warning |
| `10005` | Fault Misalignment | aggregated warning |
| `10006` | Fault Looseness | aggregated warning |
| `10007` | Fault Bearing | aggregated warning |
| `10008` | Fault Gear Mesh | aggregated warning |
| `10009` | Fault Resonance | aggregated warning |
| `10010` | Fault Speed Unstable | aggregated warning |

## 7. Summary

Your **current real Modbus image** is:

- `40001` to `40008` for communication/basic control fields
- `52545` to `52554` for vibration configuration fields
- `52673` to `52797` for remapping address table
- `32625` for temperature
- `32641` to `32882` for the three axis vibration result blocks
- `30201` to `30212` for processing timestamps

Your **best next expansion area** is:

- `40004` to `40009` for communication/profile/baseline control
- `30221` onward for tach, order tracking, advanced features, baseline status, and predictive maintenance

This keeps your present firmware compatible while giving a clean path for future vibration intelligence features.
