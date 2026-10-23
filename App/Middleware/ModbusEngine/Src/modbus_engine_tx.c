#include "modbus_engine_tx.h"

#include "main.h"
#include "cmsis_os2.h"
#include "stm32h5xx_hal.h"
#include "modbus_engine_config.h"
#include "modbus_engine_hw_config.h"

extern UART_HandleTypeDef huart3;
extern volatile uint32_t g_modbus_frame_boundary_tick;

static volatile bool tx_busy;
static volatile uint32_t tx_errors;
static volatile bool de_active;
#if MODBUS_PYXIS_TX_IT
static osEventFlagsId_t tx_event;
#define TX_COMPLETE_EVENT (1U << 0)
#endif
volatile uint32_t g_modbus_tx_complete_callbacks;
volatile uint32_t g_modbus_tx_wait_timeouts;
volatile uint32_t g_modbus_tx_attempts;
volatile uint32_t g_modbus_tx_errors;
volatile uint32_t g_modbus_tx_busy_rejections;
volatile uint32_t g_modbus_tx_last_start_tick;
volatile uint32_t g_modbus_tx_last_end_tick;
volatile uint32_t g_modbus_tx_last_duration_ms;
volatile uint16_t g_modbus_tx_last_length;
volatile uint32_t g_modbus_tx_last_hal_status;
volatile uint32_t g_modbus_de_assert_tick;
volatile uint32_t g_modbus_de_release_tick;
volatile uint32_t g_modbus_boundary_to_de_ms;
volatile uint32_t g_modbus_de_duration_ms;

/* DWT gives sub-millisecond DE guard delays without blocking the RTOS tick.
 * This function is called only from task context, never from an ISR. */
static void modbus_engine_delay_us(uint32_t delay_us)
{
    uint32_t start;
    uint32_t cycles;

    if (delay_us == 0U) {
        return;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    cycles = (uint32_t)(((uint64_t)SystemCoreClock * delay_us) / 1000000ULL);
    if (cycles == 0U) {
        cycles = 1U;
    }
    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
        __NOP();
    }
}

/* Match Titania's RS485 timing policy: derive the timeout from the active
 * UART framing and the actual response length instead of using a fixed
 * one-second wait. */
static bool modbus_engine_calculate_tx_timeouts(uint16_t length,
                                                uint32_t *tx_timeout_ms,
                                                uint32_t *tc_timeout_ms)
{
    uint32_t word_bits;
    uint32_t data_bits;
    uint32_t parity_bits;
    uint32_t stop_bits;
    uint32_t bits_per_character;
    uint64_t transfer_us;
    uint64_t tx_ms;
    uint64_t tc_ms;

    if ((length == 0U) || (tx_timeout_ms == NULL) ||
        (tc_timeout_ms == NULL) || (huart3.Init.BaudRate == 0U)) {
        return false;
    }

    switch (huart3.Init.WordLength) {
    case UART_WORDLENGTH_8B:
        word_bits = 8U;
        break;
    case UART_WORDLENGTH_9B:
        word_bits = 9U;
        break;
    default:
        return false;
    }

    if (huart3.Init.Parity == UART_PARITY_NONE) {
        data_bits = word_bits;
        parity_bits = 0U;
    } else if ((huart3.Init.Parity == UART_PARITY_EVEN) ||
               (huart3.Init.Parity == UART_PARITY_ODD)) {
        data_bits = word_bits - 1U;
        parity_bits = 1U;
    } else {
        return false;
    }

    if (huart3.Init.StopBits == UART_STOPBITS_1) {
        stop_bits = 1U;
    } else if (huart3.Init.StopBits == UART_STOPBITS_2) {
        stop_bits = 2U;
    } else {
        return false;
    }

    bits_per_character = 1U + data_bits + parity_bits + stop_bits;
    transfer_us = (((uint64_t)length * bits_per_character * 1000000ULL) +
                   huart3.Init.BaudRate - 1ULL) /
                  huart3.Init.BaudRate;
    tx_ms = ((transfer_us + 999ULL) / 1000ULL) + 10ULL;
    tc_ms = ((((uint64_t)bits_per_character * 1000ULL) +
              huart3.Init.BaudRate - 1ULL) /
             huart3.Init.BaudRate) + 2ULL;

    if ((tx_ms > UINT32_MAX) || (tc_ms > UINT32_MAX)) {
        return false;
    }
    *tx_timeout_ms = (uint32_t)tx_ms;
    *tc_timeout_ms = (uint32_t)tc_ms;
    return true;
}

static bool modbus_engine_wait_uart_tc(uint32_t timeout_ms)
{
    const uint32_t start = HAL_GetTick();

    while (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC) == RESET) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            return false;
        }
    }
    return true;
}

void ModbusEngineTx_SetDe(bool transmit)
{
    uint32_t now = HAL_GetTick();

    #if !MODBUS_PYXIS_HW_DE
        HAL_GPIO_WritePin(MODBUS_PYXIS_DE_GPIO_PORT,
                        MODBUS_PYXIS_DE_GPIO_PIN,
                        transmit ? GPIO_PIN_SET : GPIO_PIN_RESET);
    #endif

    if (transmit) {
        g_modbus_de_assert_tick = now;
        g_modbus_boundary_to_de_ms = now - g_modbus_frame_boundary_tick;
        de_active = true;
    } else if (de_active) {
        g_modbus_de_release_tick = now;
        g_modbus_de_duration_ms = now - g_modbus_de_assert_tick;
        de_active = false;
    }
}


bool ModbusEngineTx_Init(void)
{
    tx_busy = false;
    tx_errors = 0U;
    de_active = false;
    g_modbus_tx_attempts = 0U;
    g_modbus_tx_errors = 0U;
    g_modbus_tx_complete_callbacks = 0U;
    g_modbus_tx_wait_timeouts = 0U;
    g_modbus_tx_busy_rejections = 0U;
    g_modbus_tx_last_start_tick = 0U;
    g_modbus_tx_last_end_tick = 0U;
    g_modbus_tx_last_duration_ms = 0U;
    g_modbus_tx_last_length = 0U;
    g_modbus_tx_last_hal_status = (uint32_t)HAL_OK;
    g_modbus_de_assert_tick = 0U;
    g_modbus_de_release_tick = 0U;
    g_modbus_boundary_to_de_ms = 0U;
    g_modbus_de_duration_ms = 0U;
#if MODBUS_PYXIS_TX_IT
    tx_event = osEventFlagsNew(NULL);
    if (tx_event == NULL) {
        return false;
    }
#endif
    return (huart3.Instance == USART3);
}

bool ModbusEngineTx_Send(const uint8_t *frame, uint16_t length)
{
    HAL_StatusTypeDef status;
    uint32_t tx_timeout_ms;
    uint32_t tc_timeout_ms;

    if ((frame == NULL) || (length == 0U)) {
        return false;
    }
    if (tx_busy) {
        g_modbus_tx_busy_rejections++;
        return false;
    }
    if (!modbus_engine_calculate_tx_timeouts(length, &tx_timeout_ms,
                                             &tc_timeout_ms)) {
        g_modbus_tx_errors++;
        return false;
    }

    tx_busy = true;
    g_modbus_tx_attempts++;
    g_modbus_tx_last_length = length;
    g_modbus_tx_last_start_tick = HAL_GetTick();
    ModbusEngineTx_SetDe(true);
    modbus_engine_delay_us(MODBUS_PYXIS_DE_SETUP_DELAY_US);
#if MODBUS_PYXIS_TX_IT
    (void)osEventFlagsClear(tx_event, TX_COMPLETE_EVENT);
    status = HAL_UART_Transmit_IT(&huart3, (uint8_t *)frame, length);
    if (status == HAL_OK) {
        uint32_t flags = osEventFlagsWait(tx_event, TX_COMPLETE_EVENT,
                                          osFlagsWaitAny, tx_timeout_ms);
        if ((flags & TX_COMPLETE_EVENT) == 0U) {
            g_modbus_tx_wait_timeouts++;
            (void)HAL_UART_AbortTransmit(&huart3);
            status = HAL_TIMEOUT;
        }
        if ((status == HAL_OK) &&
            !modbus_engine_wait_uart_tc(tc_timeout_ms)) {
            g_modbus_tx_wait_timeouts++;
            status = HAL_TIMEOUT;
        }
    }
#else
    status = HAL_UART_Transmit(&huart3, (uint8_t *)frame, length,
                               tx_timeout_ms);
    if ((status == HAL_OK) &&
        !modbus_engine_wait_uart_tc(tc_timeout_ms)) {
        g_modbus_tx_wait_timeouts++;
        status = HAL_TIMEOUT;
    }
#endif
    if (status == HAL_OK) {
        modbus_engine_delay_us(MODBUS_PYXIS_DE_HOLD_DELAY_US);
        ModbusEngineTx_SetDe(false);
    }
    g_modbus_tx_last_end_tick = HAL_GetTick();
    g_modbus_tx_last_duration_ms =
        g_modbus_tx_last_end_tick - g_modbus_tx_last_start_tick;
    g_modbus_tx_last_hal_status = (uint32_t)status;
    tx_busy = false;
    if (status != HAL_OK) {
        ModbusEngineTx_SetDe(false);
    }

    if (status != HAL_OK) {
        tx_errors++;
        g_modbus_tx_errors++;
        return false;
    }

    /* HAL_UART_Transmit() returns after TC, so RS485 DE may safely release. */
    return true;
}

#if MODBUS_PYXIS_TX_IT
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart_cb)
{
    if (huart_cb == &huart3) {
        g_modbus_tx_complete_callbacks++;

        if (tx_event != NULL) {
            (void)osEventFlagsSet(tx_event, TX_COMPLETE_EVENT);
        }
    }
}
#endif

bool ModbusEngineTx_IsBusy(void)
{
    return tx_busy;
}

uint32_t ModbusEngineTx_GetErrorCount(void)
{
    return tx_errors;
}
