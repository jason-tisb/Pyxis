#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t  slave_id;
    uint32_t baud_rate;
    uint8_t  parity; /* 0=Even/1sb, 1=Odd/1sb, 2=None/2sb, 3=None/1sb */
} ModbusConfig_t;

typedef struct {
    uint32_t total_rx_ok;
    uint32_t crc_errors;
    uint32_t exception_res;
} ModbusStats_t;

void ModbusModule_Init(void);
void ModbusModule_GetConfig(ModbusConfig_t* out_cfg);
bool ModbusModule_ValidateConfig(const ModbusConfig_t* candidate_cfg);
bool ModbusModule_UpdateConfig(const ModbusConfig_t* new_cfg);
void ModbusModule_RecordRxOk(void);
void ModbusModule_RecordCrcError(void);
void ModbusModule_GetStats(ModbusStats_t* out_stats);
void ModbusModule_ClearStats(void);
