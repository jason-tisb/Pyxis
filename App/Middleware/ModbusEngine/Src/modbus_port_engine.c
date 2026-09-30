#include "modbus_port_engine.h"

#include <string.h>

static bool modbus_port_engine_rx_mode_valid(modbus_port_rx_mode_t mode) {
  return (mode == MODBUS_PORT_RX_IT) || (mode == MODBUS_PORT_RX_DMA_RTO);
}

static bool modbus_port_engine_dma_mode(modbus_port_rx_mode_t mode) {
  return (mode == MODBUS_PORT_RX_DMA_RTO);
}

static bool modbus_port_engine_it_frame_ready(modbus_port_engine_t *e) {
  if ((e == NULL) || (e->cfg.rx_mode != MODBUS_PORT_RX_IT) ||
      (e->frame_len == 0U) || (e->has_frame != 0U)) {
    return false;
  }

  if (e->frame_ready != 0U) {
    return true;
  }

  if ((e->cfg.timer_packet_ready != NULL) &&
      (e->cfg.timer_packet_ready() != 0U)) {
    e->frame_ready = 1U;
    return true;
  }

  return false;
}

static void modbus_port_engine_append(modbus_port_engine_t *e, uint8_t byte,
                                      bool reset_timer) {
  if ((e == NULL) || (e->cfg.frame_buf == NULL) ||
      (e->cfg.frame_buf_len == 0U) || (e->frame_ready != 0U) ||
      (e->has_frame != 0U) ||
      e->frame_overflow) {
    return;
  }

  if ((e->cfg.rx_mode == MODBUS_PORT_RX_IT) &&
      (e->frame_len != 0U) &&
      (e->cfg.timer_packet_ready != NULL) &&
      (e->cfg.timer_packet_ready() != 0U)) {
    e->frame_ready = 1U;
    return;
  }

  if (e->frame_len >= e->cfg.frame_buf_len) {
    e->frame_overflow = true;
    return;
  }

  e->cfg.frame_buf[e->frame_len] = byte;
  e->frame_len++;

  if (e->cfg.tick_100us != NULL) {
    e->last_rx_tick_100us = e->cfg.tick_100us(e->cfg.user);
  }

  if (reset_timer && (e->cfg.timer_reset_silence != NULL)) {
    e->cfg.timer_reset_silence();
  }
}

static bool modbus_port_engine_cfg_valid(const modbus_port_engine_cfg_t *cfg)
{
  if (cfg == NULL) {
    return false;
  }

  if (!modbus_port_engine_rx_mode_valid(cfg->rx_mode)) {
    return false;
  }

  if ((cfg->frame_buf == NULL) || (cfg->frame_buf_len == 0U)) {
    return false;
  }

  if (modbus_port_engine_dma_mode(cfg->rx_mode)) {
    if ((cfg->dma_buf == NULL) ||
        (cfg->dma_buf_len < 2U)) {
      return false;
    }
  } else {
    if ((cfg->timer_reset_silence == NULL) ||
        (cfg->timer_packet_ready == NULL) ||
        (cfg->timer_interchar_timeout == NULL)) {
      return false;
    }
  }

  return true;
}

static bool modbus_port_engine_harvest_dma_abs_to(modbus_port_engine_t *e,
                                                  uint32_t end_abs_pos) {
  uint32_t available;

  if ((e == NULL) || (e->cfg.dma_buf == NULL) || (e->cfg.dma_buf_len == 0U)) {
    return false;
  }

  available = end_abs_pos - e->dma_consumer_abs_pos;

  if (available > (uint32_t)e->cfg.dma_buf_len) {
    e->dma_error = true;
    return false;
  }

  while (available != 0U) {
    uint16_t index =
        (uint16_t)(e->dma_consumer_abs_pos % (uint32_t)e->cfg.dma_buf_len);

    modbus_port_engine_append(e, e->cfg.dma_buf[index], false);
    e->dma_consumer_abs_pos++;
    available--;
  }

  return true;
}

static bool modbus_port_engine_discard_dma_abs_to(modbus_port_engine_t *e,
                                                  uint32_t end_abs_pos)
{
  uint32_t distance;

  if (e == NULL) {
    return false;
  }

  distance = end_abs_pos - e->dma_consumer_abs_pos;

  if (distance > (uint32_t)e->cfg.dma_buf_len) {
    e->dma_error = true;
    return false;
  }

  e->dma_consumer_abs_pos = end_abs_pos;
  e->frame_len = 0U;
  e->frame_ready = 0U;
  e->has_frame = 0U;
  e->frame_overflow = false;

  return true;
}

static void modbus_port_engine_discard_it_frame(modbus_port_engine_t *e)
{
  if (e == NULL) {
    return;
  }

  e->frame_len = 0U;
  e->frame_ready = 0U;
  e->has_frame = 0U;
  e->frame_overflow = false;
}

bool modbus_port_engine_init(modbus_port_engine_t *e,
                             const modbus_port_engine_cfg_t *cfg) {
  if ((e == NULL) || !modbus_port_engine_cfg_valid(cfg)) {
    return false;
  }

  memset(e, 0, sizeof(*e));
  e->cfg = *cfg;
  modbus_port_engine_reset(e);

  return true;
}

void modbus_port_engine_reset(modbus_port_engine_t *e) {
  if (e == NULL) {
    return;
  }

  e->frame_len = 0U;
  e->dma_consumer_abs_pos = 0U;
  e->frame_ready = 0U;
  e->has_frame = 0U;
  e->frame_overflow = false;
  e->dma_error = false;
  e->last_rx_tick_100us = 0U;

  if ((e->cfg.frame_buf != NULL) && (e->cfg.frame_buf_len != 0U)) {
    memset(e->cfg.frame_buf, 0, e->cfg.frame_buf_len);
  }
}

void modbus_port_engine_clear_frame(modbus_port_engine_t *e) {
  if (e == NULL) {
    return;
  }

  e->frame_len = 0U;
  e->frame_ready = 0U;
  e->has_frame = 0U;
  e->frame_overflow = false;
}

void modbus_port_engine_on_rx_byte(modbus_port_engine_t *e, uint8_t byte) {
  if (modbus_port_engine_it_frame_ready(e)) {
    return;
  }

  modbus_port_engine_append(e, byte, true);
}

/*
 * When MODBUS_PORT_FRAME_READY is returned, *rx points to the engine-owned
 * frame buffer. The buffer remains valid only until the frame is cleared,
 * the engine is reset, or a later discard/overwrite operation invalidates it.
 *
 * The caller must process the frame completely before calling
 * modbus_port_poll_frame() again.
 */
modbus_port_poll_status_t modbus_port_engine_poll_frame(
    modbus_port_engine_t *e,
    const uint8_t **rx,
    uint16_t *rx_len,
    bool dma_frame_ready_requested,
    bool dma_frame_invalid_requested,
    uint32_t dma_frame_end_abs_pos,
    uint32_t dma_frame_invalid_end_abs_pos)
{
  if (rx != NULL) {
    *rx = NULL;
  }

  if (rx_len != NULL) {
    *rx_len = 0U;
  }

  if ((e == NULL) || (rx == NULL) || (rx_len == NULL)) {
    return MODBUS_PORT_POLL_NOT_INITIALIZED;
  }

  if (dma_frame_invalid_requested) {
    if (e->cfg.rx_mode != MODBUS_PORT_RX_DMA_RTO) {
      return MODBUS_PORT_DMA_ERROR;
    }

    if (!modbus_port_engine_discard_dma_abs_to(
            e,
            dma_frame_invalid_end_abs_pos)) {
      return MODBUS_PORT_DMA_ERROR;
    }

    return MODBUS_PORT_INTERCHAR_TIMEOUT;
  }

  if ((e->cfg.rx_mode == MODBUS_PORT_RX_IT) &&
      (e->frame_len != 0U) &&
      (e->cfg.timer_interchar_timeout != NULL) &&
      (e->cfg.timer_interchar_timeout() != 0U)) {
    modbus_port_engine_discard_it_frame(e);
    return MODBUS_PORT_INTERCHAR_TIMEOUT;
  }

  if (e->has_frame != 0U) {
    return MODBUS_PORT_NO_FRAME;
  }

  if (e->cfg.rx_mode == MODBUS_PORT_RX_DMA_RTO) {
    if (!dma_frame_ready_requested) {
      return MODBUS_PORT_NO_FRAME;
    }

    if (!modbus_port_engine_harvest_dma_abs_to(
            e,
            dma_frame_end_abs_pos)) {
      return MODBUS_PORT_DMA_ERROR;
    }

    if (e->dma_error) {
      return MODBUS_PORT_DMA_ERROR;
    }

    if (e->frame_overflow) {
      return MODBUS_PORT_FRAME_OVERFLOW;
    }

    if (e->frame_len == 0U) {
      return MODBUS_PORT_NO_FRAME;
    }

    e->has_frame = 1U;
    *rx = e->cfg.frame_buf;
    *rx_len = e->frame_len;
    return MODBUS_PORT_FRAME_READY;
  }

  /* IT mode only below here. */
  if (e->dma_error) {
    return MODBUS_PORT_DMA_ERROR;
  }

  if (e->frame_overflow) {
    return MODBUS_PORT_FRAME_OVERFLOW;
  }

  if (e->frame_len == 0U) {
    return MODBUS_PORT_NO_FRAME;
  }

  if (modbus_port_engine_it_frame_ready(e)) {
    e->has_frame = 1U;
    *rx = e->cfg.frame_buf;
    *rx_len = e->frame_len;
    return MODBUS_PORT_FRAME_READY;
  }

  return MODBUS_PORT_NO_FRAME;
}
