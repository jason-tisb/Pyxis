#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    int16_t rssi_dbm;
    int8_t snr_db;
} LoRaPortRxMeta_t;

typedef struct {
    int32_t (*send)(void *user_ctx, const uint8_t *payload, uint16_t len, uint32_t timeout_ms);
    int32_t (*receive)(void *user_ctx, uint8_t *payload, uint16_t max_len, uint16_t *out_len, LoRaPortRxMeta_t *out_meta, uint32_t timeout_ms);
} LoRaPortVTable_t;

typedef struct {
    void *user_ctx;
    const LoRaPortVTable_t *vtable;
} LoRaPort_t;

static inline int32_t LoRaPort_Send(const LoRaPort_t *port, const uint8_t *payload, uint16_t len, uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->send == NULL) {
        return -1;
    }
    return port->vtable->send(port->user_ctx, payload, len, timeout_ms);
}

static inline int32_t LoRaPort_Receive(const LoRaPort_t *port,
                                       uint8_t *payload,
                                       uint16_t max_len,
                                       uint16_t *out_len,
                                       LoRaPortRxMeta_t *out_meta,
                                       uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->receive == NULL) {
        return -1;
    }
    return port->vtable->receive(port->user_ctx, payload, max_len, out_len, out_meta, timeout_ms);
}

#ifdef __cplusplus
}
#endif
