#ifndef INC_MODBUS_PORT_ENGINE_H_
#define INC_MODBUS_PORT_ENGINE_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  MODBUS_PORT_RX_IT = 0,
  MODBUS_PORT_RX_DMA_RTO
} modbus_port_rx_mode_t;

typedef struct {
  modbus_port_rx_mode_t rx_mode;

  uint8_t *frame_buf;
  uint16_t frame_buf_len;

  uint8_t *dma_buf;
  uint16_t dma_buf_len;

  uint16_t (*get_dma_pos)(void *user);
  void (*timer_reset_silence)(void);
  uint8_t (*timer_packet_ready)(void);
  uint8_t (*timer_interchar_timeout)(void);
  uint32_t (*tick_100us)(void *user);
  void *user;
} modbus_port_engine_cfg_t;

typedef struct {
  modbus_port_engine_cfg_t cfg;
  volatile uint16_t frame_len;
  volatile uint8_t frame_ready;
  volatile uint8_t has_frame;
  volatile bool frame_overflow;
  bool dma_error;
  volatile uint32_t last_rx_tick_100us;
  uint32_t dma_consumer_abs_pos;
} modbus_port_engine_t;

typedef enum {
  MODBUS_PORT_NO_FRAME = 0,
  MODBUS_PORT_FRAME_READY,
  MODBUS_PORT_INTERCHAR_TIMEOUT,
  MODBUS_PORT_POLL_NOT_INITIALIZED,
  MODBUS_PORT_FRAME_OVERFLOW,
  MODBUS_PORT_DMA_ERROR
} modbus_port_poll_status_t;

bool modbus_port_engine_init(modbus_port_engine_t *e,
                             const modbus_port_engine_cfg_t *cfg);
void modbus_port_engine_reset(modbus_port_engine_t *e);
void modbus_port_engine_on_rx_byte(modbus_port_engine_t *e, uint8_t b);

modbus_port_poll_status_t modbus_port_engine_poll_frame(
    modbus_port_engine_t *e,
    const uint8_t **rx,
    uint16_t *rx_len,
    bool dma_frame_ready_requested,
    bool dma_frame_invalid_requested,
    uint32_t dma_frame_end_abs_pos,
    uint32_t dma_frame_invalid_end_abs_pos);

void modbus_port_engine_clear_frame(modbus_port_engine_t *e);
#endif
