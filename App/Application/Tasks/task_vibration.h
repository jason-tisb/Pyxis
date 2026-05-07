#pragma once

#include "main.h"
#include "cmsis_os.h"
#include "mb.h"
#include "user_mb_app.h"

extern USHORT usSRegInBuf[S_REG_INPUT_NREGS];

void main_TaskVibration(void *argument);
