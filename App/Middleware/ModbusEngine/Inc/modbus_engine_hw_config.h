#ifndef MODBUS_ENGINE_HW_CONFIG_H
#define MODBUS_ENGINE_HW_CONFIG_H

/* Pyxis-V hardware contract for the custom Modbus transport.
 * Keep board-specific UART/DMA facts here; the protocol core must not need
 * to know which STM32 peripheral carries the RTU bytes. */
#define MODBUS_PYXIS_UART_INSTANCE       USART3
#define MODBUS_PYXIS_DMA_INSTANCE        GPDMA1_Channel2
#define MODBUS_PYXIS_DE_GPIO_PORT        GPIOB
#define MODBUS_PYXIS_DE_GPIO_PIN         GPIO_PIN_14
#if MODBUS_PYXIS_DMA_ABSOLUTE
#define MODBUS_PYXIS_RX_DMA_SIZE         512U
#else
#define MODBUS_PYXIS_RX_DMA_SIZE         256U
#endif
#define MODBUS_PYXIS_RX_RING_SIZE        512U

/* The current transport uses HAL ReceiveToIdle DMA and a task-side ring
 * worker.  The absolute-position/RTO transport will be selected separately
 * once its producer-position logic has been qualified. */
#define MODBUS_PYXIS_RX_EVENT_MODE_IDLE  1U
#define MODBUS_PYXIS_RX_WORKER_ENABLED   1U

#if (MODBUS_PYXIS_RX_DMA_SIZE == 0U) || \
    ((MODBUS_PYXIS_RX_DMA_SIZE & (MODBUS_PYXIS_RX_DMA_SIZE - 1U)) != 0U)
#error "MODBUS_PYXIS_RX_DMA_SIZE must be a non-zero power of two"
#endif

#if (MODBUS_PYXIS_RX_RING_SIZE == 0U) || \
    ((MODBUS_PYXIS_RX_RING_SIZE & (MODBUS_PYXIS_RX_RING_SIZE - 1U)) != 0U)
#error "MODBUS_PYXIS_RX_RING_SIZE must be a non-zero power of two"
#endif

#endif
