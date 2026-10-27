#ifndef MODBUS_ENGINE_PORT_H
#define MODBUS_ENGINE_PORT_H

#include <stdbool.h>
#include <stdint.h>

void ModbusEnginePort_Init(void);
bool ModbusEnginePort_UartInit(uint32_t baud_rate, uint8_t data_bits,
                               uint8_t parity_code);
bool ModbusEnginePort_ReconfigureUart(uint32_t baud_rate, uint8_t data_bits,
                                      uint8_t parity_code,
                                      uint32_t stop_bits);
bool ModbusEnginePort_GetUartConfig(uint32_t *baud_rate, uint8_t *data_bits,
                                    uint8_t *parity_code,
                                    uint32_t *stop_bits);
void ModbusEnginePort_SetRtuTiming(uint32_t baud_rate, uint8_t data_bits,
                                   uint8_t parity_code, uint32_t stop_bits);
bool ModbusEnginePort_UartEnable(bool enable);
void ModbusEnginePort_OnRxByte(uint8_t byte);
void ModbusEnginePort_OnRxFrameBoundary(void);
bool ModbusEnginePort_FramePending(void);
void ModbusEnginePort_RxTimeoutIsr(void);
bool ModbusEnginePort_Poll(const uint8_t **frame, uint16_t *length);
void ModbusEnginePort_ClearFrame(void);
uint32_t ModbusEnginePort_GetFrameCount(void);
uint32_t ModbusEnginePort_GetOverflowCount(void);

/* RX timing diagnostics.  Timestamps are HAL_GetTick() values captured at
 * the first and last byte handed to the custom Modbus engine for a frame. */
extern volatile uint32_t g_modbus_rx_first_tick;
extern volatile uint32_t g_modbus_rx_last_tick;
extern volatile uint32_t g_modbus_frame_boundary_tick;
extern volatile uint32_t g_uart3_dma_restart_tick;
extern volatile uint32_t g_uart3_rx_callback_tick;
extern volatile uint32_t g_uart3_last_disable_tick;
extern volatile uint32_t g_uart3_last_enable_tick;
extern volatile uint32_t g_uart3_frame_queue_pushed;
extern volatile uint32_t g_uart3_frame_queue_popped;
extern volatile uint32_t g_uart3_frame_queue_overruns;
extern volatile uint32_t g_uart3_frame_queue_max_depth;
extern volatile uint32_t g_uart3_error_recovery_attempts;
extern volatile uint32_t g_uart3_error_recovery_failures;
extern volatile uint32_t g_uart3_transport_flushes;
extern volatile uint32_t g_uart3_last_recovery_status;

#endif
