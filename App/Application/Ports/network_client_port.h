#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *scheme;
    const char *host;
    uint16_t port;
    const char *path;
} NetworkEndpoint_t;

typedef struct {
    int32_t (*connect)(void *user_ctx, const NetworkEndpoint_t *endpoint, uint32_t timeout_ms);
    int32_t (*disconnect)(void *user_ctx);
    bool (*is_connected)(void *user_ctx);
} NetworkClientPortVTable_t;

typedef struct {
    void *user_ctx;
    const NetworkClientPortVTable_t *vtable;
} NetworkClientPort_t;

static inline int32_t NetworkClientPort_Connect(const NetworkClientPort_t *port,
                                                const NetworkEndpoint_t *endpoint,
                                                uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->connect == NULL) {
        return -1;
    }
    return port->vtable->connect(port->user_ctx, endpoint, timeout_ms);
}

static inline int32_t NetworkClientPort_Disconnect(const NetworkClientPort_t *port)
{
    if (port == NULL || port->vtable == NULL || port->vtable->disconnect == NULL) {
        return -1;
    }
    return port->vtable->disconnect(port->user_ctx);
}

static inline bool NetworkClientPort_IsConnected(const NetworkClientPort_t *port)
{
    if (port == NULL || port->vtable == NULL || port->vtable->is_connected == NULL) {
        return false;
    }
    return port->vtable->is_connected(port->user_ctx);
}

#ifdef __cplusplus
}
#endif
