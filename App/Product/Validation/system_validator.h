#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "mb.h"

bool SystemValidator_ModbusWriteValidateHoldingRegister(USHORT reg_addr, USHORT value);

#ifdef __cplusplus
}
#endif
