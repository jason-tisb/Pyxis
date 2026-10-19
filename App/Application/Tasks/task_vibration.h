#pragma once

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
