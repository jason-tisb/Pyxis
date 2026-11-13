#include "port.h"
#include "main.h"
#include "cmsis_os2.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"
#include "modbus_engine_config.h"
#include "modbus_engine_hw_config.h"
#include "modbus_engine_port.h"
#include "modbus_engine_tx.h"

#if MODBUS_ENGINE_ACTIVE && MODBUS_AUTOBAUD_V2_PORT_ENABLE
#include "modbus_engine_v2_transport.h"
#endif
#if MODBUS_ENGINE_ACTIVE && MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
#include <string.h>
#endif

#if MODBUS_ENGINE_ACTIVE

/*
 * Custom-engine transport.  The UART/DMA ISR records the DMA producer and
 * wakes a worker task.  The worker task harvests bytes and feeds the engine outside
 * interrupt context, preserving the transport boundary used by the original
 * stable FreeModbus implementation without invoking FreeModbus itself.
 */
#define RB_SIZE                 MODBUS_PYXIS_RX_RING_SIZE
#define RB_MASK                 (RB_SIZE - 1U)
#define UART3_RX_DMA_SIZE       MODBUS_PYXIS_RX_DMA_SIZE
#define EVENT_CUSTOM_RX         (1U << 0)
#define RX_BOUNDARY_QUEUE_SIZE  8U
#define RX_BOUNDARY_QUEUE_MASK  (RX_BOUNDARY_QUEUE_SIZE - 1U)
#define UART3_FRAME_QUEUE_DEPTH 4U
#define UART3_FRAME_MAX_SIZE    256U

extern UART_HandleTypeDef huart3;
extern uint32_t g_modbus_stop_bits;

static volatile uint8_t uart3_rx_ring[RB_SIZE];
static volatile uint16_t uart3_rx_head;
static volatile uint16_t uart3_rx_tail;
static volatile uint32_t uart3_rx_overruns;
static uint8_t uart3_rx_dma_buf[UART3_RX_DMA_SIZE];
static volatile uint8_t uart3_rx_enabled;
static volatile uint8_t uart3_rx_dma_running;
static volatile uint16_t uart3_rx_dma_last_pos;
static volatile uint32_t uart3_rx_dma_harvest_abs;
static volatile uint32_t uart3_rx_boundary_queue[RX_BOUNDARY_QUEUE_SIZE];
static volatile uint8_t uart3_rx_boundary_head;
static volatile uint8_t uart3_rx_boundary_tail;
static volatile uint32_t uart3_rto_pending;
/* UART/DMA error recovery is completed by the worker task, not from the
 * interrupt callback.  HAL aborts the DMA receive asynchronously on a
 * blocking UART error, so restarting it directly from the callback can race
 * the HAL state transition back to READY. */
static volatile uint8_t uart3_rx_recovery_pending;

/* V2 M4 transaction gate. Compiled only when explicitly opted in.
 * The existing custom RX worker is the ONLY owner of ring/queue mutation;
 * a claimant must wait for worker_parked before quiescence or reset. */
#if MODBUS_AUTOBAUD_V2_PORT_ENABLE
static volatile uint8_t s_v2_claimed;
static volatile uint8_t s_v2_park_requested;
static volatile uint8_t s_v2_worker_parked;
static volatile uint8_t s_v2_passive;
static volatile uint8_t s_v2_expected_passive;
static volatile uint8_t s_v2_restart_ready;
static volatile uint8_t s_v2_fatal;
static volatile uint32_t s_v2_session_id;
static volatile uint32_t s_v2_rx_error_generation;
#define PYXIS_V2_WORKER_PARK_TIMEOUT_MS 20U
#endif

typedef struct {
    uint8_t data[UART3_FRAME_MAX_SIZE];
    uint16_t length;
} uart3_frame_slot_t;
static uart3_frame_slot_t uart3_frame_queue[UART3_FRAME_QUEUE_DEPTH];
static uint8_t uart3_frame_queue_head;
static uint8_t uart3_frame_queue_tail;
static uint8_t uart3_frame_queue_count;
#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
/* One worker-producer, one Modbus-task consumer. The producer NEVER
 * overwrites an unread slot. Overflow invalidates the entire session.
 * The normal Modbus engine cannot access these buffers. */
#define PYXIS_V2_PASSIVE_SLOTS 4U
#define PYXIS_V2_FRAME_QUIET_MS 5U
typedef struct {
    uint8_t data[UART3_FRAME_MAX_SIZE];
    uint16_t length;
    uint32_t session_id;
    uint32_t frame_id;
    uint32_t error_generation;
} pyxis_v2_passive_slot_t;
static pyxis_v2_passive_slot_t s_v2_passive_queue[PYXIS_V2_PASSIVE_SLOTS];
static volatile uint32_t s_v2_passive_produced;
static volatile uint32_t s_v2_passive_consumed;
static volatile uint32_t s_v2_frame_sequence;
static uint32_t s_v2_last_consumed_frame;
static uint32_t s_v2_session_error_baseline;
#endif
static osEventFlagsId_t custom_rx_event;
static osThreadId_t custom_rx_task;
static const osThreadAttr_t custom_rx_task_attr = {
    .name = "customMbRx",
    .priority = (osPriority_t)osPriorityAboveNormal4,
    .stack_size = 1024U
};

volatile uint32_t g_uart3_rx_callback_count = 0;
volatile uint32_t g_uart3_rx_forwarded_bytes = 0;
volatile uint16_t g_uart3_last_rx_size = 0;
volatile uint32_t g_uart3_last_event_type = 0;
volatile uint32_t g_custom_uart_init_stage = 0;
volatile uint32_t g_custom_uart_hal_status = 0;
volatile uint16_t g_uart3_dma_last_start = 0;
volatile uint16_t g_uart3_dma_last_end = 0;
volatile uint16_t g_uart3_dma_last_count = 0;
volatile uint32_t g_uart3_rx_overruns = 0;
volatile uint32_t g_uart3_dma_producer_abs = 0;
volatile uint32_t g_uart3_dma_wrap_count = 0;
volatile uint32_t g_uart3_dma_position_errors = 0;
volatile uint32_t g_uart3_dma_overruns = 0;
volatile uint32_t g_uart3_rto_count = 0;
volatile uint32_t g_modbus_frame_boundary_tick = 0;
volatile uint32_t g_modbus_rx_first_tick = 0;
volatile uint32_t g_modbus_rx_last_tick = 0;
volatile uint32_t g_uart3_dma_restart_tick = 0;
volatile uint32_t g_uart3_rx_callback_tick = 0;
volatile uint32_t g_uart3_last_disable_tick = 0;
volatile uint32_t g_uart3_last_enable_tick = 0;
/* RX event-to-worker handoff diagnostics. */
volatile uint32_t g_uart3_callbacks_while_disabled = 0;
volatile uint32_t g_uart3_boundaries_queued = 0;
volatile uint32_t g_uart3_boundaries_harvested = 0;
volatile uint32_t g_uart3_zero_length_events = 0;
volatile uint32_t g_uart3_boundary_flush_timeouts = 0;
volatile uint32_t g_uart3_frame_queue_pushed = 0;
volatile uint32_t g_uart3_frame_queue_popped = 0;
volatile uint32_t g_uart3_frame_queue_overruns = 0;
volatile uint32_t g_uart3_frame_queue_max_depth = 0;
volatile uint32_t g_uart3_error_recovery_attempts = 0;
volatile uint32_t g_uart3_error_recovery_failures = 0;
volatile uint32_t g_uart3_transport_flushes = 0;
volatile uint32_t g_uart3_last_recovery_status = 0;

static BOOL uart3_start_custom_dma(void)
{
    g_uart3_dma_restart_tick = HAL_GetTick();
    if (uart3_rx_dma_running != 0U) {
        return TRUE;
    }
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart3, uart3_rx_dma_buf,
                                     UART3_RX_DMA_SIZE) != HAL_OK) {
        return FALSE;
    }
    uart3_rx_dma_running = 1U;
    return TRUE;
}

static void uart3_stop_custom_dma(void)
{
    if (uart3_rx_dma_running != 0U) {
        (void)HAL_UART_AbortReceive(&huart3);
        uart3_rx_dma_running = 0U;
    }
}

static void uart3_harvest_dma_range(uint32_t start_abs, uint32_t end_abs)
{
    uint32_t distance = end_abs - start_abs;
    uint32_t pos_abs;
    uint16_t count;
    BOOL received = FALSE;

    /* The ISR may be delayed while the DMA continues. Keep only the newest
     * complete window if the producer has lapped the foreground harvester. */
    if (distance > UART3_RX_DMA_SIZE) {
        g_uart3_dma_overruns++;
#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
        ++s_v2_rx_error_generation;
#endif
        start_abs = end_abs - UART3_RX_DMA_SIZE;
        distance = UART3_RX_DMA_SIZE;
    }
    count = (uint16_t)distance;

    g_uart3_dma_last_start = (uint16_t)(start_abs % UART3_RX_DMA_SIZE);
    g_uart3_dma_last_end = (uint16_t)(end_abs % UART3_RX_DMA_SIZE);
    g_uart3_dma_last_count = count;

    pos_abs = start_abs;
    while (count-- != 0U) {
        uint16_t pos = (uint16_t)(pos_abs % UART3_RX_DMA_SIZE);
        uint16_t next = (uint16_t)((uart3_rx_head + 1U) & RB_MASK);

        if (next == uart3_rx_tail) {
            /* Preserve the newest data, matching the old stable transport. */
            uart3_rx_tail = (uint16_t)((uart3_rx_tail + 1U) & RB_MASK);
            uart3_rx_overruns++;
            g_uart3_rx_overruns = uart3_rx_overruns;
#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
            ++s_v2_rx_error_generation;
#endif
        }

        uart3_rx_ring[uart3_rx_head] = uart3_rx_dma_buf[pos];
        uart3_rx_head = next;
        received = TRUE;
        pos_abs++;
    }

    if ((received != FALSE) && (custom_rx_event != NULL)) {
        (void)osEventFlagsSet(custom_rx_event, EVENT_CUSTOM_RX);
    }
}

static void uart3_queue_boundary(uint32_t end_abs)
{
    uint8_t next = (uint8_t)((uart3_rx_boundary_head + 1U) &
                             RX_BOUNDARY_QUEUE_MASK);
    if (next == uart3_rx_boundary_tail) {
        uart3_rx_boundary_tail = (uint8_t)((uart3_rx_boundary_tail + 1U) &
                                           RX_BOUNDARY_QUEUE_MASK);
        g_uart3_dma_overruns++;
#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
        ++s_v2_rx_error_generation;
#endif
    }
    uart3_rx_boundary_queue[uart3_rx_boundary_head] = end_abs;
    uart3_rx_boundary_head = next;
    g_uart3_boundaries_queued++;
}

static void uart3_queue_current_ring_frame(void)
{
    uart3_frame_slot_t *slot;
    uint16_t length = 0U;

    if (uart3_rx_tail == uart3_rx_head) {
        return;
    }

    if (uart3_frame_queue_count >= UART3_FRAME_QUEUE_DEPTH) {
        g_uart3_frame_queue_overruns++;
#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
        ++s_v2_rx_error_generation;
#endif
        /* Drop the oldest completed frame, preserving the newest request. */
        uart3_frame_queue_tail = (uint8_t)((uart3_frame_queue_tail + 1U) %
                                           UART3_FRAME_QUEUE_DEPTH);
        uart3_frame_queue_count--;
    }

    slot = &uart3_frame_queue[uart3_frame_queue_head];
    while ((uart3_rx_tail != uart3_rx_head) &&
           (length < UART3_FRAME_MAX_SIZE)) {
        slot->data[length++] = uart3_rx_ring[uart3_rx_tail];
        uart3_rx_tail = (uint16_t)((uart3_rx_tail + 1U) & RB_MASK);
        g_uart3_rx_forwarded_bytes++;
    }

    /* Discard an overlong tail rather than allowing it to contaminate the
     * next RTU frame.  Normal Modbus requests are well below this limit. */
    while (uart3_rx_tail != uart3_rx_head) {
        uart3_rx_tail = (uint16_t)((uart3_rx_tail + 1U) & RB_MASK);
        g_uart3_rx_overruns++;
    }

    if (length != 0U) {
        slot->length = length;
        uart3_frame_queue_head = (uint8_t)((uart3_frame_queue_head + 1U) %
                                           UART3_FRAME_QUEUE_DEPTH);
        uart3_frame_queue_count++;
        g_uart3_frame_queue_pushed++;
        if (uart3_frame_queue_count > g_uart3_frame_queue_max_depth) {
            g_uart3_frame_queue_max_depth = uart3_frame_queue_count;
        }
    }
}

#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
static void v2_publish_passive_frames(void)
{
    /* Worker only: copies completed frame bytes, then publishes metadata.
     * A backpressure overflow poisons session trust. */
    while (uart3_frame_queue_count != 0U) {
        const uart3_frame_slot_t *from =
            &uart3_frame_queue[uart3_frame_queue_tail];
        uint32_t produced = s_v2_passive_produced;
        if ((uint32_t)(produced - s_v2_passive_consumed) >=
            PYXIS_V2_PASSIVE_SLOTS) {
            ++s_v2_rx_error_generation;
        } else {
            pyxis_v2_passive_slot_t *to =
                &s_v2_passive_queue[produced % PYXIS_V2_PASSIVE_SLOTS];
            to->length = from->length;
            memcpy(to->data, from->data, from->length);
            to->session_id = s_v2_session_id;
            to->error_generation = s_v2_rx_error_generation;
            ++s_v2_frame_sequence;
            if (s_v2_frame_sequence == 0U) ++s_v2_frame_sequence;
            to->frame_id = s_v2_frame_sequence;
            __DMB(); /* Publish data before the new producer cursor. */
            s_v2_passive_produced = produced + 1U;
        }
        uart3_frame_queue_tail =
            (uint8_t)((uart3_frame_queue_tail + 1U) %
                      UART3_FRAME_QUEUE_DEPTH);
        --uart3_frame_queue_count;
        ++g_uart3_frame_queue_popped;
    }
}
#endif

static void uart3_feed_queued_frames(void)
{
    while ((uart3_frame_queue_count != 0U) &&
           (ModbusEnginePort_FramePending() == false)) {
        uart3_frame_slot_t frame =
            uart3_frame_queue[uart3_frame_queue_tail];
        uart3_frame_queue_tail = (uint8_t)((uart3_frame_queue_tail + 1U) %
                                           UART3_FRAME_QUEUE_DEPTH);
        uart3_frame_queue_count--;
        g_uart3_frame_queue_popped++;

        for (uint16_t i = 0U; i < frame.length; ++i) {
            if (i == 0U) {
                g_modbus_rx_first_tick = HAL_GetTick();
            }
            g_modbus_rx_last_tick = HAL_GetTick();
            ModbusEnginePort_OnRxByte(frame.data[i]);
        }
        ModbusEnginePort_OnRxFrameBoundary();
    }
}

static BOOL uart3_recover_after_error(void)
{
    const uint8_t restart = uart3_rx_enabled;

    g_uart3_error_recovery_attempts++;
    g_uart3_last_recovery_status = (uint32_t)HAL_ERROR;

    /* Prevent callbacks from publishing more data while the old receive
     * transaction and its parser state are being discarded. */
    uart3_rx_enabled = 0U;
    __HAL_UART_DISABLE_IT(&huart3, UART_IT_IDLE | UART_IT_RTO);

    /* In the normal DMA-error path HAL has already aborted the channel.  The
     * conditional makes this safe if the callback was caused by a peripheral
     * error before the DMA abort completed. */
    if ((huart3.Instance->CR3 & USART_CR3_DMAR) != 0U) {
        (void)HAL_UART_AbortReceive(&huart3);
    }
    uart3_rx_dma_running = 0U;

    /* Drop every partial/stale byte and boundary.  A parity/framing-error
     * burst must never be concatenated with the first valid frame after the
     * master changes back to the correct UART format. */
    ModbusEnginePort_ClearFrame();
    uart3_rx_head = 0U;
    uart3_rx_tail = 0U;
    uart3_rx_dma_last_pos = 0U;
    uart3_rx_dma_harvest_abs = 0U;
    g_uart3_dma_producer_abs = 0U;
    g_uart3_dma_wrap_count = 0U;
    uart3_rx_boundary_head = 0U;
    uart3_rx_boundary_tail = 0U;
    uart3_frame_queue_head = 0U;
    uart3_frame_queue_tail = 0U;
    uart3_frame_queue_count = 0U;
    uart3_rto_pending = 0U;
    g_uart3_transport_flushes++;

    __HAL_UART_CLEAR_OREFLAG(&huart3);
    __HAL_UART_CLEAR_FEFLAG(&huart3);
    __HAL_UART_CLEAR_NEFLAG(&huart3);
    __HAL_UART_CLEAR_PEFLAG(&huart3);
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_RTOF);
    __HAL_UART_SEND_REQ(&huart3, UART_RXDATA_FLUSH_REQUEST);
    huart3.ErrorCode = HAL_UART_ERROR_NONE;

    if (restart != 0U) {
        uart3_rx_enabled = 1U;
        if (uart3_start_custom_dma() != TRUE) {
            uart3_rx_enabled = 0U;
            g_uart3_error_recovery_failures++;
            g_uart3_last_recovery_status = (uint32_t)HAL_ERROR;
            return FALSE;
        }
    }

    g_uart3_last_recovery_status = (uint32_t)HAL_OK;
    return TRUE;
}

static void custom_rx_task_entry(void *argument)
{
    (void)argument;

    for (;;) {
#if MODBUS_AUTOBAUD_V2_PORT_ENABLE
        /* Do not touch rings, queue or error recovery while a UART
         * transaction claimant is changing peripheral ownership. */
        if (s_v2_park_requested != 0U) {
            s_v2_worker_parked = 1U;
            (void)osDelay(1U);
            continue;
        }
        s_v2_worker_parked = 0U;
#endif
        /* Use a short timeout so a frame queued while the core is pending is
         * fed promptly after main_TaskModbus clears the previous frame. */
        (void)osEventFlagsWait(custom_rx_event, EVENT_CUSTOM_RX,
                               osFlagsWaitAny, 1U);
#if MODBUS_AUTOBAUD_V2_PORT_ENABLE
        /* Claim may have arrived while the worker was waiting. */
        if (s_v2_park_requested != 0U || s_v2_fatal != 0U) {
            continue;
        }
#endif

        if (uart3_rx_recovery_pending != 0U) {
            uart3_rx_recovery_pending = 0U;
            (void)uart3_recover_after_error();
        }

        while (uart3_rx_boundary_tail != uart3_rx_boundary_head) {
            uint32_t end_abs = uart3_rx_boundary_queue[uart3_rx_boundary_tail];
            uart3_rx_boundary_tail = (uint8_t)((uart3_rx_boundary_tail + 1U) &
                                               RX_BOUNDARY_QUEUE_MASK);
            uart3_harvest_dma_range(uart3_rx_dma_harvest_abs, end_abs);
            uart3_rx_dma_harvest_abs = end_abs;
            g_uart3_boundaries_harvested++;
            uart3_queue_current_ring_frame();
        }
        if (uart3_rto_pending != 0U) {
            uart3_rto_pending = 0U;
            uart3_queue_current_ring_frame();
        }
#if MODBUS_AUTOBAUD_V2_PORT_ENABLE
        /* Candidate traffic must NEVER reach the active Modbus engine.
         * A dedicated passive frame harvester is introduced in M6. */
        if (s_v2_passive == 0U) {
            uart3_feed_queued_frames();
        }
#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
        else {
            v2_publish_passive_frames();
        }
#endif
#else
        uart3_feed_queued_frames();
#endif
    }
}

bool ModbusEnginePort_UartInit(uint32_t baud_rate, uint8_t data_bits,
                               uint8_t parity_code)
{
    g_custom_uart_init_stage = 1U;
    g_custom_uart_hal_status = 0U;

    /* Fail closed if CubeMX has been regenerated with a different UART/DMA
     * assignment.  This keeps a board-port mistake from presenting as a
     * protocol or register-map failure. */
    if ((huart3.Instance != MODBUS_PYXIS_UART_INSTANCE) ||
        (huart3.hdmarx == NULL) ||
        (huart3.hdmarx->Instance != MODBUS_PYXIS_DMA_INSTANCE)) {
        g_custom_uart_hal_status = (uint32_t)HAL_ERROR;
        return false;
    }

    huart3.Init.BaudRate = baud_rate;
    /* Codes 0/1 include parity; codes 2/3 are no-parity formats. */
    huart3.Init.WordLength = (parity_code >= 2U) ?
        ((data_bits <= 8U) ? UART_WORDLENGTH_8B : UART_WORDLENGTH_9B) :
        UART_WORDLENGTH_9B;
    huart3.Init.StopBits = g_modbus_stop_bits;
    huart3.Init.Parity = (parity_code == 1U) ? UART_PARITY_ODD :
                         (parity_code == 0U) ? UART_PARITY_EVEN :
                                               UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;

    /* Custom engine uses software-controlled DE on the existing PB14 pin.
     * Use normal UART init, then take PB14 out of USART alternate function. */
    #if MODBUS_PYXIS_HW_DE
        g_custom_uart_hal_status =
            (uint32_t)HAL_RS485Ex_Init(&huart3,
                                    UART_DE_POLARITY_HIGH,
                                    1U,
                                    1U);
    #else
        g_custom_uart_hal_status =
            (uint32_t)HAL_UART_Init(&huart3);
    #endif

    if (g_custom_uart_hal_status != (uint32_t)HAL_OK) {
        return false;
    }
#if !MODBUS_PYXIS_HW_DE
#if defined(USART_CR3_DEM)
    CLEAR_BIT(huart3.Instance->CR3, USART_CR3_DEM);
#endif
    {
        GPIO_InitTypeDef de_gpio = {0};
        __HAL_RCC_GPIOB_CLK_ENABLE();
        de_gpio.Pin = MODBUS_PYXIS_DE_GPIO_PIN;
        de_gpio.Mode = GPIO_MODE_OUTPUT_PP;
        de_gpio.Pull = GPIO_NOPULL;
        de_gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(MODBUS_PYXIS_DE_GPIO_PORT, &de_gpio);
        ModbusEngineTx_SetDe(false);
    }
#endif
    ModbusEnginePort_SetRtuTiming(baud_rate, data_bits, parity_code,
                                  g_modbus_stop_bits);
#if MODBUS_PYXIS_HW_RTO
    {
        uint32_t bits_per_character = 1U + (uint32_t)data_bits +
            ((parity_code <= 1U) ? 1U : 0U) +
            ((g_modbus_stop_bits == UART_STOPBITS_2) ? 2U : 1U);
        HAL_UART_ReceiverTimeout_Config(&huart3, bits_per_character * 4U);
        if (HAL_UART_EnableReceiverTimeout(&huart3) != HAL_OK) {
            g_custom_uart_hal_status = (uint32_t)HAL_ERROR;
            return false;
        }
        __HAL_UART_ENABLE_IT(&huart3, UART_IT_RTO);
    }
#endif
    g_custom_uart_init_stage = 2U;
    (void)HAL_UARTEx_DisableFifoMode(&huart3);
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    __HAL_UART_CLEAR_FEFLAG(&huart3);
    __HAL_UART_CLEAR_NEFLAG(&huart3);
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    __HAL_UART_CLEAR_PEFLAG(&huart3);
    __HAL_UART_DISABLE_IT(&huart3, UART_IT_TXE | UART_IT_TC);
    uart3_rx_enabled = 0U;
    uart3_rx_dma_running = 0U;
    uart3_rx_dma_last_pos = 0U;
    uart3_rx_dma_harvest_abs = 0U;
    uart3_rx_boundary_head = 0U;
    uart3_rx_boundary_tail = 0U;
    uart3_frame_queue_head = 0U;
    uart3_frame_queue_tail = 0U;
    uart3_frame_queue_count = 0U;
    g_uart3_dma_producer_abs = 0U;
    g_uart3_dma_wrap_count = 0U;
    g_uart3_dma_position_errors = 0U;
    g_uart3_dma_overruns = 0U;
    g_uart3_rto_count = 0U;
    g_uart3_callbacks_while_disabled = 0U;
    g_uart3_boundaries_queued = 0U;
    g_uart3_boundaries_harvested = 0U;
    g_uart3_zero_length_events = 0U;
    g_uart3_boundary_flush_timeouts = 0U;
    g_uart3_frame_queue_pushed = 0U;
    g_uart3_frame_queue_popped = 0U;
    g_uart3_frame_queue_overruns = 0U;
    g_uart3_frame_queue_max_depth = 0U;
    g_modbus_rx_first_tick = 0U;
    g_modbus_rx_last_tick = 0U;
    g_uart3_dma_restart_tick = 0U;
    g_uart3_rx_callback_tick = 0U;
    g_uart3_last_disable_tick = 0U;
    g_uart3_last_enable_tick = 0U;
    uart3_rto_pending = 0U;
    uart3_rx_recovery_pending = 0U;
    uart3_rx_head = 0U;
    uart3_rx_tail = 0U;
    uart3_rx_overruns = 0U;

    custom_rx_event = osEventFlagsNew(NULL);
    if (custom_rx_event == NULL) {
        return false;
    }
    g_custom_uart_init_stage = 3U;

    custom_rx_task = osThreadNew(custom_rx_task_entry, NULL,
                                 &custom_rx_task_attr);
    if (custom_rx_task == NULL) {
        return false;
    }
    g_custom_uart_init_stage = 4U;
    return true;
}

bool ModbusEnginePort_UartEnable(bool enable)
{
    if (!enable) {
        uint32_t flush_start;

        g_uart3_last_disable_tick = HAL_GetTick();

        /* Stop accepting new RX events, then allow the worker to harvest
         * boundaries already queued by the ISR.  Resetting the queue first
         * loses complete requests during the TX ownership handoff. */
        uart3_rx_enabled = 0U;
        uart3_stop_custom_dma();
        __HAL_UART_DISABLE_IT(&huart3, UART_IT_IDLE | UART_IT_RTO);
        __HAL_UART_CLEAR_IDLEFLAG(&huart3);
#if MODBUS_PYXIS_HW_RTO
        __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_RTOF);
#endif

        flush_start = HAL_GetTick();
        while ((uart3_rx_boundary_tail != uart3_rx_boundary_head) &&
               ((HAL_GetTick() - flush_start) < 20U)) {
            (void)osDelay(1U);
        }
        if (uart3_rx_boundary_tail != uart3_rx_boundary_head) {
            g_uart3_boundary_flush_timeouts++;
        }

        uart3_rx_dma_last_pos = 0U;
        g_uart3_dma_producer_abs = 0U;
        g_uart3_dma_wrap_count = 0U;
        uart3_rx_dma_harvest_abs = 0U;
        uart3_rx_boundary_head = 0U;
        uart3_rx_boundary_tail = 0U;
        uart3_rto_pending = 0U;
        uart3_rx_recovery_pending = 0U;
        uart3_rx_head = 0U;
        uart3_rx_tail = 0U;
        return true;
    }

    g_uart3_last_enable_tick = HAL_GetTick();
    uart3_rx_enabled = 1U;
    uart3_rx_dma_last_pos = 0U;
    g_uart3_dma_producer_abs = 0U;
    g_uart3_dma_wrap_count = 0U;
    uart3_rx_dma_harvest_abs = 0U;
    uart3_rto_pending = 0U;
    uart3_rx_recovery_pending = 0U;
    uart3_rx_boundary_head = 0U;
    uart3_rx_boundary_tail = 0U;
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
#if MODBUS_PYXIS_HW_RTO
    __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_RTOF);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_RTO);
#endif
    return uart3_start_custom_dma() == TRUE;
}

static bool uart3_supported_config(uint32_t baud_rate, uint8_t data_bits,
                                   uint8_t parity_code, uint32_t stop_bits)
{
    return ((baud_rate == 9600U) || (baud_rate == 19200U) ||
            (baud_rate == 38400U) || (baud_rate == 57600U) ||
            (baud_rate == 115200U)) &&
           (data_bits == 8U) && (parity_code <= 3U) &&
           ((stop_bits == UART_STOPBITS_1) ||
            (stop_bits == UART_STOPBITS_2));
}

static bool uart3_apply_config(const UART_InitTypeDef *init,
                               uint32_t stop_bits)
{
    if (init == NULL) {
        return false;
    }

    huart3.Init = *init;
#if MODBUS_PYXIS_HW_DE
    if (HAL_RS485Ex_Init(&huart3, UART_DE_POLARITY_HIGH, 1U, 1U) != HAL_OK) {
        return false;
    }
#else
    if (HAL_UART_Init(&huart3) != HAL_OK) {
        return false;
    }
#if defined(USART_CR3_DEM)
    CLEAR_BIT(huart3.Instance->CR3, USART_CR3_DEM);
#endif
    {
        GPIO_InitTypeDef de_gpio = {0};
        __HAL_RCC_GPIOB_CLK_ENABLE();
        de_gpio.Pin = MODBUS_PYXIS_DE_GPIO_PIN;
        de_gpio.Mode = GPIO_MODE_OUTPUT_PP;
        de_gpio.Pull = GPIO_NOPULL;
        de_gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(MODBUS_PYXIS_DE_GPIO_PORT, &de_gpio);
        ModbusEngineTx_SetDe(false);
    }
#endif
    (void)HAL_UARTEx_DisableFifoMode(&huart3);
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    __HAL_UART_CLEAR_FEFLAG(&huart3);
    __HAL_UART_CLEAR_NEFLAG(&huart3);
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    __HAL_UART_CLEAR_PEFLAG(&huart3);
    __HAL_UART_DISABLE_IT(&huart3, UART_IT_TXE | UART_IT_TC);
#if MODBUS_PYXIS_HW_RTO
    {
        uint32_t bits = 1U + 8U +
            ((init->Parity == UART_PARITY_NONE) ? 0U : 1U) +
            ((stop_bits == UART_STOPBITS_2) ? 2U : 1U);
        HAL_UART_ReceiverTimeout_Config(&huart3, bits * 4U);
        if (HAL_UART_EnableReceiverTimeout(&huart3) != HAL_OK) {
            return false;
        }
        __HAL_UART_ENABLE_IT(&huart3, UART_IT_RTO);
    }
#endif
    g_modbus_stop_bits = stop_bits;
    ModbusEnginePort_SetRtuTiming(init->BaudRate, 8U,
                                  (init->Parity == UART_PARITY_EVEN) ? 0U :
                                  (init->Parity == UART_PARITY_ODD) ? 1U : 2U,
                                  stop_bits);
    return (huart3.Instance == MODBUS_PYXIS_UART_INSTANCE) &&
           (huart3.hdmarx != NULL) &&
           (huart3.hdmarx->Instance == MODBUS_PYXIS_DMA_INSTANCE);
}

bool ModbusEnginePort_GetUartConfig(uint32_t *baud_rate, uint8_t *data_bits,
                                    uint8_t *parity_code,
                                    uint32_t *stop_bits)
{
    if ((baud_rate == NULL) || (data_bits == NULL) ||
        (parity_code == NULL) || (stop_bits == NULL)) {
        return false;
    }
    *baud_rate = huart3.Init.BaudRate;
    *data_bits = (huart3.Init.Parity == UART_PARITY_NONE) ? 8U : 8U;
    *parity_code = (huart3.Init.Parity == UART_PARITY_EVEN) ? 0U :
                   (huart3.Init.Parity == UART_PARITY_ODD) ? 1U : 2U;
    *stop_bits = huart3.Init.StopBits;
    return true;
}

bool ModbusEnginePort_ReconfigureUart(uint32_t baud_rate, uint8_t data_bits,
                                      uint8_t parity_code,
                                      uint32_t stop_bits)
{
    UART_InitTypeDef previous;
    UART_InitTypeDef requested;
    uint8_t previous_data_bits;
    uint8_t previous_parity;
    uint32_t previous_stop_bits;
    bool disabled;
    bool applied = false;

    if (!uart3_supported_config(baud_rate, data_bits, parity_code, stop_bits) ||
        ModbusEngineTx_IsBusy()) {
        return false;
    }
    if (!ModbusEnginePort_GetUartConfig(&previous.BaudRate,
                                        &previous_data_bits,
                                        &previous_parity,
                                        &previous_stop_bits)) {
        return false;
    }
    (void)previous_data_bits;
    (void)previous_parity;
    previous = huart3.Init;
    previous_stop_bits = g_modbus_stop_bits;
    requested = previous;
    requested.BaudRate = baud_rate;
    requested.WordLength = (parity_code <= 1U) ? UART_WORDLENGTH_9B :
                           UART_WORDLENGTH_8B;
    requested.Parity = (parity_code == 0U) ? UART_PARITY_EVEN :
                       (parity_code == 1U) ? UART_PARITY_ODD :
                                             UART_PARITY_NONE;
    requested.StopBits = stop_bits;
    requested.Mode = UART_MODE_TX_RX;
    requested.HwFlowCtl = UART_HWCONTROL_NONE;
    requested.OverSampling = UART_OVERSAMPLING_16;

    ModbusEnginePort_ClearFrame();
    disabled = ModbusEnginePort_UartEnable(false);
    if (disabled) {
        (void)HAL_UART_DeInit(&huart3);
        applied = uart3_apply_config(&requested, stop_bits);
        if (applied) {
            applied = ModbusEnginePort_UartEnable(true);
        }
    }
    if (applied) {
        return true;
    }

    /* Transactional rollback.  Keep the worker/event objects untouched. */
    (void)ModbusEnginePort_UartEnable(false);
    (void)HAL_UART_DeInit(&huart3);
    if (uart3_apply_config(&previous, previous_stop_bits)) {
        return ModbusEnginePort_UartEnable(true);
    }
    return false;
}

/* The interrupt file references this symbol for the FreeModbus fallback. */
void prvvUARTTxReadyISR(void)
{
}

void prvvUARTRxISR(void)
{
}

void prvvUARTRxReceiveCharISR(CHAR data)
{
    uint16_t next = (uint16_t)((uart3_rx_head + 1U) & RB_MASK);

    if (next == uart3_rx_tail) {
        uart3_rx_tail = (uint16_t)((uart3_rx_tail + 1U) & RB_MASK);
        uart3_rx_overruns++;
    }

    uart3_rx_ring[uart3_rx_head] = (uint8_t)data;
    uart3_rx_head = next;
    if (custom_rx_event != NULL) {
        (void)osEventFlagsSet(custom_rx_event, EVENT_CUSTOM_RX);
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart_cb, uint16_t Size)
{
    HAL_UART_RxEventTypeTypeDef event_type;
    uint16_t pos;
    uint16_t start;
    uint16_t delta;

    if (huart_cb != &huart3) {
        return;
    }

    g_uart3_rx_callback_tick = HAL_GetTick();
    g_uart3_rx_callback_count++;
    g_uart3_last_rx_size = Size;
    if (uart3_rx_enabled == 0U) {
        g_uart3_callbacks_while_disabled++;
        return;
    }

    event_type = HAL_UARTEx_GetRxEventType(huart_cb);
    if (Size > UART3_RX_DMA_SIZE) {
        g_uart3_dma_position_errors++;
        pos = UART3_RX_DMA_SIZE;
    } else {
        pos = Size;
    }
    g_uart3_last_event_type = (uint32_t)event_type;

    /*
     * In circular DMA mode HT/TC are transfer events, not evidence of a
     * received Modbus frame.  Forwarding those fixed half-buffer ranges when
     * the line is idle feeds stale/zero bytes into the custom parser.  Only
     * the IDLE event carries the actual end position for a received burst.
     */
    if (event_type != HAL_UART_RXEVENT_IDLE) {
        return;
    }

    start = (uart3_rx_dma_last_pos == UART3_RX_DMA_SIZE) ?
        0U : uart3_rx_dma_last_pos;
    if ((pos == uart3_rx_dma_last_pos) && (pos != UART3_RX_DMA_SIZE)) {
        g_uart3_zero_length_events++;
        return;
    }

    if (pos >= start) {
        delta = (uint16_t)(pos - start);
    } else {
        delta = (uint16_t)(UART3_RX_DMA_SIZE - start + pos);
        g_uart3_dma_wrap_count++;
    }

    /* A callback at exactly DMA_SIZE after position zero represents one full
     * circular-buffer pass, not an empty range. */
    if ((pos == UART3_RX_DMA_SIZE) && (start == 0U)) {
        delta = UART3_RX_DMA_SIZE;
    }
    if (delta == 0U) {
        g_uart3_zero_length_events++;
        return;
    }

    g_uart3_dma_producer_abs += delta;
    uart3_rx_dma_last_pos = (pos == UART3_RX_DMA_SIZE) ? 0U : pos;
    uart3_queue_boundary(g_uart3_dma_producer_abs);
    uart3_rx_dma_running = 1U;
    if (custom_rx_event != NULL) {
        (void)osEventFlagsSet(custom_rx_event, EVENT_CUSTOM_RX);
    }
}

void ModbusEnginePort_RxTimeoutIsr(void)
{
    g_uart3_rto_count++;
    uart3_rto_pending++;
    if (custom_rx_event != NULL) {
        (void)osEventFlagsSet(custom_rx_event, EVENT_CUSTOM_RX);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart_cb)
{
    if (huart_cb != &huart3) {
        return;
    }

#if MODBUS_AUTOBAUD_V2_PORT_ENABLE
    ++s_v2_rx_error_generation;
#endif
    uart3_rx_dma_running = 0U;
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    __HAL_UART_CLEAR_FEFLAG(&huart3);
    __HAL_UART_CLEAR_NEFLAG(&huart3);
    __HAL_UART_CLEAR_PEFLAG(&huart3);
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    if (uart3_rx_enabled != 0U) {
        uart3_rx_recovery_pending = 1U;
        if (custom_rx_event != NULL) {
            (void)osEventFlagsSet(custom_rx_event, EVENT_CUSTOM_RX);
        }
    }
}

/* ---------- M4 opt-in USART3/GPDMA1 transactional board seam ----------
 * All calls are foreground-only and are invoked via Titania's reusable
 * autobaud_uart_transaction.c controller. The worker is parked before
 * changing any ring, UART or DMA ownership.
 * This seam is not active while MODBUS_AUTOBAUD_V2_PORT_ENABLE=0.
 */
#if MODBUS_AUTOBAUD_V2_PORT_ENABLE

static bool v2_pin_uart_verified(void)
{
    /* PC4 = USART3_RX alternate function 7. Physical readback matters:
     * successfully calling HAL_GPIO_Init is not proof of restored mux. */
    return ((GPIOC->MODER >> (4U * 2U)) & 0x3U) == 0x2U &&
           ((GPIOC->AFR[0] >> (4U * 4U)) & 0xFU) == 7U;
}

static bool v2_dma_enabled(void)
{
    if (huart3.hdmarx == NULL ||
        huart3.hdmarx->Instance != MODBUS_PYXIS_DMA_INSTANCE)
        return false;
#if defined(DMA_CCR_EN)
    return (huart3.hdmarx->Instance->CCR & DMA_CCR_EN) != 0U;
#elif defined(GPDMA_CCR_EN)
    return (huart3.hdmarx->Instance->CCR & GPDMA_CCR_EN) != 0U;
#else
    /* Fail closed rather than misreport a verified RX session when a
     * future STM32 header changes the physical DMA enable-bit name. */
    return false;
#endif
}

static void v2_drop_stale_rx(void)
{
    /* Called with worker parked and RX events disabled. */
    ModbusEnginePort_ClearFrame();
    uart3_rx_head = 0U;
    uart3_rx_tail = 0U;
    uart3_rx_dma_last_pos = 0U;
    uart3_rx_dma_harvest_abs = 0U;
    g_uart3_dma_producer_abs = 0U;
    g_uart3_dma_wrap_count = 0U;
    uart3_rx_boundary_head = 0U;
    uart3_rx_boundary_tail = 0U;
    uart3_frame_queue_head = 0U;
    uart3_frame_queue_tail = 0U;
    uart3_frame_queue_count = 0U;
    uart3_rto_pending = 0U;
    uart3_rx_recovery_pending = 0U;
#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
    /* Called only with worker fully parked and claimant holding UART. */
    s_v2_passive_produced = 0U;
    s_v2_passive_consumed = 0U;
    s_v2_last_consumed_frame = 0U;
#endif
    g_uart3_transport_flushes++;
}

static bool v2_profile_from_uart(autobaud_v2_profile_t *out)
{
    uint32_t baud = 0U;
    uint32_t stop = 0U;
    uint8_t bits = 0U;
    uint8_t parity = 0U;
    if (out == NULL ||
        !ModbusEnginePort_GetUartConfig(&baud, &bits, &parity, &stop) ||
        bits != 8U) return false;
    out->baud_rate = baud;
    if (parity == 0U && stop == UART_STOPBITS_1)
        out->framing = AUTOBAUD_UART_MODE_8E1;
    else if (parity == 1U && stop == UART_STOPBITS_1)
        out->framing = AUTOBAUD_UART_MODE_8O1;
    else if (parity >= 2U && stop == UART_STOPBITS_2)
        out->framing = AUTOBAUD_UART_MODE_8N2;
    else if (parity >= 2U && stop == UART_STOPBITS_1)
        out->framing = AUTOBAUD_UART_MODE_8N1;
    else return false;
    return autobaud_v2_profile_supported(out);
}

static bool v2_uart_registers_match(const autobaud_v2_profile_t *profile)
{
    autobaud_v2_profile_t active = {0};
    bool parity_present;
    bool odd;
    bool nine_bits;
    uint32_t stop;
    uint32_t cr1;
    uint32_t cr2;
    if (!autobaud_v2_profile_supported(profile) ||
        !v2_profile_from_uart(&active) ||
        active.baud_rate != profile->baud_rate ||
        active.framing != profile->framing ||
        huart3.Instance != MODBUS_PYXIS_UART_INSTANCE ||
        !v2_pin_uart_verified()) return false;
    cr1 = huart3.Instance->CR1;
    cr2 = huart3.Instance->CR2;
    parity_present = profile->framing == AUTOBAUD_UART_MODE_8E1 ||
                     profile->framing == AUTOBAUD_UART_MODE_8O1;
    odd = profile->framing == AUTOBAUD_UART_MODE_8O1;
    nine_bits = parity_present;
    stop = profile->framing == AUTOBAUD_UART_MODE_8N2 ?
           UART_STOPBITS_2 : UART_STOPBITS_1;
    if ((cr1 & (USART_CR1_UE | USART_CR1_RE | USART_CR1_TE)) !=
          (USART_CR1_UE | USART_CR1_RE | USART_CR1_TE) ||
        ((cr1 & USART_CR1_PCE) != 0U) != parity_present ||
        ((cr1 & USART_CR1_PS) != 0U) != odd ||
        ((cr1 & USART_CR1_M0) != 0U) != nine_bits ||
        (cr1 & USART_CR1_M1) != 0U ||
        (cr2 & USART_CR2_STOP) != stop ||
        huart3.Instance->BRR == 0U) return false;
#if !MODBUS_PYXIS_HW_DE
#ifdef USART_CR3_DEM
    if ((huart3.Instance->CR3 & USART_CR3_DEM) != 0U) return false;
#endif
#endif
    return true;
}

static bool v2_de_is_receive(void)
{
    if (ModbusEngineTx_IsBusy() ||
        __HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC) != SET)
        return false;
#if !MODBUS_PYXIS_HW_DE
    if (HAL_GPIO_ReadPin(MODBUS_PYXIS_DE_GPIO_PORT,
                         MODBUS_PYXIS_DE_GPIO_PIN) != GPIO_PIN_RESET)
        return false;
#endif
    return true;
}

bool ModbusEngineV2_Claim(void)
{
    uint32_t start;
    if (s_v2_fatal != 0U || s_v2_claimed != 0U ||
        s_v2_worker_parked != 0U ||
        custom_rx_task == NULL || custom_rx_event == NULL ||
        osThreadGetId() == custom_rx_task ||
        !v2_de_is_receive() ||
        huart3.Instance != MODBUS_PYXIS_UART_INSTANCE ||
        huart3.hdmarx == NULL ||
        huart3.hdmarx->Instance != MODBUS_PYXIS_DMA_INSTANCE)
        return false;

    s_v2_claimed = 1U;
    s_v2_park_requested = 1U;
    (void)osEventFlagsSet(custom_rx_event, EVENT_CUSTOM_RX);
    start = HAL_GetTick();
    while (s_v2_worker_parked == 0U &&
           (uint32_t)(HAL_GetTick() - start) <
               PYXIS_V2_WORKER_PARK_TIMEOUT_MS)
        (void)osDelay(1U);

    if (s_v2_worker_parked == 0U || !v2_de_is_receive()) {
        s_v2_park_requested = 0U;
        s_v2_claimed = 0U;
        return false; /* BUSY: no physical configuration changed. */
    }
    s_v2_restart_ready = 0U;
    return true;
}

bool ModbusEngineV2_Snapshot(autobaud_v2_profile_t *profile)
{
    return s_v2_fatal == 0U &&
           v2_de_is_receive() &&
           v2_profile_from_uart(profile) &&
           v2_uart_registers_match(profile);
}

bool ModbusEngineV2_PreviousPassive(void)
{
    return s_v2_passive != 0U;
}

bool ModbusEngineV2_Quiesce(void)
{
    bool ok = true;
    if (s_v2_claimed == 0U || s_v2_worker_parked == 0U ||
        !v2_de_is_receive()) return false;
    uart3_rx_enabled = 0U;
    __HAL_UART_DISABLE_IT(&huart3, UART_IT_IDLE | UART_IT_RTO);
    if (uart3_rx_dma_running != 0U ||
        (huart3.Instance->CR3 & USART_CR3_DMAR) != 0U) {
        ok = HAL_UART_AbortReceive(&huart3) == HAL_OK;
    }
    uart3_rx_dma_running = 0U;
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    __HAL_UART_CLEAR_FEFLAG(&huart3);
    __HAL_UART_CLEAR_NEFLAG(&huart3);
    __HAL_UART_CLEAR_PEFLAG(&huart3);
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_RTOF);
    __HAL_UART_SEND_REQ(&huart3, UART_RXDATA_FLUSH_REQUEST);
    v2_drop_stale_rx();
    s_v2_restart_ready = 0U;
    return ok && (huart3.Instance->CR3 & USART_CR3_DMAR) == 0U &&
           !v2_dma_enabled();
}

bool ModbusEngineV2_Apply(const autobaud_v2_profile_t *profile)
{
    UART_InitTypeDef next;
    uint32_t stop = UART_STOPBITS_1;
    if (s_v2_claimed == 0U || s_v2_worker_parked == 0U ||
        s_v2_restart_ready != 0U || !autobaud_v2_profile_supported(profile) ||
        (huart3.Instance->CR3 & USART_CR3_DMAR) != 0U ||
        v2_dma_enabled()) return false;

    next = huart3.Init;
    next.BaudRate = profile->baud_rate;
    next.WordLength = UART_WORDLENGTH_8B;
    next.Parity = UART_PARITY_NONE;
    if (profile->framing == AUTOBAUD_UART_MODE_8E1 ||
        profile->framing == AUTOBAUD_UART_MODE_8O1) {
        next.WordLength = UART_WORDLENGTH_9B;
        next.Parity = profile->framing == AUTOBAUD_UART_MODE_8E1 ?
                      UART_PARITY_EVEN : UART_PARITY_ODD;
    }
    if (profile->framing == AUTOBAUD_UART_MODE_8N2)
        stop = UART_STOPBITS_2;
    next.StopBits = stop;

    if (HAL_UART_DeInit(&huart3) != HAL_OK) return false;
    if (!uart3_apply_config(&next, stop)) return false;
    return v2_uart_registers_match(profile);
}

bool ModbusEngineV2_ApplyTiming(const autobaud_v2_profile_t *profile)
{
    uint32_t stop;
    uint8_t parity_code;
    if (s_v2_claimed == 0U || s_v2_worker_parked == 0U ||
        !v2_uart_registers_match(profile)) return false;
    parity_code = profile->framing == AUTOBAUD_UART_MODE_8E1 ? 0U :
                  profile->framing == AUTOBAUD_UART_MODE_8O1 ? 1U :
                  profile->framing == AUTOBAUD_UART_MODE_8N2 ? 2U : 3U;
    stop = profile->framing == AUTOBAUD_UART_MODE_8N2 ?
           UART_STOPBITS_2 : UART_STOPBITS_1;
    ModbusEnginePort_SetRtuTiming(profile->baud_rate, 8U, parity_code, stop);
    return g_modbus_stop_bits == stop;
}

bool ModbusEngineV2_Restart(bool passive)
{
    if (s_v2_claimed == 0U || s_v2_worker_parked == 0U ||
        (huart3.Instance->CR3 & USART_CR3_DMAR) != 0U ||
        v2_dma_enabled() || !v2_pin_uart_verified()) return false;

    v2_drop_stale_rx();
    huart3.ErrorCode = HAL_UART_ERROR_NONE;
    uart3_rx_enabled = 1U;
    if (uart3_start_custom_dma() != TRUE) {
        uart3_rx_enabled = 0U;
        return false;
    }
    s_v2_expected_passive = passive ? 1U : 0U;
    s_v2_restart_ready = 1U;
    return true;
}

bool ModbusEngineV2_Verify(const autobaud_v2_profile_t *profile,
                            bool passive)
{
    return s_v2_claimed != 0U && s_v2_worker_parked != 0U &&
           s_v2_restart_ready != 0U &&
           s_v2_expected_passive == (passive ? 1U : 0U) &&
           uart3_rx_enabled != 0U && uart3_rx_dma_running != 0U &&
           (huart3.Instance->CR3 & USART_CR3_DMAR) != 0U &&
           v2_dma_enabled() &&
           huart3.ErrorCode == HAL_UART_ERROR_NONE &&
           v2_uart_registers_match(profile) &&
           v2_de_is_receive();
}

void ModbusEngineV2_Release(bool fatal, bool passive)
{
    if (fatal) {
        s_v2_fatal = 1U;
        s_v2_passive = 1U;
        uart3_rx_enabled = 0U;
        __HAL_UART_DISABLE_IT(&huart3, UART_IT_IDLE | UART_IT_RTO);
        uart3_stop_custom_dma();
        ModbusEngineTx_SetDe(false);
        /* Retain worker park request while latched; do not deliver frames
         * through the regular Modbus processing queue. */
        s_v2_park_requested = 1U;
    } else if (s_v2_claimed != 0U && s_v2_restart_ready != 0U) {
        s_v2_passive = passive ? 1U : 0U;
        ++s_v2_session_id;
        if (s_v2_session_id == 0U) ++s_v2_session_id;
#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
        s_v2_session_error_baseline = s_v2_rx_error_generation;
#endif
        s_v2_park_requested = 0U;
    } else {
        /* A claim was released without independently verified restart.
         * Never reopen normal Modbus RX after an unknown UART state. */
        s_v2_fatal = 1U;
        s_v2_passive = 1U;
        s_v2_park_requested = 1U;
        uart3_rx_enabled = 0U;
    }
    s_v2_restart_ready = 0U;
    s_v2_claimed = 0U;
}

bool ModbusEngineV2_IsFatal(void)
{
    return s_v2_fatal != 0U;
}

bool ModbusEngineV2_IsPassive(void)
{
    return s_v2_passive != 0U;
}

bool ModbusEngineV2_TxAllowed(void)
{
    return s_v2_fatal == 0U && s_v2_claimed == 0U &&
           s_v2_passive == 0U && s_v2_park_requested == 0U;
}

bool ModbusEngineV2_SnapshotSession(uint32_t *session_id,
                                    uint32_t *error_generation)
{
    if (session_id == NULL || error_generation == NULL ||
        s_v2_fatal != 0U || s_v2_claimed != 0U ||
        s_v2_passive == 0U || s_v2_session_id == 0U ||
        uart3_rx_enabled == 0U || uart3_rx_dma_running == 0U ||
        !v2_dma_enabled()) return false;
    *session_id = s_v2_session_id;
    *error_generation = s_v2_rx_error_generation;
    return true;
}

#if MODBUS_AUTOBAUD_V2_FRAMING_ENABLE
/* These methods run only from the future M7 Modbus task. The RX worker is
 * the sole queue producer; this path never calls the Modbus request runtime. */
bool ModbusEngineV2_RxActivity(uint32_t *total_bytes, uint32_t *total_errors)
{
    if (total_bytes == NULL || total_errors == NULL ||
        s_v2_passive == 0U || s_v2_fatal != 0U ||
        s_v2_claimed != 0U || s_v2_session_id == 0U)
        return false;
    *total_bytes = g_uart3_dma_producer_abs;
    *total_errors = s_v2_rx_error_generation;
    return true;
}

autobaud_e_poll_t ModbusEngineV2_TakePassiveFrame(
    uint8_t *out, uint16_t capacity, uint16_t *length,
    autobaud_e_frame_meta_t *meta)
{
    uint32_t tail;
    pyxis_v2_passive_slot_t *slot;
    if (out == NULL || length == NULL || meta == NULL ||
        capacity < AUTOBAUD_VALIDATE_MIN_ADU_LENGTH)
        return AUTOBAUD_E_POLL_RX_FAULT;
    *length = 0U;
    *meta = (autobaud_e_frame_meta_t){0};
    if (s_v2_fatal != 0U || s_v2_passive == 0U ||
        s_v2_claimed != 0U || s_v2_park_requested != 0U ||
        s_v2_session_id == 0U || uart3_rx_enabled == 0U ||
        uart3_rx_dma_running == 0U ||
        (huart3.Instance->CR3 & USART_CR3_DMAR) == 0U ||
        !v2_dma_enabled() ||
        huart3.ErrorCode != HAL_UART_ERROR_NONE ||
        uart3_rx_recovery_pending != 0U ||
        s_v2_session_error_baseline != s_v2_rx_error_generation)
        return AUTOBAUD_E_POLL_RX_FAULT;
    tail = s_v2_passive_consumed;
    if (tail == s_v2_passive_produced) return AUTOBAUD_E_POLL_NONE;
    /* The RTU receiver runs on a FreeRTOS worker. Waiting for 5ms of
     * observed line silence prevents a UART 1-character IDLE boundary from
     * immediately being misclassified as a complete RTU frame. */
    if ((uint32_t)(HAL_GetTick() - g_uart3_rx_callback_tick) <
        PYXIS_V2_FRAME_QUIET_MS)
        return AUTOBAUD_E_POLL_NONE;
    __DMB(); /* Observe payload after the producer cursor. */
    slot = &s_v2_passive_queue[tail % PYXIS_V2_PASSIVE_SLOTS];
    if (slot->length > capacity ||
        slot->length < AUTOBAUD_VALIDATE_MIN_ADU_LENGTH ||
        slot->length > AUTOBAUD_VALIDATE_MAX_ADU_LENGTH ||
        slot->session_id != s_v2_session_id ||
        slot->error_generation != s_v2_rx_error_generation ||
        slot->frame_id == 0U) {
        __DMB();
        s_v2_passive_consumed = tail + 1U;
        return AUTOBAUD_E_POLL_BAD_FRAME;
    }
    memcpy(out, slot->data, slot->length);
    *length = slot->length;
    meta->session_id = slot->session_id;
    meta->frame_id = slot->frame_id;
    meta->uart_error_generation = slot->error_generation;
    meta->evidence.complete_rtu_frame = true;
    meta->evidence.uart_error_free = true;
    meta->evidence.transport_error_free = true;
    meta->evidence.fresh_after_profile_apply = true;
    __DMB();
    s_v2_passive_consumed = tail + 1U;
    s_v2_last_consumed_frame = slot->frame_id;
    return AUTOBAUD_E_POLL_FRAME;
}

bool ModbusEngineV2_VerifyPassiveFrame(
    const autobaud_v2_profile_t *profile, uint32_t session_id,
    uint32_t error_generation)
{
    return s_v2_fatal == 0U && s_v2_claimed == 0U &&
           s_v2_passive != 0U && s_v2_park_requested == 0U &&
           session_id != 0U && session_id == s_v2_session_id &&
           s_v2_session_error_baseline == error_generation &&
           error_generation == s_v2_rx_error_generation &&
           uart3_rx_recovery_pending == 0U &&
           uart3_rx_enabled != 0U && uart3_rx_dma_running != 0U &&
           huart3.ErrorCode == HAL_UART_ERROR_NONE &&
           (huart3.Instance->CR3 & USART_CR3_DMAR) != 0U &&
           v2_dma_enabled() && v2_de_is_receive() &&
           v2_uart_registers_match(profile);
}

bool ModbusEngineV2_AcceptPassiveFrame(
    const autobaud_v2_profile_t *profile,
    const autobaud_e_frame_meta_t *meta)
{
    bool valid;
    if (profile == NULL || meta == NULL ||
        !autobaud_e_evidence_trustworthy(meta, meta->session_id) ||
        !ModbusEngineV2_VerifyPassiveFrame(
            profile, meta->session_id, meta->uart_error_generation) ||
        meta->frame_id != s_v2_last_consumed_frame)
        return false;
    if (!ModbusEngineV2_Claim()) return false; /* Busy, no UART mutation. */
    /* The worker is now parked: recheck the exact immutable RX token,
     * error generation and hardware UART profile before committing. */
    valid = s_v2_passive != 0U &&
            s_v2_worker_parked != 0U &&
            meta->session_id == s_v2_session_id &&
            meta->frame_id == s_v2_last_consumed_frame &&
            meta->uart_error_generation == s_v2_rx_error_generation &&
            s_v2_session_error_baseline == s_v2_rx_error_generation &&
            v2_uart_registers_match(profile) && v2_de_is_receive();
    if (!valid) {
        /* No physical UART state changed, release the claim without
         * restarting or reprocessing any evidence frame. */
        s_v2_park_requested = 0U;
        s_v2_claimed = 0U;
        return false;
    }
    /* Fully discard the detection frame and all intervening RX, then
     * start a NEW normal RX session at the same verified profile.
     * Even a valid FC06/FC16 detection request cannot be executed. */
    if (!ModbusEngineV2_Quiesce() ||
        !ModbusEngineV2_Restart(false) ||
        !ModbusEngineV2_Verify(profile, false)) {
        ModbusEngineV2_Release(true, false); /* Unknown RX -> fatal. */
        return false;
    }
    ModbusEngineV2_Release(false, false);
    return !ModbusEngineV2_IsFatal();
}

void ModbusEngineV2_LatchFatal(void)
{
    ModbusEngineV2_Release(true, false);
}
#endif /* MODBUS_AUTOBAUD_V2_FRAMING_ENABLE */

#endif /* MODBUS_AUTOBAUD_V2_PORT_ENABLE */


#else /* MODBUS_ENGINE_ACTIVE == 0: FreeModbus transport */

/* ----------------------- Config -------------------------------------------*/
#define RB_SIZE                 512u                /* power of two */
#define RB_MASK                 (RB_SIZE - 1u)
#define UART3_RX_DMA_SIZE       256u
#define UART3_RX_DMA_HALF_SIZE  (UART3_RX_DMA_SIZE / 2u)
#define EVENT_MBSLAVE_HANDLE_RX (1u << 0)

#define RS485_SLAVE_TX_MODE()   //LL_GPIO_SetOutputPin(RS485_SLAVE_CTRL_GPIO_Port, RS485_SLAVE_CTRL_Pin)
#define RS485_SLAVE_RX_MODE()   //LL_GPIO_ResetOutputPin(RS485_SLAVE_CTRL_GPIO_Port, RS485_SLAVE_CTRL_Pin)

extern UART_HandleTypeDef 		huart3;
#define huart					huart3
extern uint32_t g_modbus_stop_bits;

static volatile uint8_t  		rx_buf[RB_SIZE];
static volatile uint16_t 		rx_head = 0;                  /* ISR writes */
static volatile uint16_t 		rx_tail = 0;                  /* task reads */
static volatile uint32_t 		rx_overruns = 0;
static volatile uint8_t         uart3_rx_enabled = 0u;
static volatile uint8_t         uart3_rx_dma_running = 0u;
static uint8_t                  uart3_rx_dma_buf[UART3_RX_DMA_SIZE];
static volatile uint16_t        uart3_rx_dma_last_pos = 0u;
static const osThreadAttr_t     serial_rx_task_attr = {
    .name = "mbRxDrain",
    .priority = (osPriority_t)osPriorityAboveNormal4,
    .stack_size = 1024u
};

static osThreadId_t 			receiveDataHandle;
static osEventFlagsId_t 		xSerialEventHandle;

/* Optional receive-only hook for the custom Modbus engine. */
__attribute__((weak)) void ModbusEnginePort_OnRxByte(uint8_t byte)
{
    (void)byte;
}


/* ----------------------- Static variables ---------------------------------*/
//static volatile uint8_t rx_buff[256];
//static int rx_index;
//static int put_index;


/* ----------------------- Defines ------------------------------------------*/
//#define EVENT_MBSLAVE_HANDLE_RECEIVED_DATA  0x00000001UL

/* ----------------------- static functions ---------------------------------*/
static void handleReceivedDataTask(void *argument);
static BOOL uart3_start_rx_dma(void);
static void uart3_stop_rx_dma(void);
static void uart3_handle_rx_range(uint16_t start, uint16_t end, BOOL notify);
void prvvUARTRxISR(void);

/* Weak hook to wake your Modbus task in a different way if desired */
__attribute__((weak)) void UART3_RB_NotifyBytesAvailableISR(void)
{
    /* Default: event flag to the local handler task */
    if (xSerialEventHandle) {
        (void)osEventFlagsSet(xSerialEventHandle, EVENT_MBSLAVE_HANDLE_RX);
    }
}

/* ----------------------- Ring helpers -------------------------------------*/
static inline uint16_t rb_count(void)
{
    return (uint16_t)((rx_head - rx_tail) & RB_MASK);
}

static inline void rb_push_isr(uint8_t b)
{
    uint16_t next = (uint16_t)((rx_head + 1u) & RB_MASK);
    if (next == rx_tail) {                         /* overflow: drop oldest */
        rx_tail = (uint16_t)((rx_tail + 1u) & RB_MASK);
        rx_overruns++;
    }
    rx_buf[rx_head] = b;
    rx_head = next;
}

static void uart3_handle_rx_range(uint16_t start, uint16_t end, BOOL notify)
{
    uint16_t pos = start;

    while (pos != end) {
        uint8_t byte = uart3_rx_dma_buf[pos];
        ModbusEnginePort_OnRxByte(byte);
        rb_push_isr(byte);
        pos = (uint16_t)((pos + 1u) % UART3_RX_DMA_SIZE);
    }

    if ((notify != FALSE) && (start != end)) {
        prvvUARTRxISR();
    }
}

static BOOL uart3_start_rx_dma(void)
{
    HAL_StatusTypeDef status;

    if (uart3_rx_dma_running != 0u) {
        return TRUE;
    }

    status = HAL_UARTEx_ReceiveToIdle_DMA(&huart, uart3_rx_dma_buf, UART3_RX_DMA_SIZE);
    if (status != HAL_OK) {
        return FALSE;
    }

    uart3_rx_dma_running = 1u;
    return TRUE;
}

static void uart3_stop_rx_dma(void)
{
    if (uart3_rx_dma_running == 0u) {
        return;
    }

    (void)HAL_UART_AbortReceive(&huart);
    uart3_rx_dma_running = 0u;
}

/* ----------------------- Start implementation -----------------------------*/
BOOL xMBPortSerialInit(UCHAR ucPORT, ULONG ulBaudRate, UCHAR ucDataBits,
        eMBParity eParity)
{
	(void)ucPORT;

    /* set serial configure parameter */
    huart.Init.BaudRate 	= ulBaudRate;
    if (eParity == MB_PAR_NONE) {
        huart.Init.WordLength = (ucDataBits <= 8u) ? UART_WORDLENGTH_8B : UART_WORDLENGTH_9B;
    } else {
        /* STM32 word length includes parity bit: 8 data bits + parity => 9-bit word length. */
        huart.Init.WordLength = UART_WORDLENGTH_9B;
    }
    huart.Init.StopBits 	= g_modbus_stop_bits;
    switch (eParity)
    {
        case MB_PAR_NONE:	huart.Init.Parity = UART_PARITY_NONE;	break;
        case MB_PAR_ODD:	huart.Init.Parity = UART_PARITY_ODD;	break;
        case MB_PAR_EVEN:	huart.Init.Parity = UART_PARITY_EVEN;	break;
        default:			return FALSE;
    }

    huart.Init.Mode 		= UART_MODE_TX_RX;
    huart.Init.HwFlowCtl 	= UART_HWCONTROL_NONE;
    huart.Init.OverSampling	= UART_OVERSAMPLING_16;

    /* RS-485 Auto-DE: polarity HIGH for SP3485; set small pre/post times (in bit-times, 0..31). */
    const uint32_t de_assert_bt   = 1;   // ~1 bit-time before first start bit
    const uint32_t de_deassert_bt = 1;   // ~1 bit-time after last stop bit
    if (HAL_RS485Ex_Init(&huart, UART_DE_POLARITY_HIGH, de_assert_bt, de_deassert_bt) != HAL_OK)
        return FALSE;

    /* Classic RXNE per-byte behavior keeps the ISR simple */
    (void)HAL_UARTEx_DisableFifoMode(&huart);

    /* Clear pending flags before enabling runtime RX path */
    __HAL_UART_CLEAR_OREFLAG(&huart);
    __HAL_UART_CLEAR_FEFLAG(&huart);
    __HAL_UART_CLEAR_NEFLAG(&huart);
    __HAL_UART_CLEAR_IDLEFLAG(&huart);
    __HAL_UART_CLEAR_PEFLAG(&huart);

    uart3_rx_enabled = 0u;
    uart3_rx_dma_running = 0u;
    uart3_rx_dma_last_pos = 0u;
    rx_head = 0u;
    rx_tail = 0u;

    /* DO NOT enable TC here; we use TXE to drive the byte pump */
    __HAL_UART_DISABLE_IT(&huart, UART_IT_TC);
    __HAL_UART_DISABLE_IT(&huart, UART_IT_TXE);

    /* Create event + worker task (optional; you can feed your own Modbus task) */
    xSerialEventHandle = osEventFlagsNew(NULL);
    if (xSerialEventHandle == NULL) return FALSE;

    receiveDataHandle = osThreadNew(handleReceivedDataTask, NULL, &serial_rx_task_attr);
    if (receiveDataHandle == NULL) return FALSE;

    return TRUE;
}

void vMBPortSerialEnable(BOOL xRxEnable, BOOL xTxEnable)
{
    if (xRxEnable) {
        RS485_SLAVE_RX_MODE();
        uart3_rx_enabled = 1u;
        uart3_rx_dma_last_pos = 0u;
        (void)uart3_start_rx_dma();
    } else {
        uart3_rx_enabled = 0u;
        uart3_stop_rx_dma();
        uart3_rx_dma_last_pos = 0u;
    }

    if (xTxEnable) {
        RS485_SLAVE_TX_MODE();
        __HAL_UART_DISABLE_IT(&huart3, UART_IT_TXE | UART_IT_TC);

        /* Kick the TX state machine once */
        pxMBFrameCBTransmitterEmpty();

        __HAL_UART_ENABLE_IT(&huart, UART_IT_TXE);   /* enable "TX empty" IRQ */
        //__HAL_UART_CLEAR_FLAG(&huart, UART_FLAG_TXE); // clear stale
    } else {
        __HAL_UART_DISABLE_IT(&huart, UART_IT_TXE);
        __HAL_UART_DISABLE_IT(&huart, UART_IT_TC);
    }
}

void vMBPortClose(void)
{
    uart3_rx_enabled = 0u;
    uart3_stop_rx_dma();
    uart3_rx_dma_last_pos = 0u;
    __HAL_UART_DISABLE_IT(&huart, UART_IT_TXE|UART_IT_TC);
    (void)HAL_UART_DeInit(&huart);
}

BOOL xMBPortSerialPutByte(CHAR ucByte)
{
    /* Non-blocking: write a byte to TDR, TXE interrupt drives subsequent bytes */
    huart.Instance->TDR = (uint8_t)ucByte;
    return TRUE;
}

/*
 * A Function to send all bytes in one call.
 */
BOOL xMBPortSerialPutBytes(volatile UCHAR *ucByte, USHORT usSize)
{
    while (usSize--) {
        huart.Instance->TDR = *ucByte++;
        /* Let TXE IRQ clock further bytes via pxMBFrameCBTransmitterEmpty() */
        /* If you want to throttle, you can wait for TXE here—but not required. */
    }
    return TRUE;
}

BOOL xMBPortSerialGetByte(CHAR * pucByte)
{
    /* Pop one byte; FreeModbus calls this only after pxMBFrameCBByteReceived() */
    if (rx_head == rx_tail) return FALSE;                 /* should not happen */
    *pucByte = (CHAR)rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1u) & RB_MASK);
    return TRUE;
}

/*
 * Create an interrupt handler for the transmit buffer empty interrupt
 * (or an equivalent) for your target processor. This function should then
 * call pxMBFrameCBTransmitterEmpty( ) which tells the protocol stack that
 * a new character can be sent. The protocol stack will then call
 * xMBPortSerialPutByte( ) to send the character.
 */
void prvvUARTTxReadyISR(void)
{
	pxMBFrameCBTransmitterEmpty();
}

/*
 * Create an interrupt handler for the receive interrupt for your target
 * processor. This function should then call pxMBFrameCBByteReceived( ). The
 * protocol stack will then call xMBPortSerialGetByte( ) to retrieve the
 * character.
 *
 * quanghona: This function is called when IDLE is detected. thus will then
 * trigger the received message handler.
 */
void prvvUARTRxISR(void)
{
	UART3_RB_NotifyBytesAvailableISR();
}

/*
 * Create an interrupt handler for the receive character for your target processor.
 * This function should store data to a buffer for received message thread to
 * handle later.
 */
void prvvUARTRxReceiveCharISR(CHAR data)
{
	rb_push_isr((uint8_t)data);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart_cb, uint16_t Size)
{
    HAL_UART_RxEventTypeTypeDef event_type;
    uint16_t pos;

    if (huart_cb != &huart) {
        return;
    }

    event_type = HAL_UARTEx_GetRxEventType(huart_cb);
    pos = Size;
    if (pos > UART3_RX_DMA_SIZE) {
        pos = UART3_RX_DMA_SIZE;
    }

    if (event_type == HAL_UART_RXEVENT_HT) {
        pos = UART3_RX_DMA_HALF_SIZE;
    } else if (event_type == HAL_UART_RXEVENT_TC) {
        pos = 0u;
    }

    uart3_handle_rx_range(uart3_rx_dma_last_pos, pos, TRUE);
    uart3_rx_dma_last_pos = pos;
    uart3_rx_dma_running = 1u;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart_cb)
{
    if (huart_cb != &huart) {
        return;
    }

    uart3_rx_dma_running = 0u;
    uart3_rx_dma_last_pos = 0u;

    __HAL_UART_CLEAR_OREFLAG(&huart);
    __HAL_UART_CLEAR_FEFLAG(&huart);
    __HAL_UART_CLEAR_NEFLAG(&huart);
    __HAL_UART_CLEAR_PEFLAG(&huart);
    __HAL_UART_CLEAR_IDLEFLAG(&huart);

    if (uart3_rx_enabled != 0u) {
        (void)uart3_start_rx_dma();
    }
}

/*
 * A thread to handle the received message.
 * This thread is wait until a received message flag is set, then it will call
 * pxMBFrameCBByteReceived() until all characters in the receive buffer is handled.
 */
static void handleReceivedDataTask(void *argument)
{
    (void)argument;
    for (;;) {
        (void)osEventFlagsWait(xSerialEventHandle, EVENT_MBSLAVE_HANDLE_RX,
                               osFlagsWaitAny, osWaitForever);
        /* Drain everything we currently have: one FSM tick per byte */
        while (rb_count() > 0u) {
            pxMBFrameCBByteReceived();
        }
    }
}

#endif /* MODBUS_ENGINE_ACTIVE */
