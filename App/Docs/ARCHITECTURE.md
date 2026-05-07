# App Architecture

This project uses a layered `App` structure so board-specific code, reusable feature logic, product bindings, and third-party middleware stay separated.

The current codebase is centered around two runtime features:

- Modbus configuration, register access, and communication state handling.
- Vibration acquisition, signal analysis, health indicators, and persistent machine configuration.

## Current Repository Shape

```text
.
|-- App/
|   |-- Application/
|   |   |-- Modules/
|   |   |   |-- Modbus/
|   |   |   `-- Vibration/
|   |   |-- Ports/
|   |   |-- Services/
|   |   |   `-- Vibration/
|   |   `-- Tasks/
|   |-- BSP/
|   |   |-- Sensors/
|   |   |   `-- IIS3DWB/
|   |   `-- Storage/
|   |-- Docs/
|   |-- Libraries/
|   |   `-- DSP/
|   |       |-- BasicMathFunctions/
|   |       |-- CommonTables/
|   |       |-- ComplexMathFunctions/
|   |       |-- StatisticsFunctions/
|   |       `-- TransformFunctions/
|   |-- Middleware/
|   |   |-- EEPROM_Emul/
|   |   |   |-- Core/
|   |   |   `-- Porting/STM32H5/
|   |   `-- FreeModbus/
|   |       |-- modbus/
|   |       `-- port/
|   `-- Product/
|       |-- Configurations/
|       |-- RegisterMap/
|       |   `-- Modbus/
|       `-- Validation/
|-- Core/
|   |-- Inc/
|   |-- Src/
|   `-- Startup/
|-- Drivers/
|   |-- CMSIS/
|   |-- Flash_Storage/
|   |-- IIS3DWB/
|   `-- STM32H5xx_HAL_Driver/
`-- cmake/
    |-- stm32cubemx/
    `-- toolchain files
```

## Layer Responsibilities

### `App/Application`

Owns feature logic that should stay stable even if the MCU wiring, storage backend, or product variant changes.

#### `App/Application/Tasks`

RTOS entry points and orchestration loops.

- `task_modbus.c`
  - Initializes the Modbus module and vibration module.
  - Populates the shared Modbus holding register image.
  - Starts and polls FreeModbus.
  - Applies requested Modbus config updates through adapters and modules.
  - Applies requested vibration config updates and baseline commands.
  - Handles autobaud and reset-after-Modbus-config-change behavior.
- `task_vibration.c`
  - Owns the vibration runtime loop.
  - Creates the sensor bus binding and IIS3DWB sensor instance.
  - Fetches FIFO samples, runs analysis, updates live Modbus telemetry registers, and refreshes derived bearing frequencies when settings change.

Keep this layer focused on sequencing and coordination, not deep validation or low-level peripheral details.

#### `App/Application/Modules`

Feature-owned state, persistence-facing config models, and module-level validation.

- `Modules/Modbus`
  - Stores `ModbusConfig_t` and `ModbusStats_t`.
  - Loads/saves Modbus config and communication statistics through storage.
  - Exposes update and statistics APIs to tasks.
- `Modules/Vibration`
  - Owns `VibConfig_t`, health record data, baseline data, and tach state.
  - Performs final vibration config validation in `VibrationModule_ValidateConfig()`.
  - Updates persistent baseline, tach state, record, and current feature config.

This is the right layer for feature truth and final config acceptance.

#### `App/Application/Ports`

Small interfaces that decouple core logic from hardware implementations.

- `sensor_bus_port.h`
  - Defines the generic register read/write/delay vtable used by sensor drivers.
  - Now also provides optional async transfer hooks so a concrete BSP implementation can grow into interrupt or DMA-based transfers without changing upper-layer feature ownership.
  - Lets application code talk to a sensor transport without knowing whether the bus is SPI, I2C, or something else.
- `uart_port.h`
  - Generic byte-stream UART transport abstraction for future serial devices or protocols.
- `can_port.h`
  - Frame-based CAN transport abstraction for future CAN-connected modules.
- `lora_port.h`
  - Packet-oriented LoRa transport abstraction for future radio links.
- `network_client_port.h`
  - Generic network session abstraction for future IP-based clients.
- `mqtt_client_port.h`
  - MQTT-specific client abstraction for future publish/subscribe integrations.
- `rest_client_port.h`
  - REST request/response abstraction for future HTTP-style integrations.

The current project actively uses `sensor_bus_port.h`. Its synchronous path is the active one today. The async hooks are extension points for future BSP work such as IIS3DWB DMA transfers. The other port headers are also extension points only and are intentionally not wired into the current runtime path yet.

#### `App/Application/Services`

Reusable processing logic below the task level and above hardware access.

- `Services/Vibration/vibration_algorithm.*`
  - FFT-based analysis and derived condition indicators.
  - Tach tracking, fault flags, ISO severity interpretation, envelope and spectral metrics, and legacy register result packing.
- `Services/Vibration/bearing_freq_calculation.*`
  - Bearing frequency generation from machine geometry and shaft speed assumptions.

This layer contains the math and analysis behavior used by the vibration feature.

### `App/BSP`

Board support implementations for concrete hardware and persistent storage.

#### `App/BSP/Sensors/IIS3DWB`

Concrete wrapper around the ST IIS3DWB sensor driver.

- Handles device bring-up, reset, FIFO setup, temperature readout, and sample extraction.
- Bridges the generic `SensorBusPort_t` interface to the ST `stmdev_ctx_t` callbacks.
- Converts raw FIFO samples into acceleration values in `g`.

#### `App/BSP/Storage`

System storage abstraction built on EEPROM emulation.

- `storage_kv.*`
  - Key-value persistence API for feature config and runtime records.
  - Splits payloads into 96-bit chunks for EEPROM emulation storage.
  - Adds RTOS mutex/semaphore protection when enabled.
- `sys_storage_partition.*`
  - Defines which keys exist and how much virtual EEPROM space each one owns.

When storage backend details change, this is the main place to touch.

### `App/Product`

Product- and model-specific configuration, register meaning, and validation policy.

#### `App/Product/Configurations`

Product constants and compile-time bindings.

- `vibration_product_config.h`
  - Declares the active product variant (`VB001`).
  - Defines sensor bus binding, FFT size, sample rate, FIFO watermark, default bearing geometry, base frequency, and related constants used by vibration analysis and sensor bring-up.

This folder answers questions like "what machine assumptions does this product ship with?" and "which SPI handle / chip-select does this product use?"

#### `App/Product/RegisterMap/Modbus`

Product-facing Modbus register ownership.

- `modbus_register_map.h`
  - Canonical register address layout for communication config, vibration config, runtime telemetry, timing data, and analytics.
- `modbus_app_adapter.*`
  - Converts between Modbus register values and `ModbusConfig_t`.
  - Validates common Modbus config fields such as slave ID, baud code, and parity code.
- `vibration_modbus_adapter.*`
  - Converts between raw holding register values and `VibConfig_t`.
  - Encodes/decodes machine profile, gear ratio, motor speed, bearing geometry, filters, and baseline commands.
  - Provides product-side raw register validation helpers for vibration-related Modbus writes.

This layer should own register meaning and register-to-feature translation, not the generic FreeModbus port.

#### `App/Product/Validation`

Cross-feature or product-level validation entry points.

- `system_validator.*`
  - Centralizes acceptance checks for Modbus holding-register writes.
  - Routes each register to the correct product-side validator instead of hardcoding product rules inside `user_mb_app.c`.

This is the right place for "system accepts or rejects this product-specific write" decisions.

### `App/Middleware`

Third-party stacks and subsystem-style components that the app builds on.

#### `App/Middleware/FreeModbus`

Integrated Modbus protocol stack and RTOS porting layer.

- `modbus/`
  - Core stack implementation for Modbus functions, RTU, ASCII, TCP, and protocol helpers.
- `port/`
  - Project-specific hooks and shared register buffers for the slave/master callbacks.
  - `cmsis_rtos2/` contains RTOS bindings for events, timers, serial, and critical sections.
  - `user_mb_app.*` exposes the register image used by FreeModbus callbacks and should stay transport-oriented, not product-rule-oriented.

#### `App/Middleware/EEPROM_Emul`

EEPROM emulation subsystem plus MCU-specific flash porting.

- `Core/`
  - Generic EEPROM emulation engine and types.
- `Porting/STM32H5/`
  - STM32H5 flash access adapter used by the storage layer.

### `App/Libraries`

Vendored reusable code used directly by the application.

#### `App/Libraries/DSP`

Local DSP source tree used by the vibration analysis path.

- `BasicMathFunctions/`
- `CommonTables/`
- `ComplexMathFunctions/`
- `StatisticsFunctions/`
- `TransformFunctions/`

These folders provide the CMSIS-DSP style primitives and tables consumed by vibration analysis and FFT workflows.

### `App/Docs`

Project documentation for architecture and register mapping.

- `ARCHITECTURE.md`
  - High-level structure and folder ownership.
- `MODBUS_REGISTER_MAP.md`
  - Register-level Modbus behavior and documentation.

## Supporting Top-Level Folders

### `Core`

STM32Cube-generated startup and platform glue.

- `Core/Inc`
  - Global project headers and RTOS config headers.
- `Core/Src`
  - `main.c`, FreeRTOS startup, ISR glue, HAL MSP, timebase, and system startup code.
- `Core/Startup`
  - Startup assembly for the target MCU.

### `Drivers`

Vendor and hardware drivers.

- `STM32H5xx_HAL_Driver`
  - STM32 HAL and LL sources.
- `CMSIS`
  - CMSIS core/device headers.
- `IIS3DWB`
  - ST sensor register driver used by the BSP wrapper.
- `Flash_Storage`
  - Non-volatile data support used by the EEPROM emulation/storage path.

### `cmake`

Build-system support files.

- Toolchain files for GCC and clang-based embedded builds.
- `stm32cubemx/`
  - STM32CubeMX-generated CMake integration.

## Practical Change Rules

- If SPI pins, chip-select wiring, or peripheral initialization changes, start in `Core` or `App/BSP`.
- If sensor bus access changes but the analysis should not, update `App/BSP` and keep `App/Application` stable.
- If a sensor transport moves from polling to interrupt or DMA mode, prefer extending the BSP transport implementation first and keep the task/service/module layers unchanged.
- If machine defaults or product assumptions change, update `App/Product/Configurations`.
- If Modbus register meaning changes, update `App/Product/RegisterMap/Modbus`.
- If product-specific acceptance rules change, update `App/Product/Validation` or the relevant product adapter validator.
- If feature state, persistence schema, or final config validation changes, update `App/Application/Modules`.
- If math or signal processing changes, update `App/Application/Services/Vibration`.

## Recommended Ownership Boundaries

- `user_mb_app.c` and the FreeModbus port should answer transport questions such as address range and register image access.
- Product folders should answer semantic questions such as what a register means and whether a value is valid for this product.
- Application modules should own final feature config validity and persistent state transitions.

That split keeps the Modbus stack reusable, product rules localized, and feature logic easier to evolve across variants.
