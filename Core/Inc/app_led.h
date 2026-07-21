#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    LED_STATE_FATAL_FAULT = 0,
    LED_STATE_DEVICE_FAULT,
    LED_STATE_CONFIG_INVALID,
    LED_STATE_SLAVE_ID_DISPLAY,
    LED_STATE_IDENTIFY,
    LED_STATE_COMM_WARNING,
    LED_STATE_GENERAL_WARNING,
    LED_STATE_NETWORK_LOST,
    LED_STATE_NO_POLLING_YET,
    LED_STATE_NORMAL_POLLING_ACTIVE
} led_base_state_t;

void LED_Init(void);
void LED_Task_10ms(void);
void LED_Task_1ms_ISR(void);

void LED_SetBaseState(led_base_state_t state);
void LED_RequestSlaveIdDisplay(uint8_t slave_id, uint8_t repeat_count);
void LED_RequestStartupDisplay(uint8_t slave_id, uint8_t baud_display, uint8_t parity_display, uint8_t repeat_count);
uint32_t LED_GetStartupDisplayDurationMs(uint8_t slave_id, uint8_t baud_display, uint8_t parity_display, uint8_t repeat_count);
void LED_RequestIdentify(uint32_t duration_ms);

void LED_NotifyValidModbusTransaction(void);
void LED_NotifyCommunicationError(void);

void LED_SetFatalFault(bool active);
void LED_SetDeviceFault(bool active);
void LED_SetConfigurationInvalid(bool active);
void LED_SetGeneralWarning(bool active);
void LED_SetCommunicationWarning(bool active);
