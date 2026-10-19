# Modbus Autobaud Milestone A — TIM2 Capture Backend

Status: implemented; hardware verification pending (Milestone G).

## Files added or modified

- `App/Middleware/ModbusEngine/Inc/modbus_autobaud_capture.h`
- `App/Middleware/ModbusEngine/Src/modbus_autobaud_capture.c`
- `Core/Src/stm32h5xx_hal_msp.c`
- `Core/Src/stm32h5xx_it.c`
- `CMakeLists.txt`
- CubeIDE generated `Debug/App/Middleware/ModbusEngine/Src/subdir.mk`

## Responsibilities

`modbus_autobaud_capture.c` temporarily suspends the custom USART3/DMA
transport, remuxes PC4 from USART3_RX (AF7) to TIM2_CH4 (AF1), captures both
edges, stores up to 64 unsigned timestamp intervals, and restores USART3/DMA
on timeout, completion, or initialization failure. The capture interrupt only
records timestamps; it does not score baud rates or process Modbus frames.

TIM2 is configured as a free-running 32-bit up-counter with prescaler 2. The
current APB1 configuration is DIV1 at 32 MHz, so the expected capture clock is
approximately 10.667 MHz. The implementation derives the PCLK1 value at
runtime and stores the divided frequency for Milestone B.

## Concurrency and cleanup

The public start/service/stop functions are task-context APIs. Service yields
with `osDelay(1)` and completes after 64 intervals, after eight intervals plus
5 ms of silence, or after the caller-provided timeout. All failure paths stop
TIM2, restore PC4 to USART3_RX, and restart the existing RX/DMA transport.

## Verification

- The new capture source compiles successfully with the existing Release
  compile command.
- `stm32h5xx_hal_msp.c` and `stm32h5xx_it.c` compile successfully with the
  existing Release compile commands.
- CMake and CubeIDE source lists include the new capture source.
- No baud scoring, framing detection, UART switching policy, persistence, or
  startup integration was added in this milestone.

Physical TIM2 edge capture, PC4 electrical connectivity, and RS485 receiver
enable behavior remain pending for Milestone G.
