#include "modbus_engine_port.h"

extern volatile uint32_t g_modbus_frame_boundary_tick;

#include "main.h"
#include "cmsis_os2.h"
#include "modbus_port_engine.h"

#define MODBUS_ENGINE_FRAME_SIZE 256U
#define MODBUS_ENGINE_SILENCE_MS_DEFAULT 5U
#define MODBUS_ENGINE_WORKER_SILENCE_MS_MIN 5U

static modbus_port_engine_t engine;
static uint8_t engine_frame[MODBUS_ENGINE_FRAME_SIZE];
static volatile uint32_t frame_count;
static volatile uint32_t overflow_count;
static volatile uint32_t silence_deadline;
static volatile uint32_t silence_ms = MODBUS_ENGINE_SILENCE_MS_DEFAULT;
static osMutexId_t engine_mutex;

static void engine_lock(void)
{
    if (engine_mutex != NULL) {
        (void)osMutexAcquire(engine_mutex, osWaitForever);
    }
}

static void engine_unlock(void)
{
    if (engine_mutex != NULL) {
        (void)osMutexRelease(engine_mutex);
    }
}

static void engine_timer_reset_silence(void)
{
    silence_deadline = HAL_GetTick() + silence_ms;
}

static uint8_t engine_timer_expired(void)
{
    return ((int32_t)(HAL_GetTick() - silence_deadline) >= 0) ? 1U : 0U;
}

static uint8_t engine_timer_interchar_timeout(void)
{
    /* The task converts silence into frame_ready below.  Do not discard the
     * completed frame through the engine's inter-character error path. */
    return 0U;
}

static uint8_t engine_timer_packet_ready(void)
{
    return engine_timer_expired();
}

void ModbusEnginePort_Init(void)
{
    const modbus_port_engine_cfg_t cfg = {
        .rx_mode = MODBUS_PORT_RX_IT,
        .frame_buf = engine_frame,
        .frame_buf_len = MODBUS_ENGINE_FRAME_SIZE,
        .dma_buf = NULL,
        .dma_buf_len = 0U,
        .get_dma_pos = NULL,
        .timer_reset_silence = engine_timer_reset_silence,
        .timer_packet_ready = engine_timer_packet_ready,
        .timer_interchar_timeout = engine_timer_interchar_timeout,
        .tick_100us = NULL,
        .user = NULL
    };

    frame_count = 0U;
    overflow_count = 0U;
    silence_deadline = HAL_GetTick();
    silence_ms = MODBUS_ENGINE_SILENCE_MS_DEFAULT;
    engine_mutex = osMutexNew(NULL);
    (void)modbus_port_engine_init(&engine, &cfg);
}

void ModbusEnginePort_SetRtuTiming(uint32_t baud_rate, uint8_t data_bits,
                                   uint8_t parity_code, uint32_t stop_bits)
{
    uint32_t bits_per_character;
    uint64_t t3_5_us;
    uint32_t computed_ms;

    if (baud_rate == 0U) {
        return;
    }

    /* Start bit + data bits + optional parity + stop bits. */
    bits_per_character = 1U + (uint32_t)data_bits +
                         ((parity_code <= 1U) ? 1U : 0U) +
                         ((stop_bits == UART_STOPBITS_2) ? 2U : 1U);
    t3_5_us = ((uint64_t)bits_per_character * 3500000ULL +
               (uint64_t)baud_rate - 1ULL) / (uint64_t)baud_rate;
    computed_ms = (uint32_t)((t3_5_us + 999ULL) / 1000ULL);
    if (computed_ms == 0U) {
        computed_ms = 1U;
    }
    /* The current parser is fed by a FreeRTOS worker rather than a hardware
     * character-timer ISR. Prevent a scheduler gap from splitting one RTU
     * request into several frames. */
    if (computed_ms < MODBUS_ENGINE_WORKER_SILENCE_MS_MIN) {
        computed_ms = MODBUS_ENGINE_WORKER_SILENCE_MS_MIN;
    }

    engine_lock();
    silence_ms = computed_ms;
    engine_unlock();
}

void ModbusEnginePort_OnRxByte(uint8_t byte)
{
    engine_lock();
    modbus_port_engine_on_rx_byte(&engine, byte);
    engine_unlock();
}

void ModbusEnginePort_OnRxFrameBoundary(void)
{
    g_modbus_frame_boundary_tick = HAL_GetTick();
    engine_lock();
    modbus_port_engine_on_rx_boundary(&engine);
    engine_unlock();
}

bool ModbusEnginePort_FramePending(void)
{
    bool pending;
    engine_lock();
    pending = (engine.frame_ready != 0U) || (engine.has_frame != 0U);
    engine_unlock();
    return pending;
}

bool ModbusEnginePort_Poll(const uint8_t **frame, uint16_t *length)
{
    bool ready = false;

    engine_lock();

    /* There is no byte after the final RTU character to latch frame_ready.
     * Feed a sentinel only to trigger the engine's packet-ready transition;
     * modbus_port_engine_on_rx_byte() will not append it once the timer says
     * the packet is complete. */
    if ((engine.frame_len != 0U) && engine_timer_expired()) {
        modbus_port_engine_on_rx_byte(&engine, 0U);
    }

    const modbus_port_poll_status_t status = modbus_port_engine_poll_frame(
        &engine, frame, length, false, false, 0U, 0U);

    if (status == MODBUS_PORT_FRAME_READY) {
        frame_count++;
        ready = true;
        goto done;
    }

    if (status == MODBUS_PORT_FRAME_OVERFLOW) {
        overflow_count++;
        modbus_port_engine_clear_frame(&engine);
    }

done:
    engine_unlock();
    return ready;
}

void ModbusEnginePort_ClearFrame(void)
{
    engine_lock();
    modbus_port_engine_clear_frame(&engine);
    engine_unlock();
}

uint32_t ModbusEnginePort_GetFrameCount(void)
{
    return frame_count;
}

uint32_t ModbusEnginePort_GetOverflowCount(void)
{
    return overflow_count;
}
