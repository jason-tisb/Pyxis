#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *client_id;
    const char *username;
    const char *password;
    bool clean_session;
    uint16_t keep_alive_s;
} MqttClientConfig_t;

typedef struct {
    const char *topic;
    const uint8_t *payload;
    uint16_t payload_len;
    uint8_t qos;
    bool retain;
} MqttPublishMessage_t;

typedef struct {
    int32_t (*connect)(void *user_ctx, const MqttClientConfig_t *config, uint32_t timeout_ms);
    int32_t (*disconnect)(void *user_ctx);
    int32_t (*publish)(void *user_ctx, const MqttPublishMessage_t *message, uint32_t timeout_ms);
    int32_t (*subscribe)(void *user_ctx, const char *topic_filter, uint8_t qos, uint32_t timeout_ms);
} MqttClientPortVTable_t;

typedef struct {
    void *user_ctx;
    const MqttClientPortVTable_t *vtable;
} MqttClientPort_t;

static inline int32_t MqttClientPort_Connect(const MqttClientPort_t *port,
                                             const MqttClientConfig_t *config,
                                             uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->connect == NULL) {
        return -1;
    }
    return port->vtable->connect(port->user_ctx, config, timeout_ms);
}

static inline int32_t MqttClientPort_Disconnect(const MqttClientPort_t *port)
{
    if (port == NULL || port->vtable == NULL || port->vtable->disconnect == NULL) {
        return -1;
    }
    return port->vtable->disconnect(port->user_ctx);
}

static inline int32_t MqttClientPort_Publish(const MqttClientPort_t *port,
                                             const MqttPublishMessage_t *message,
                                             uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->publish == NULL) {
        return -1;
    }
    return port->vtable->publish(port->user_ctx, message, timeout_ms);
}

static inline int32_t MqttClientPort_Subscribe(const MqttClientPort_t *port,
                                               const char *topic_filter,
                                               uint8_t qos,
                                               uint32_t timeout_ms)
{
    if (port == NULL || port->vtable == NULL || port->vtable->subscribe == NULL) {
        return -1;
    }
    return port->vtable->subscribe(port->user_ctx, topic_filter, qos, timeout_ms);
}

#ifdef __cplusplus
}
#endif
