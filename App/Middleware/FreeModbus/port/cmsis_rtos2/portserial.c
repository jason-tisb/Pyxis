#include "port.h"
#include "main.h"
#include "cmsis_os2.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"

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
        rb_push_isr(uart3_rx_dma_buf[pos]);
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
