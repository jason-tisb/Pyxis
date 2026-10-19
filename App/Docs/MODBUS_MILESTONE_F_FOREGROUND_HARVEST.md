# Modbus migration Milestone F — foreground DMA harvesting

The USART3 callback now performs only lightweight bookkeeping:

* reads and validates the HAL DMA producer position;
* advances the absolute producer counter; and
* signals the `customMbRx` worker.

The worker task performs the DMA-to-ring copy and then feeds the custom
protocol engine. If the producer has lapped the worker by more than one DMA
buffer, the newest complete DMA window is retained and
`g_uart3_dma_overruns` is incremented.

This keeps potentially long copy loops out of interrupt context and preserves
the FreeModbus fallback. A clean CubeIDE build is required before flashing.
