#ifndef MODBUS_AUTOBAUD_CAPTURE_H
#define MODBUS_AUTOBAUD_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#define MODBUS_AUTOBAUD_CAPTURE_MAX_INTERVALS 64U

typedef struct {
    uint32_t interval_ticks[MODBUS_AUTOBAUD_CAPTURE_MAX_INTERVALS];
    uint32_t timer_frequency_hz;
    uint16_t interval_count;
    uint8_t overrun;
    uint8_t active;
    uint8_t timed_out;
} modbus_autobaud_capture_result_t;

/* Starts TIM2_CH4 capture on PC4 after suspending the normal USART3 RX path. */
bool ModbusAutobaudCapture_Start(void);

/* Cooperative task-context service. Returns true when the capture is complete. */
bool ModbusAutobaudCapture_Service(uint32_t timeout_ms);

/* Stops capture and restores PC4 to USART3_RX and the normal RX/DMA path. */
bool ModbusAutobaudCapture_Stop(void);

bool ModbusAutobaudCapture_IsActive(void);
uint32_t ModbusAutobaudCapture_GetTimerFrequencyHz(void);
bool ModbusAutobaudCapture_GetResult(
    modbus_autobaud_capture_result_t *result);

/* Called by the TIM2 IRQ path. Kept separate for unit/integration testing. */
void ModbusAutobaudCapture_OnCaptureIrq(void);

#endif
