# Modbus migration Milestone H — hardware RTU receiver timeout

An opt-in USART3 receiver-timeout path is implemented with:

```text
MODBUS_ENGINE_ACTIVE=1
MODBUS_PYXIS_DMA_ABSOLUTE=1
MODBUS_PYXIS_HW_RTO=1
```

The timeout is configured for four character times, providing a hardware
T3.5-style boundary. The interrupt only records the event and wakes the
existing worker; frame harvesting and parser access remain task-side.

The default remains `MODBUS_PYXIS_HW_RTO=0`, preserving the IDLE/worker
fallback. Monitor `g_uart3_rto_count` when testing the opt-in build.
