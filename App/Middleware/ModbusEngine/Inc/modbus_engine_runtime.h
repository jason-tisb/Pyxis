#ifndef MODBUS_ENGINE_RUNTIME_H
#define MODBUS_ENGINE_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint32_t frames_seen;
    volatile uint32_t frames_processed;
    volatile uint32_t responses_generated;
    volatile uint32_t no_response_frames;
    volatile uint32_t dropped_frames;
} ModbusEngineRuntimeDiagnostics_t;

void ModbusEngineRuntime_Init(void);
void ModbusEngineRuntime_SetSlaveId(uint8_t slave_id);
uint8_t ModbusEngineRuntime_GetSlaveId(void);
bool ModbusEngineRuntime_ProcessFrame(const uint8_t *frame, uint16_t length,
                                      const uint8_t **response,
                                      uint16_t *response_length);
const ModbusEngineRuntimeDiagnostics_t *ModbusEngineRuntime_GetDiagnostics(void);
uint32_t ModbusEngineRuntime_GetLastResult(void);
bool ModbusEngineRuntime_LastFrameMatches(uint8_t slave_id);

#endif
