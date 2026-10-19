# Modbus Milestone G: interrupt-driven transmit

This milestone adds an opt-in interrupt-driven response path for the custom
Modbus engine while retaining the blocking transmitter as the fallback.

## Build selection

`MODBUS_PYXIS_TX_IT=0` (default) keeps the existing blocking
`HAL_UART_Transmit()` path.

`MODBUS_PYXIS_TX_IT=1` uses `HAL_UART_Transmit_IT()`, waits for the HAL TX
complete callback, and times out after 1 second. The USART3 ISR routes both
TX-empty and transmission-complete interrupts to `HAL_UART_IRQHandler()` in
this mode. FreeModbus TX handling remains selected when
`MODBUS_ENGINE_ACTIVE=0`.

Example custom-engine build:

```text
cmake -S . -B build/custom -G Ninja `
  -DMODBUS_ENGINE_ACTIVE=1 `
  -DMODBUS_PYXIS_TX_IT=1 `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/custom --parallel 6
```

Keep `MODBUS_PYXIS_TX_IT=0` for the known-good blocking baseline until the
interrupt-driven image has been tested on the target. Inspect
`g_modbus_tx_last_hal_status`, `g_modbus_tx_last_duration_ms`,
`g_modbus_tx_errors`, and `g_modbus_tx_busy_rejections` during testing.
