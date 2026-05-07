#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t (*write)(void *user_ctx, const uint8_t *buf, uint16_t len);
    int32_t (*read)(void *user_ctx, uint8_t *buf, uint16_t len, uint32_t timeout_ms);
    int32_t (*flush)(void *user_ctx);
} UartPortVTable_t;

typedef struct {
    void *user_ctx;
    const UartPortVTable_t *vtable;
} UartPort_t;

static inline int32_t UartPort_Write(const UartPort_t *port, const uint8_t *buf, uint16_t len)
{
    if (port == NULL || port->vtable == NULL || port->vtable->write == NULL) {
        return -1;
    }
    return port->vtable->write(port->user_ctx, buf, len);
}

static inline int32_t UartPort_Read(const UartPort_t *port, uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->read == NULL) {
        return -1;
    }
    return port->vtable->read(port->user_ctx, buf, len, timeout_ms);
}

static inline int32_t UartPort_Flush(const UartPort_t *port)
{
    if (port == NULL || port->vtable == NULL || port->vtable->flush == NULL) {
        return -1;
    }
    return port->vtable->flush(port->user_ctx);
}

#ifdef __cplusplus
}
#endif
