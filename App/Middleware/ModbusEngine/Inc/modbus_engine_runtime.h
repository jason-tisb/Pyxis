#ifndef MODBUS_ENGINE_RUNTIME_H
#define MODBUS_ENGINE_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_module.h"

typedef struct {
    volatile uint32_t frames_seen;
    volatile uint32_t frames_processed;
    volatile uint32_t responses_generated;
    volatile uint32_t no_response_frames;
    volatile uint32_t dropped_frames;
    volatile uint32_t write_transactions_staged;
    volatile uint32_t write_validation_failures;
    volatile uint32_t write_commits_completed;
    volatile uint32_t write_commit_failures;
    volatile uint32_t persistence_requests;
    volatile uint32_t persistence_successes;
    volatile uint32_t persistence_failures;
    volatile uint32_t restart_required;
    volatile uint32_t persistence_fault;
    volatile uint32_t persistence_state;
    /* Retained as deprecated counters for debugger/backward compatibility. */
    volatile uint32_t comm_change_deferred;
    volatile uint32_t comm_response_tx_complete;
    volatile uint32_t comm_persist_failures;
    volatile uint32_t comm_apply_failures;
    volatile uint32_t system_command_requests;
    volatile uint32_t system_command_rejects;
    volatile uint32_t restart_requests;
    volatile uint32_t factory_reset_requests;
    volatile uint32_t factory_reset_successes;
    volatile uint32_t factory_reset_failures;
    volatile uint32_t system_command_state;
} ModbusEngineRuntimeDiagnostics_t;

extern volatile uint32_t g_modbus_restart_required;
extern volatile uint32_t g_modbus_persistence_fault;
extern volatile uint32_t g_modbus_persistence_state;
extern volatile uint32_t g_modbus_system_command_state;
extern volatile uint32_t g_modbus_system_command_last_error;

/* Persistence lifecycle is separate from active communication.  A boot-only
 * communication write never gates Modbus service or changes the active UART
 * during the current boot. */
typedef enum {
    MODBUS_PERSIST_STATE_IDLE = 0,
    MODBUS_PERSIST_STATE_PENDING = 1,
    MODBUS_PERSIST_STATE_FAILED = 2
} ModbusPersistenceState_t;

/* Deprecated compatibility type.  New code should use the explicit
 * persistence-state accessor below. */
typedef ModbusPersistenceState_t ModbusEngineCommState_t;

typedef enum {
    MODBUS_SYSTEM_ACTION_NONE = 0,
    MODBUS_SYSTEM_ACTION_RESTART = 1,
    MODBUS_SYSTEM_ACTION_FACTORY_RESET = 2
} ModbusSystemAction_t;

void ModbusEngineRuntime_Init(void);
void ModbusEngineRuntime_SetSlaveId(uint8_t slave_id);
uint8_t ModbusEngineRuntime_GetSlaveId(void);
void ModbusEngineRuntime_SetActiveCommConfig(const ModbusConfig_t *config);
bool ModbusEngineRuntime_GetActiveCommConfig(ModbusConfig_t *out_config);
bool ModbusEngineRuntime_GetConfiguredCommConfig(ModbusConfig_t *out_config);
bool ModbusEngineRuntime_HasCommPersistencePending(void);
ModbusPersistenceState_t ModbusEngineRuntime_GetPersistenceState(void);
bool ModbusEngineRuntime_PersistConfiguredComm(void);
void ModbusEngineRuntime_RecordPersistenceResult(bool success);
bool ModbusEngineRuntime_IsRestartRequired(void);
bool ModbusEngineRuntime_HasPersistenceFault(void);
/* Executes a previously accepted command after the response's UART TC/DE
 * sequence has completed.  Reset actions do not return on target. */
bool ModbusEngineRuntime_ServiceSystemAction(void);
void ModbusEngineRuntime_CancelSystemAction(void);
ModbusSystemAction_t ModbusEngineRuntime_GetPendingSystemAction(void);
bool ModbusEngineRuntime_ProcessFrame(const uint8_t *frame, uint16_t length,
                                      const uint8_t **response,
                                      uint16_t *response_length);
const ModbusEngineRuntimeDiagnostics_t *ModbusEngineRuntime_GetDiagnostics(void);
uint32_t ModbusEngineRuntime_GetLastResult(void);
bool ModbusEngineRuntime_LastFrameMatches(uint8_t slave_id);
/* Legacy live-apply APIs are intentionally no-ops/deprecated in Milestone F. */
ModbusEngineCommState_t ModbusEngineRuntime_GetCommState(void);
bool ModbusEngineRuntime_GetPendingCommConfig(ModbusConfig_t *out_cfg);
void ModbusEngineRuntime_MarkCommResponseTransmitted(bool complete);
void ModbusEngineRuntime_MarkCommPersistFailure(void);
void ModbusEngineRuntime_MarkCommPersistSuccess(void);
/* Deprecated name retained for older callers; this no longer applies UART
 * settings at runtime. */
void ModbusEngineRuntime_CompleteCommApply(bool applied);

#endif
