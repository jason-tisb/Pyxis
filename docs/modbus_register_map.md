# Pyxis canonical Modbus register map

Generated from `App/Product/RegisterMap/Modbus/modbus_map.h`.
Addresses are zero-based protocol offsets; Holding and Input spaces
are independent. Human addresses are 40001+offset and 30001+offset.

| ID | Name | Space | Offset | Count | Min | Max | Default | Access | Persistence | Apply |
|---|---|---|---:|---:|---:|---:|---:|---|---|---|
| `PYXIS_REG_SLAVE_ID` | `slave_id` | Holding | 0 | 1 | 1 | 247 | 3 | Read Write | Persistent | `ON_BOOT` |
| `PYXIS_REG_BAUD_SELECTOR` | `baudrate` | Holding | 1 | 1 | 0 | 4 | 1 | Read Write | Persistent | `ON_BOOT` |
| `PYXIS_REG_FRAME_SELECTOR` | `parity` | Holding | 2 | 1 | 0 | 3 | 3 | Read Write | Persistent | `ON_BOOT` |
| `PYXIS_REG_RESPONSE_DELAY` | `response_delay` | Holding | 3 | 1 | 0 | 65535 | 0 | Read Write | Volatile | `IMMEDIATE` |
| `PYXIS_REG_MACHINE_PROFILE` | `machine_profile` | Holding | 4 | 1 | 0 | 5 | 0 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_TACH_ENABLE` | `tach_enable` | Holding | 6 | 1 | 0 | 1 | 0 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_TACH_SOURCE` | `tach_source` | Holding | 7 | 1 | 0 | 65535 | 0 | Read Write | Volatile | `IMMEDIATE` |
| `PYXIS_REG_SYSTEM_COMMAND` | `system_command` | Holding | 5 | 1 | 0 | 0xDEAD | 0 | Read Write | Volatile | `IMMEDIATE` |
| `PYXIS_REG_GEAR_RATIO` | `gear_ratio` | Holding | 12544 | 1 | 1 | 5000 | 100 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_BASE_FREQ_HZ` | `base_frequency` | Holding | 12545 | 1 | 1 | 65535 | 5000 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_BEARING_BALL_COUNT` | `bearing_ball_count` | Holding | 12546 | 1 | 3 | 32 | 8 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_BEARING_BALL_SIZE` | `bearing_ball_size` | Holding | 12547 | 1 | 1 | 5000 | 1200 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_BEARING_DIAMETER` | `bearing_diameter` | Holding | 12548 | 1 | 1 | 7200 | 7200 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_BEARING_ALPHA` | `bearing_alpha` | Holding | 12549 | 1 | 0 | 900 | 0 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_FILTER_HIGH_PASS` | `filter_high_pass` | Holding | 12550 | 1 | 1 | 65535 | 10 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_FILTER_LOW_PASS` | `filter_low_pass` | Holding | 12551 | 1 | 1 | 65535 | 13333 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_REFRESH` | `refresh` | Holding | 12552 | 1 | 0 | 1 | 0 | Read Write | Volatile | `IMMEDIATE` |
| `PYXIS_REG_GEAR_TEETH` | `gear_teeth` | Holding | 12553 | 1 | 1 | 1024 | 8 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_REMAPPING` | `remapping` | Holding | 12672 | 125 | 0 | 65535 | 0 | Read Write | Persistent | `IMMEDIATE` |
| `PYXIS_REG_IDENTITY` | `identity` | Input | 0 | 6 | 0 | 65535 | 0 | Read Only | Volatile | `IMMEDIATE` |
| `PYXIS_REG_TEMPERATURE` | `temperature` | Input | 2624 | 2 | 0 | 65535 | 0 | Read Only | Volatile | `IMMEDIATE` |
| `PYXIS_REG_X_VIBRATION` | `x_vibration` | Input | 2640 | 50 | 0 | 65535 | 0 | Read Only | Volatile | `IMMEDIATE` |
| `PYXIS_REG_Y_VIBRATION` | `y_vibration` | Input | 2736 | 50 | 0 | 65535 | 0 | Read Only | Volatile | `IMMEDIATE` |
| `PYXIS_REG_Z_VIBRATION` | `z_vibration` | Input | 2832 | 50 | 0 | 65535 | 0 | Read Only | Volatile | `IMMEDIATE` |
| `PYXIS_REG_ANALYTICS` | `analytics` | Input | 2928 | 30 | 0 | 65535 | 0 | Read Only | Volatile | `IMMEDIATE` |

## Communication lifecycle

Communication rows use `APPLY_ON_BOOT`. Their Holding-register values change immediately after an accepted write, while the active UART and slave address remain unchanged until the next boot.

## One-shot system commands

Holding offset 5 (human address 40006) is the one-shot System Command word, not
configuration. It is accepted only through FC06, is cleared back to zero after
acceptance, and is executed only after the response has completed UART
transmission and RS-485 DE has been released. The command values are:

| Value | Action |
|---:|---|
| `0x0000` | Idle |
| `0x0001` | Restart MCU; preserve configuration and baseline |
| `0x0002` | System reset; clear vibration baseline, then restart MCU |
| `0x0004` | Capture/save vibration baseline |
| `0x0005` | Reset/clear vibration baseline |
| `0xDEAD` | Factory reset canonical configuration and baseline, persist, then restart |

Factory reset writes a verified canonical-default H1 persistence generation and
blocks legacy migration before rebooting. Unknown values, FC16 transactions
containing this address, and broadcast destructive commands are rejected without
side effects.
