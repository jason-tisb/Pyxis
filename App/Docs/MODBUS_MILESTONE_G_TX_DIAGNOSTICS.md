# Modbus migration Milestone G — TX/RS485 diagnostics

The custom TX path remains blocking and waits for UART transmission complete,
which is safe for RS485 automatic DE. It now records:

```text
g_modbus_tx_attempts
g_modbus_tx_errors
g_modbus_tx_busy_rejections
g_modbus_tx_last_start_tick
g_modbus_tx_last_end_tick
g_modbus_tx_last_duration_ms
g_modbus_tx_last_length
g_modbus_tx_last_hal_status
```

These counters establish whether a short master timeout is caused by actual
transmit latency or by the master/adapter not receiving the bus response.
Asynchronous TX DMA/interrupt operation is intentionally deferred until this
baseline is measured.
