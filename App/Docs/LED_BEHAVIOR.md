# Application LED Behavior

This document describes the LED behavior owned by the PiLoT-V application firmware. The bootloader controls the LEDs before it jumps to the application; its handshake-window behavior is outside the application sequence documented here.

## Hardware Mapping

| Logical channel | Board signal | MCU pin | Active level |
| :--- | :--- | :--- | :--- |
| Green status | `LED_STAT` | `PC6` | Active low |
| Red power/fault | `LED_PWR` | `PB15` | Active low |

An active-low LED is on when its GPIO output is reset and off when its GPIO output is set. When a pattern says both channels are on, it means the red and green LEDs are driven independently at the same time; it is not a separate yellow state.

## Initialization and Runtime Ownership

The LED engine must never run against its zero-initialized context. `LED_STATE_FATAL_FAULT` has enum value zero, so servicing the context before `LED_Init()` would incorrectly interpret an uninitialized requested state as a fatal fault.

The application uses this sequence:

```text
Application reset handler
  -> HAL_Init()                     starts the HAL timebase only
  -> SystemClock_Config()
  -> MX_GPIO_Init()                 configures PC6 and PB15
  -> LED_Init()                     initializes the LED context and turns both LEDs off
  -> remaining peripheral setup
  -> persistent-storage setup
  -> osKernelInitialize()
  -> MX_FREERTOS_Init()             creates the LED, Modbus, and vibration tasks
  -> osKernelStart()
  -> LED_ServiceTask()              calls LED_Task_10ms() every 10 ms
```

The TIM1 period callback now increments the HAL tick only. It does not call the LED engine. LED servicing begins only after `LED_Init()` has completed and the FreeRTOS scheduler has started the dedicated low-priority LED task.

`LED_Task_10ms()` calculates elapsed time using `HAL_GetTick()`. The engine therefore advances by actual elapsed milliseconds even if the LED task experiences small scheduling delays.

Relevant implementation files:

- `Core/Src/main.c`: GPIO/LED initialization order and HAL timebase callback.
- `Core/Src/app_freertos.c`: dedicated `LED_ServiceTask` creation and 10 ms service loop.
- `Core/Src/app_led.c`: LED state engine, priorities, special displays, and GPIO writes.
- `Core/Inc/app_led_config.h`: pin mapping and timing constants.
- `App/Application/Tasks/task_modbus.c`: startup display request and Modbus-driven status changes.

## Application Startup Display

After the scheduler starts, the Modbus task loads and validates the communication configuration. A valid configuration is displayed once in this order:

1. Slave ID.
2. Both red and green LEDs on for the 375 ms stage separator.
3. Baud-rate code on the green LED.
4. Both red and green LEDs on for the 375 ms stage separator.
5. Parity display value on the green LED.
6. Both red and green LEDs on for the 375 ms stage separator.
7. Both LEDs off for the 500 ms end separator.
8. The normal base-state pattern begins.

Each nonzero digit is shown as 250 ms on and 250 ms off between pulses. The completed digit is followed by 300 ms off. A displayed zero is represented by one continuous 200 ms pulse followed by 300 ms off.

While the startup display is active, its startup lock prevents identify requests and normal event overlays from replacing the configuration sequence.

### Slave ID Encoding

The slave ID is displayed as hexadecimal nibbles:

- A nonzero high nibble is displayed on the red LED.
- The low nibble is displayed on the green LED.
- When the high nibble is zero, it is skipped.
- When a high nibble is present, the completed red digit and the green low digit are separated by an additional 300 ms off interval.

For slave ID `1` (`0x01`), the display is one green pulse and no red digit pulse.

### Baud-Rate Encoding

| Baud display code | Configured rate |
| :--- | :--- |
| `0` | Autobaud |
| `1` | 9600 bps |
| `2` | 19200 bps |
| `3` | 38400 bps |
| `4` | 115200 bps |

For 115200 bps, the green LED flashes four times.

### Parity Encoding

The stored Modbus parity code includes zero as a valid configuration value. The LED display therefore uses `stored parity code + 1` so that every setting has a visible, nonzero flash count.

| Stored parity code | Serial format | LED display value |
| :--- | :--- | :--- |
| `0` | Even parity, one stop bit | 1 green flash |
| `1` | Odd parity, one stop bit | 2 green flashes |
| `2` | No parity, two stop bits | 3 green flashes |
| `3` | No parity, one stop bit (`8N1`) | 4 green flashes |

The four flashes for stored code `3` do not represent a parity mode numbered `4`; four is only the visible display count.

### Example: Slave 1, 115200 bps, 8N1

```text
Green: 1 pulse                         slave ID 1
Red + green: on for 375 ms             stage separator
Green: 4 pulses                        115200 baud, code 4
Red + green: on for 375 ms             stage separator
Green: 4 pulses                        stored parity code 3 + 1
Red + green: on for 375 ms             stage separator
Red + green: off for 500 ms            sequence end
Green: 500 ms on, 1500 ms off          no Modbus polling yet
```

## Base-State Patterns

All repeating status patterns use a 2000 ms status frame unless otherwise noted.

| State | Green LED | Red LED |
| :--- | :--- | :--- |
| Fatal fault | Off | Solid on |
| Device fault | Off | 150 ms on, 150 ms off, repeating |
| Invalid configuration | 200 ms on, 200 ms off, 200 ms on, then off | Same as green |
| Communication warning | 150 ms heartbeat at frame start | 200 ms pulses at 700 ms and 1100 ms |
| General warning | 150 ms heartbeat at frame start | On from 750 ms through 1250 ms |
| Network lost | 200 ms on, 200 ms off, 200 ms on, then off | Off |
| No polling yet | 500 ms on, 1500 ms off | Off |
| Normal polling active | 150 ms on, 1850 ms off | Off |

`LED_STATE_SLAVE_ID_DISPLAY` and `LED_STATE_IDENTIFY` are not ordinary base patterns. Their visible behavior is produced by the special display engine through `LED_RequestSlaveIdDisplay()` and `LED_RequestIdentify()`.

## State Priority

After the startup lock is released, the engine resolves simultaneous conditions in this order, highest priority first:

1. Fatal fault.
2. Device fault.
3. Invalid configuration.
4. Communication warning.
5. General warning.
6. Requested base state.

A fatal fault produces red solid and green off after the LED service task applies the state. During the locked startup display, the configuration sequence owns the LEDs and fault flags are rendered only after the startup lock ends.

## Activity Overlays

After the startup display finishes:

- A valid Modbus transaction requests a 150 ms green activity pulse, rate-limited to once every 250 ms.
- A communication error requests a 200 ms red pulse, rate-limited to once every 500 ms.
- A red error pulse overrides a simultaneous green activity pulse.

## Diagnostic Expectations

A permanently solid green LED is not an intentional steady application state. The longest normal continuous green interval is 500 ms in the no-polling pattern. If green remains solid:

1. Confirm `LED_Init()` is reached after `MX_GPIO_Init()`.
2. Confirm `osKernelStart()` succeeds.
3. Confirm `LED_ServiceTask` is created and scheduled.
4. Confirm `LED_Task_10ms()` continues to execute.
5. Confirm the physical LED colors match the `PC6` green and `PB15` red mapping.

Pre-scheduler failures are a separate limitation: the LED task is not running yet, and the current `Error_Handler()` disables interrupts and loops without generating a managed LED pattern. If a deterministic pre-scheduler fault indication is required, it must be implemented as a direct GPIO fault indication or another non-RTOS error path.
