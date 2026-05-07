#include "sensor_bus_port.h"

#include <string.h>

enum {
    SENSOR_BUS_SPI_MAX_CONTEXTS = 4
};

static SensorBusSpiContext_t *g_spi_contexts[SENSOR_BUS_SPI_MAX_CONTEXTS];

static SensorBusSpiContext_t *sensor_bus_spi_find_context(SPI_HandleTypeDef *hspi)
{
    uint32_t i;

    for (i = 0U; i < SENSOR_BUS_SPI_MAX_CONTEXTS; ++i) {
        if (g_spi_contexts[i] != NULL && g_spi_contexts[i]->spi == hspi) {
            return g_spi_contexts[i];
        }
    }

    return NULL;
}

static void sensor_bus_spi_register_context(SensorBusSpiContext_t *spi_ctx)
{
    uint32_t i;

    for (i = 0U; i < SENSOR_BUS_SPI_MAX_CONTEXTS; ++i) {
        if (g_spi_contexts[i] == spi_ctx) {
            return;
        }
        if (g_spi_contexts[i] == NULL) {
            g_spi_contexts[i] = spi_ctx;
            return;
        }
    }
}

static bool sensor_bus_spi_dma_ready(const SensorBusSpiContext_t *spi_ctx, uint16_t transfer_len)
{
    return (spi_ctx != NULL) &&
           (spi_ctx->dma != NULL) &&
           (spi_ctx->dma->done_sem != NULL) &&
           (spi_ctx->dma->bus_lock != NULL) &&
           (spi_ctx->dma->tx_buf != NULL) &&
           (spi_ctx->dma->rx_buf != NULL) &&
           (spi_ctx->dma->buffer_size >= transfer_len);
}

static void sensor_bus_spi_complete(SensorBusSpiContext_t *spi_ctx, SensorBusTransferState_t state)
{
    if (spi_ctx == NULL) {
        return;
    }

    spi_ctx->transfer_state = state;
    if (spi_ctx->transfer_owns_cs) {
        HAL_GPIO_WritePin(spi_ctx->cs_port, spi_ctx->cs_pin, GPIO_PIN_SET);
        spi_ctx->transfer_owns_cs = false;
    }

    if (spi_ctx->dma != NULL && spi_ctx->dma->done_sem != NULL) {
        (void)osSemaphoreRelease(spi_ctx->dma->done_sem);
    }
}

static int32_t sensor_bus_spi_wait_dma(SensorBusSpiContext_t *spi_ctx)
{
    uint32_t timeout_ms;

    timeout_ms = (spi_ctx->dma->timeout_ms == 0U) ? 1000U : spi_ctx->dma->timeout_ms;
    if (osSemaphoreAcquire(spi_ctx->dma->done_sem, timeout_ms) != osOK ||
        spi_ctx->transfer_state != SENSOR_BUS_XFER_DONE) {
        (void)HAL_SPI_Abort(spi_ctx->spi);
        sensor_bus_spi_complete(spi_ctx, SENSOR_BUS_XFER_ERROR);
        return -1;
    }

    return 0;
}

static int32_t sensor_bus_spi_write_reg(void *user_ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    SensorBusSpiContext_t *spi_ctx;
    HAL_StatusTypeDef st;
    uint16_t transfer_len;

    spi_ctx = (SensorBusSpiContext_t *)user_ctx;
    if (spi_ctx == NULL || spi_ctx->spi == NULL) {
        return -1;
    }

    transfer_len = (uint16_t)(len + 1U);
    if (len > 1U) {
        reg |= 0x40U;
    }

    if (sensor_bus_spi_dma_ready(spi_ctx, transfer_len)) {
        if (osMutexAcquire(spi_ctx->dma->bus_lock, osWaitForever) != osOK) {
            return -1;
        }

        spi_ctx->dma->tx_buf[0] = reg;
        if (len > 0U) {
            memcpy(&spi_ctx->dma->tx_buf[1], buf, len);
        }

        while (osSemaphoreAcquire(spi_ctx->dma->done_sem, 0U) == osOK) {
        }

        spi_ctx->transfer_state = SENSOR_BUS_XFER_BUSY;
        spi_ctx->transfer_owns_cs = true;
        HAL_GPIO_WritePin(spi_ctx->cs_port, spi_ctx->cs_pin, GPIO_PIN_RESET);

        if (HAL_SPI_Transmit_DMA(spi_ctx->spi, spi_ctx->dma->tx_buf, transfer_len) != HAL_OK) {
            sensor_bus_spi_complete(spi_ctx, SENSOR_BUS_XFER_ERROR);
            (void)osMutexRelease(spi_ctx->dma->bus_lock);
            return -1;
        }

        if (sensor_bus_spi_wait_dma(spi_ctx) != 0) {
            (void)osMutexRelease(spi_ctx->dma->bus_lock);
            return -1;
        }

        (void)osMutexRelease(spi_ctx->dma->bus_lock);
        return 0;
    }

    HAL_GPIO_WritePin(spi_ctx->cs_port, spi_ctx->cs_pin, GPIO_PIN_RESET);
    st = HAL_SPI_Transmit(spi_ctx->spi, &reg, 1U, 1000U);
    if (st == HAL_OK && len > 0U) {
        st = HAL_SPI_Transmit(spi_ctx->spi, (uint8_t *)buf, len, 1000U);
    }
    HAL_GPIO_WritePin(spi_ctx->cs_port, spi_ctx->cs_pin, GPIO_PIN_SET);

    return (st == HAL_OK) ? 0 : -1;
}

static int32_t sensor_bus_spi_read_reg(void *user_ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    SensorBusSpiContext_t *spi_ctx;
    HAL_StatusTypeDef st;
    uint16_t transfer_len;

    spi_ctx = (SensorBusSpiContext_t *)user_ctx;
    if (spi_ctx == NULL || spi_ctx->spi == NULL) {
        return -1;
    }

    transfer_len = (uint16_t)(len + 1U);
    reg |= 0x80U;
    if (len > 1U) {
        reg |= 0x40U;
    }

    if (sensor_bus_spi_dma_ready(spi_ctx, transfer_len)) {
        if (osMutexAcquire(spi_ctx->dma->bus_lock, osWaitForever) != osOK) {
            return -1;
        }

        spi_ctx->dma->tx_buf[0] = reg;
        if (len > 0U) {
            memset(&spi_ctx->dma->tx_buf[1], 0xFF, len);
        }

        while (osSemaphoreAcquire(spi_ctx->dma->done_sem, 0U) == osOK) {
        }

        spi_ctx->transfer_state = SENSOR_BUS_XFER_BUSY;
        spi_ctx->transfer_owns_cs = true;
        HAL_GPIO_WritePin(spi_ctx->cs_port, spi_ctx->cs_pin, GPIO_PIN_RESET);

        if (HAL_SPI_TransmitReceive_DMA(spi_ctx->spi,
                                        spi_ctx->dma->tx_buf,
                                        spi_ctx->dma->rx_buf,
                                        transfer_len) != HAL_OK) {
            sensor_bus_spi_complete(spi_ctx, SENSOR_BUS_XFER_ERROR);
            (void)osMutexRelease(spi_ctx->dma->bus_lock);
            return -1;
        }

        if (sensor_bus_spi_wait_dma(spi_ctx) != 0) {
            (void)osMutexRelease(spi_ctx->dma->bus_lock);
            return -1;
        }

        if (len > 0U) {
            memcpy(buf, &spi_ctx->dma->rx_buf[1], len);
        }

        (void)osMutexRelease(spi_ctx->dma->bus_lock);
        return 0;
    }

    HAL_GPIO_WritePin(spi_ctx->cs_port, spi_ctx->cs_pin, GPIO_PIN_RESET);
    st = HAL_SPI_Transmit(spi_ctx->spi, &reg, 1U, 1000U);
    if (st == HAL_OK && len > 0U) {
        st = HAL_SPI_Receive(spi_ctx->spi, buf, len, 1000U);
    }
    HAL_GPIO_WritePin(spi_ctx->cs_port, spi_ctx->cs_pin, GPIO_PIN_SET);

    return (st == HAL_OK) ? 0 : -1;
}

static void sensor_bus_spi_delay_ms(void *user_ctx, uint32_t ms)
{
    SensorBusSpiContext_t *spi_ctx;

    spi_ctx = (SensorBusSpiContext_t *)user_ctx;
    if (spi_ctx != NULL && spi_ctx->delay_ms != NULL) {
        spi_ctx->delay_ms(ms);
        return;
    }

    HAL_Delay(ms);
}

static const SensorBusVTable_t g_sensor_bus_spi_vtable = {
    .write_reg = sensor_bus_spi_write_reg,
    .read_reg = sensor_bus_spi_read_reg,
    .delay_ms = sensor_bus_spi_delay_ms,
    .start_write_reg_async = NULL,
    .start_read_reg_async = NULL,
    .get_transfer_state = NULL,
    .wait_transfer = NULL,
    .cancel_transfer = NULL
};

void SensorBusSpi_Init(SensorBusPort_t *port,
                       SensorBusSpiContext_t *spi_ctx,
                       SPI_HandleTypeDef *spi,
                       GPIO_TypeDef *cs_port,
                       uint16_t cs_pin,
                       void (*delay_ms)(uint32_t ms))
{
    if (port == NULL || spi_ctx == NULL) {
        return;
    }

    memset(spi_ctx, 0, sizeof(*spi_ctx));
    spi_ctx->spi = spi;
    spi_ctx->cs_port = cs_port;
    spi_ctx->cs_pin = cs_pin;
    spi_ctx->delay_ms = delay_ms;
    spi_ctx->transfer_state = SENSOR_BUS_XFER_IDLE;

    port->user_ctx = spi_ctx;
    port->vtable = &g_sensor_bus_spi_vtable;

    sensor_bus_spi_register_context(spi_ctx);
}

void SensorBusSpi_EnableDma(SensorBusSpiContext_t *spi_ctx, const SensorBusSpiDmaConfig_t *dma_cfg)
{
    if (spi_ctx == NULL) {
        return;
    }

    spi_ctx->dma = dma_cfg;
}

void SensorBusSpi_DisableDma(SensorBusSpiContext_t *spi_ctx)
{
    if (spi_ctx == NULL) {
        return;
    }

    spi_ctx->dma = NULL;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    SensorBusSpiContext_t *spi_ctx;

    spi_ctx = sensor_bus_spi_find_context(hspi);
    if (spi_ctx != NULL) {
        sensor_bus_spi_complete(spi_ctx, SENSOR_BUS_XFER_DONE);
    }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    SensorBusSpiContext_t *spi_ctx;

    spi_ctx = sensor_bus_spi_find_context(hspi);
    if (spi_ctx != NULL) {
        sensor_bus_spi_complete(spi_ctx, SENSOR_BUS_XFER_DONE);
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    SensorBusSpiContext_t *spi_ctx;

    spi_ctx = sensor_bus_spi_find_context(hspi);
    if (spi_ctx != NULL) {
        sensor_bus_spi_complete(spi_ctx, SENSOR_BUS_XFER_ERROR);
    }
}
