#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "main.h"
#include "cmsis_os.h"
#include "mb.h"
#include "modbus_engine_config.h"
#if MODBUS_ENGINE_ACTIVE
#include "modbus_engine_map.h"
#else
#include "user_mb_app.h"
#endif

#if !MODBUS_ENGINE_ACTIVE
extern USHORT usSRegInBuf[S_REG_INPUT_NREGS];
#endif

void main_TaskVibration(void *argument);

/* Temperature-service diagnostics for debugger and health monitoring. */
extern volatile uint32_t g_iis3dwb_temperature_attempts;
extern volatile uint32_t g_iis3dwb_temperature_successes;
extern volatile uint32_t g_iis3dwb_temperature_failures;
extern volatile uint32_t g_iis3dwb_temperature_last_attempt_tick;
extern volatile uint32_t g_iis3dwb_temperature_last_success_tick;
extern volatile int16_t g_iis3dwb_temperature_last_value_x10;
extern volatile bool g_iis3dwb_temperature_valid;
extern volatile bool g_iis3dwb_sensor_ready;
