# Modbus migration Milestone E — RTU silence timing

The custom port no longer uses a fixed 5 ms frame delay. After UART
initialization it calculates the Modbus RTU 3.5-character silence interval
from baud rate, data bits, parity, and stop bits. The result is rounded up to
milliseconds with a 1 ms minimum because the current timer source is
`HAL_GetTick()`.

Examples:

```text
115200 8N1  -> 1 ms minimum
9600   8N1  -> approximately 4 ms
```

The receive architecture and DMA selector are unchanged. FreeModbus fallback
remains available with `MODBUS_ENGINE_ACTIVE=0`.
