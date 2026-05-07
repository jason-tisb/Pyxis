#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "main.h"
#include "cmsis_os2.h"

typedef enum {
    SENSOR_BUS_XFER_IDLE = 0,
    SENSOR_BUS_XFER_BUSY,
    SENSOR_BUS_XFER_DONE,
    SENSOR_BUS_XFER_ERROR
} SensorBusTransferState_t;

typedef struct {
    int32_t (*write_reg)(void *user_ctx, uint8_t reg, const uint8_t *buf, uint16_t len);
    int32_t (*read_reg)(void *user_ctx, uint8_t reg, uint8_t *buf, uint16_t len);
    void (*delay_ms)(void *user_ctx, uint32_t ms);
    int32_t (*start_write_reg_async)(void *user_ctx, uint8_t reg, const uint8_t *buf, uint16_t len);
    int32_t (*start_read_reg_async)(void *user_ctx, uint8_t reg, uint8_t *buf, uint16_t len);
    SensorBusTransferState_t (*get_transfer_state)(void *user_ctx);
    int32_t (*wait_transfer)(void *user_ctx, uint32_t timeout_ms);
    int32_t (*cancel_transfer)(void *user_ctx);
} SensorBusVTable_t;

typedef struct {
    void *user_ctx;
    const SensorBusVTable_t *vtable;
} SensorBusPort_t;

typedef struct SensorBusSpiDmaConfig {
    osSemaphoreId_t done_sem;
    osMutexId_t bus_lock;
    uint8_t *tx_buf;
    uint8_t *rx_buf;
    uint16_t buffer_size;
    uint32_t timeout_ms;
} SensorBusSpiDmaConfig_t;

typedef struct SensorBusSpiContext {
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    void (*delay_ms)(uint32_t ms);
    const SensorBusSpiDmaConfig_t *dma;
    volatile SensorBusTransferState_t transfer_state;
    volatile bool transfer_owns_cs;
} SensorBusSpiContext_t;

void SensorBusSpi_Init(SensorBusPort_t *port,
                       SensorBusSpiContext_t *spi_ctx,
                       SPI_HandleTypeDef *spi,
                       GPIO_TypeDef *cs_port,
                       uint16_t cs_pin,
                       void (*delay_ms)(uint32_t ms));
void SensorBusSpi_EnableDma(SensorBusSpiContext_t *spi_ctx, const SensorBusSpiDmaConfig_t *dma_cfg);
void SensorBusSpi_DisableDma(SensorBusSpiContext_t *spi_ctx);

static inline int32_t SensorBus_WriteReg(const SensorBusPort_t *port, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    if (port == NULL || port->vtable == NULL || port->vtable->write_reg == NULL) {
        return -1;
    }
    return port->vtable->write_reg(port->user_ctx, reg, buf, len);
}

static inline int32_t SensorBus_ReadReg(const SensorBusPort_t *port, uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (port == NULL || port->vtable == NULL || port->vtable->read_reg == NULL) {
        return -1;
    }
    return port->vtable->read_reg(port->user_ctx, reg, buf, len);
}

static inline void SensorBus_DelayMs(const SensorBusPort_t *port, uint32_t ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->delay_ms == NULL) {
        return;
    }
    port->vtable->delay_ms(port->user_ctx, ms);
}

static inline bool SensorBus_SupportsAsync(const SensorBusPort_t *port)
{
    return (port != NULL) &&
           (port->vtable != NULL) &&
           ((port->vtable->start_write_reg_async != NULL) || (port->vtable->start_read_reg_async != NULL));
}

static inline int32_t SensorBus_StartWriteRegAsync(const SensorBusPort_t *port,
                                                   uint8_t reg,
                                                   const uint8_t *buf,
                                                   uint16_t len)
{
    if (port == NULL || port->vtable == NULL || port->vtable->start_write_reg_async == NULL) {
        return -1;
    }
    return port->vtable->start_write_reg_async(port->user_ctx, reg, buf, len);
}

static inline int32_t SensorBus_StartReadRegAsync(const SensorBusPort_t *port,
                                                  uint8_t reg,
                                                  uint8_t *buf,
                                                  uint16_t len)
{
    if (port == NULL || port->vtable == NULL || port->vtable->start_read_reg_async == NULL) {
        return -1;
    }
    return port->vtable->start_read_reg_async(port->user_ctx, reg, buf, len);
}

static inline SensorBusTransferState_t SensorBus_GetTransferState(const SensorBusPort_t *port)
{
    if (port == NULL || port->vtable == NULL || port->vtable->get_transfer_state == NULL) {
        return SENSOR_BUS_XFER_IDLE;
    }
    return port->vtable->get_transfer_state(port->user_ctx);
}

static inline int32_t SensorBus_WaitTransfer(const SensorBusPort_t *port, uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->wait_transfer == NULL) {
        return -1;
    }
    return port->vtable->wait_transfer(port->user_ctx, timeout_ms);
}

static inline int32_t SensorBus_CancelTransfer(const SensorBusPort_t *port)
{
    if (port == NULL || port->vtable == NULL || port->vtable->cancel_transfer == NULL) {
        return -1;
    }
    return port->vtable->cancel_transfer(port->user_ctx);
}

#ifdef __cplusplus
}
#endif
