#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mb.h"
#include "modbus_module.h"
#include "user_mb_app.h"

typedef struct {
    USHORT *holding_regs;
    uint16_t reg_count;
} ModbusAdapterContext_t;

uint16_t ModbusAdapter_BaudToCode(uint32_t baud_rate);
bool ModbusAdapter_CodeToBaud(uint16_t baud_code, uint32_t *baud_rate);
bool ModbusAdapter_IsValidBaudCode(uint16_t baud_code);
bool ModbusAdapter_IsValidSlaveId(uint16_t value);
bool ModbusAdapter_IsValidParityCode(uint16_t value);
eMBParity ModbusAdapter_ParityFromCode(uint16_t parity_code);
uint16_t ModbusAdapter_ParityToCode(uint8_t parity_code);

void ModbusAdapter_InitMap(const ModbusAdapterContext_t *ctx, const ModbusConfig_t *current_cfg);
bool ModbusAdapter_CheckConfigUpdate(const ModbusAdapterContext_t *ctx,
                                     const ModbusConfig_t *current_cfg,
                                     ModbusConfig_t *out_new_cfg);

#ifdef __cplusplus
}
#endif
