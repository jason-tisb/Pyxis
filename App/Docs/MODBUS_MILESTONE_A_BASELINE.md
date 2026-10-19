# Modbus Migration Milestone A — Baseline

This milestone freezes the currently verified Pyxis custom-engine transport
while retaining FreeModbus as a compile-time fallback.

## Build switches

```text
MODBUS_ENGINE_ACTIVE=1
MODBUS_ENGINE_FORCE_FIXED_COMM=1
MODBUS_PYXIS_DMA_ABSOLUTE=0
```

With these values, the custom core is authoritative and the current
ring-buffer/worker-task UART transport is used. The absolute-position DMA/RTO
transport is deliberately disabled until its own milestone is implemented.

The fallback build remains available with:

```text
MODBUS_ENGINE_ACTIVE=0
```

## Fixed commissioning settings

```text
Slave ID: 1
Baud:     115200
Format:   8N1
```

## Baseline verification

- Clean build completes with zero errors.
- Custom build contains `MODBUS_ENGINE_ACTIVE=1`.
- `g_custom_uart_init_stage` reaches `4`.
- `g_custom_uart_init_ok` and `g_custom_uart_enable_ok` equal `1`.
- LED task continues to run.
- FreeModbus fallback still compiles with `MODBUS_ENGINE_ACTIVE=0`.

