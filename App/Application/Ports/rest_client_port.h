#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    const char *method;
    const char *url;
    const char *content_type;
    const uint8_t *body;
    uint32_t body_len;
} RestClientRequest_t;

typedef struct {
    uint16_t status_code;
    const uint8_t *body;
    uint32_t body_len;
} RestClientResponse_t;

typedef struct {
    int32_t (*perform)(void *user_ctx,
                       const RestClientRequest_t *request,
                       RestClientResponse_t *response,
                       uint32_t timeout_ms);
} RestClientPortVTable_t;

typedef struct {
    void *user_ctx;
    const RestClientPortVTable_t *vtable;
} RestClientPort_t;

static inline int32_t RestClientPort_Perform(const RestClientPort_t *port,
                                             const RestClientRequest_t *request,
                                             RestClientResponse_t *response,
                                             uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->perform == NULL) {
        return -1;
    }
    return port->vtable->perform(port->user_ctx, request, response, timeout_ms);
}

#ifdef __cplusplus
}
#endif
