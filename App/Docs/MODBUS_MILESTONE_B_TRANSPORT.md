# Modbus migration Milestone B — Pyxis transport isolation

Milestone B records the board-specific transport contract without changing the
known-good receive path.  The custom engine still uses:

* USART3 with RS485 automatic DE;
* GPDMA1 channel 2 for USART3 RX;
* a 256-byte HAL ReceiveToIdle DMA buffer;
* a 512-byte ISR-to-task receive ring;
* the `customMbRx` worker to call `ModbusEnginePort_OnRxByte()` outside ISR
  context.

These values are centralized in
`App/Middleware/ModbusEngine/Inc/modbus_engine_hw_config.h`.  The protocol
core and register map remain independent of the STM32 peripheral selection.

Build/rollback controls remain:

```text
MODBUS_ENGINE_ACTIVE=1              # custom engine
MODBUS_ENGINE_ACTIVE=0              # FreeModbus fallback
MODBUS_PYXIS_DMA_ABSOLUTE=0         # current qualified transport
```

Verification for this milestone is a clean CubeIDE build followed by a
firmware smoke test: the LED/TIM1 counters must continue increasing, and
`g_custom_uart_init_stage` must reach 4 with `g_custom_uart_init_ok=1` and
`g_custom_uart_enable_ok=1`.  Do not enable the absolute-position transport
until Milestones C/D have been implemented and tested.
