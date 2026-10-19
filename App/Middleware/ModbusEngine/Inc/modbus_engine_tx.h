#ifndef MODBUS_ENGINE_TX_H
#define MODBUS_ENGINE_TX_H

#include <stdbool.h>
#include <stdint.h>

bool ModbusEngineTx_Init(void);
bool ModbusEngineTx_Send(const uint8_t *frame, uint16_t length);
bool ModbusEngineTx_IsBusy(void);
uint32_t ModbusEngineTx_GetErrorCount(void);
void ModbusEngineTx_SetDe(bool transmit);

extern volatile uint32_t g_modbus_tx_attempts;
extern volatile uint32_t g_modbus_tx_errors;
extern volatile uint32_t g_modbus_tx_busy_rejections;
extern volatile uint32_t g_modbus_tx_last_start_tick;
extern volatile uint32_t g_modbus_tx_last_end_tick;
extern volatile uint32_t g_modbus_tx_last_duration_ms;
extern volatile uint16_t g_modbus_tx_last_length;
extern volatile uint32_t g_modbus_tx_last_hal_status;
extern volatile uint32_t g_modbus_de_assert_tick;
extern volatile uint32_t g_modbus_de_release_tick;
extern volatile uint32_t g_modbus_boundary_to_de_ms;
extern volatile uint32_t g_modbus_de_duration_ms;
extern volatile uint32_t g_modbus_tx_complete_callbacks;
extern volatile uint32_t g_modbus_tx_wait_timeouts;
#endif
