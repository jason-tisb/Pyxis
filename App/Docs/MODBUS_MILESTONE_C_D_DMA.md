# Modbus migration Milestones C/D — circular DMA position tracking

The opt-in transport uses a 512-byte circular RX buffer when
`MODBUS_PYXIS_DMA_ABSOLUTE=1`. The established baseline remains 256 bytes
when the option is `0`.

On each USART3 IDLE callback the port normalizes the HAL producer count,
computes the newly produced range across a circular-buffer wrap, forwards only
that bounded range to the receive ring, and increments an absolute producer
counter for diagnostics.

Diagnostic symbols:

```text
g_uart3_dma_producer_abs
g_uart3_dma_wrap_count
g_uart3_dma_position_errors
```

Build this path separately with:

```text
MODBUS_ENGINE_ACTIVE=1
MODBUS_PYXIS_DMA_ABSOLUTE=1
```

Keep the normal commissioning build at `MODBUS_PYXIS_DMA_ABSOLUTE=0` until
the 512-byte path has been clean-built and tested on hardware.
