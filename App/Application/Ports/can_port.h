#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t id;
    uint8_t dlc;
    bool extended_id;
    uint8_t data[64];
} CanPortFrame_t;

typedef struct {
    int32_t (*send)(void *user_ctx, const CanPortFrame_t *frame, uint32_t timeout_ms);
    int32_t (*receive)(void *user_ctx, CanPortFrame_t *frame, uint32_t timeout_ms);
} CanPortVTable_t;

typedef struct {
    void *user_ctx;
    const CanPortVTable_t *vtable;
} CanPort_t;

static inline int32_t CanPort_Send(const CanPort_t *port, const CanPortFrame_t *frame, uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->send == NULL) {
        return -1;
    }
    return port->vtable->send(port->user_ctx, frame, timeout_ms);
}

static inline int32_t CanPort_Receive(const CanPort_t *port, CanPortFrame_t *frame, uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->receive == NULL) {
        return -1;
    }
    return port->vtable->receive(port->user_ctx, frame, timeout_ms);
}

#ifdef __cplusplus
}
#endif
