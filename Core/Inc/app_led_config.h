#pragma once

#include "main.h"

typedef enum
{
    LED_GPIO_ACTIVE_LOW = 0,
    LED_GPIO_ACTIVE_HIGH = 1
} led_gpio_active_level_t;

/*
 * Reuse the board LED mapping already defined in main.h:
 * LED_PWR  on PB15
 * LED_STAT on PC6
 */
/* "Green" in the engine means the STAT LED channel on this board. */
#define LED_GREEN_GPIO_Port    LED_STAT_GPIO_Port
#define LED_GREEN_Pin          LED_STAT_Pin
#define LED_RED_GPIO_Port      LED_PWR_GPIO_Port
#define LED_RED_Pin            LED_PWR_Pin

#define LED_GREEN_ACTIVE_LEVEL LED_GPIO_ACTIVE_LOW
#define LED_RED_ACTIVE_LEVEL   LED_GPIO_ACTIVE_LOW

#define LED_TASK_PERIOD_MS                 10U

#define LED_HEARTBEAT_ON_MS                150U
#define LED_HEARTBEAT_OFF_MS               1850U

#define LED_FAST_ON_MS                     150U
#define LED_FAST_OFF_MS                    150U

#define LED_SLOW_ON_MS                     500U
#define LED_SLOW_OFF_MS                    1500U

#define LED_DOUBLE_ON_MS                   200U
#define LED_DOUBLE_OFF_MS                  200U
#define LED_DOUBLE_END_OFF_MS              1400U

#define LED_ACTIVITY_PULSE_MS              150U
#define LED_ERROR_PULSE_MS                 200U

#define LED_SHORT_PULSE_ON_MS              200U
#define LED_SHORT_PULSE_OFF_MS             300U
#define LED_LONG_PULSE_ON_MS               800U

#define LED_STARTUP_PULSE_ON_MS            250U
#define LED_STARTUP_PULSE_OFF_MS           250U
#define LED_STARTUP_ZERO_ON_MS             200U
#define LED_STARTUP_DIGIT_SEPARATOR_MS     300U
#define LED_STARTUP_STAGE_SEPARATOR_MS     375U
#define LED_STARTUP_END_SEPARATOR_MS       500U

#define LED_STATUS_FRAME_MS                2000U

#define GREEN_ACTIVITY_MIN_INTERVAL_MS     250U
#define RED_ERROR_MIN_INTERVAL_MS          500U

#define NETWORK_ACTIVE_TIMEOUT_MS          5000U
#define NETWORK_LOST_TIMEOUT_MS            10000U

#define MODBUS_SLAVE_ID_MIN                0x01U
#define MODBUS_SLAVE_ID_MAX                0xF7U
