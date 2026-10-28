#include "modbus_engine_runtime.h"

#include <string.h>

#include "FreeRTOS.h"

#include "modbus_core.h"
#include "modbus_engine_config.h"
#include "modbus_engine_map.h"
#include "modbus_persistence.h"
#include "modbus_module.h"
#include "modbus_register_map.h"
#include "user_mb_app.h"
#include "modbus_app_adapter.h"
#include "vibration_modbus_adapter.h"
#include "vibration_module.h"

#define MODBUS_ENGINE_TX_SIZE 256U
#define MODBUS_ENGINE_MAX_STAGED_REGISTERS 123U

typedef struct {
    bool active;
    uint16_t start_addr;
    uint16_t count;
    uint16_t values[MODBUS_ENGINE_MAX_STAGED_REGISTERS];
    ModbusConfig_t comm_cfg;
    VibConfig_t vibration_cfg;
    bool comm_changed;
    bool vibration_changed;
    bool has_immediate_config;
    bool has_boot_config;
    bool persistence_required;
    bool system_command_present;
    ModbusSystemAction_t system_action;
} runtime_proposal_t;

static modbus_core_ctx_t core_ctx;
static uint8_t tx_buffer[MODBUS_ENGINE_TX_SIZE];
static ModbusEngineRuntimeDiagnostics_t diagnostics;
static runtime_proposal_t proposal;
static ModbusConfig_t pending_comm_cfg;
static ModbusPersistenceState_t persistence_state;
static ModbusConfig_t active_comm_cfg;
static bool comm_persistence_pending;
static bool persistence_fault;
static ModbusSystemAction_t pending_system_action;
volatile uint32_t g_modbus_process_tick;

/* Exported debug counters for on-target transport troubleshooting. */
volatile uint32_t g_modbus_frames_seen;
volatile uint32_t g_modbus_frames_dropped;
volatile uint32_t g_modbus_frames_processed;
volatile uint32_t g_modbus_responses_generated;
volatile uint32_t g_modbus_no_response_frames;
volatile uint16_t g_modbus_last_frame_len;
volatile uint8_t g_modbus_last_frame[MODBUS_ENGINE_TX_SIZE];
volatile uint32_t g_modbus_last_result;
volatile uint32_t g_modbus_restart_required;
volatile uint32_t g_modbus_persistence_fault;
volatile uint32_t g_modbus_persistence_state;
volatile uint32_t g_modbus_system_command_state;
volatile uint32_t g_modbus_system_command_last_error;

static bool runtime_read_defaults(void *user, uint16_t address,
                                  uint16_t *value)
{
    const modbus_engine_map_entry_t *entry;

    (void)user;
    if (value == NULL) {
        return false;
    }
    entry = ModbusEngineMap_Find(MODBUS_REG_HOLDING, address);
    if (entry == NULL) {
        return false;
    }
    *value = entry->default_value;
    return true;
}

static bool runtime_is_system_command_address(uint16_t address)
{
    return address == PYXIS_REG_SYSTEM_COMMAND_ADDR;
}

static bool runtime_is_comm_config_entry(
    const modbus_engine_map_entry_t *entry)
{
    if (entry == NULL) {
        return false;
    }

    switch (entry->id) {
    case PYXIS_REG_SLAVE_ID:
    case PYXIS_REG_BAUD_SELECTOR:
    case PYXIS_REG_FRAME_SELECTOR:
        return true;
    default:
        return false;
    }
}

static bool runtime_is_vibration_config_entry(
    const modbus_engine_map_entry_t *entry)
{
    if (entry == NULL) {
        return false;
    }

    switch (entry->id) {
    case PYXIS_REG_MACHINE_PROFILE:
    case PYXIS_REG_GEAR_RATIO:
    case PYXIS_REG_BASE_FREQ_HZ:
    case PYXIS_REG_BEARING_BALL_COUNT:
    case PYXIS_REG_BEARING_BALL_SIZE:
    case PYXIS_REG_BEARING_DIAMETER:
    case PYXIS_REG_BEARING_ALPHA:
    case PYXIS_REG_FILTER_HIGH_PASS:
    case PYXIS_REG_FILTER_LOW_PASS:
    case PYXIS_REG_GEAR_TEETH:
        return true;
    default:
        return false;
    }
}

static ModbusSystemAction_t runtime_decode_system_command(uint16_t value)
{
    switch (value) {
    case PYXIS_SYSTEM_COMMAND_RESTART:
        return MODBUS_SYSTEM_ACTION_RESTART;
    case PYXIS_SYSTEM_COMMAND_SYSTEM_RESET:
        return MODBUS_SYSTEM_ACTION_SYSTEM_RESET;
    case PYXIS_SYSTEM_COMMAND_CAPTURE_BASELINE:
        return MODBUS_SYSTEM_ACTION_CAPTURE_BASELINE;
    case PYXIS_SYSTEM_COMMAND_RESET_BASELINE:
        return MODBUS_SYSTEM_ACTION_RESET_BASELINE;
    case PYXIS_SYSTEM_COMMAND_FACTORY_RESET:
        return MODBUS_SYSTEM_ACTION_FACTORY_RESET;
    default:
        return MODBUS_SYSTEM_ACTION_NONE;
    }
}

static bool runtime_read_proposed(void *user, uint16_t address,
                                  uint16_t *value)
{
    uint16_t *holding;
    uint16_t i;

    (void)user;
    if (value == NULL || address >= MODBUS_ENGINE_HOLDING_STORAGE_SIZE) {
        return false;
    }
    holding = ModbusEngineMap_HoldingStorage();
    *value = holding[address];
    if (proposal.active && address >= proposal.start_addr &&
        (uint32_t)(address - proposal.start_addr) < proposal.count) {
        i = (uint16_t)(address - proposal.start_addr);
        *value = proposal.values[i];
    }
    return true;
}

static bool runtime_set_proposal(uint16_t start_addr, const uint16_t *values,
                                 uint16_t count)
{
    ModbusConfig_t current_comm;
    VibConfig_t current_vibration;
    bool validate_comm = false;
    bool validate_vibration = false;

    if ((values == NULL) || (count == 0U) ||
        (count > MODBUS_ENGINE_MAX_STAGED_REGISTERS) ||
        (start_addr >= MODBUS_ENGINE_HOLDING_STORAGE_SIZE) ||
        ((uint32_t)start_addr + (uint32_t)count >
         MODBUS_ENGINE_HOLDING_STORAGE_SIZE)) {
        return false;
    }
    memset(&proposal, 0, sizeof(proposal));
    proposal.start_addr = start_addr;
    proposal.count = count;
    memcpy(proposal.values, values, (size_t)count * sizeof(values[0]));
    proposal.active = true;
    for (uint16_t i = 0U; i < count; ++i) {
        const modbus_engine_map_entry_t *entry = ModbusEngineMap_Find(
            MODBUS_REG_HOLDING, (uint16_t)(start_addr + i));
        uint16_t *holding = ModbusEngineMap_HoldingStorage();

        if ((entry != NULL) && (holding[start_addr + i] != values[i])) {
            if (entry->id == PYXIS_REG_SYSTEM_COMMAND) {
                proposal.system_command_present = true;
                proposal.system_action = runtime_decode_system_command(values[i]);
                continue;
            }
            validate_comm = validate_comm ||
                            runtime_is_comm_config_entry(entry);
            validate_vibration = validate_vibration ||
                                 runtime_is_vibration_config_entry(entry);
            if (entry->apply_policy == MODBUS_APPLY_ON_BOOT) {
                proposal.has_boot_config = true;
            } else {
                proposal.has_immediate_config = true;
            }
            if (entry->persistence == MODBUS_MAP_PERSISTENT) {
                proposal.persistence_required = true;
            }
        }
    }
    ModbusModule_GetConfig(&current_comm);
    VibrationModule_GetConfig(&current_vibration);
    proposal.comm_cfg = current_comm;
    proposal.vibration_cfg = current_vibration;

    /* Validate only the configuration domains touched by this request.  A
     * communication-only write must not be rejected because an unrelated
     * legacy vibration value is outside the current vibration-domain limits.
     * Mixed FC16 requests still validate both complete prospective configs. */
    if (validate_comm && !ModbusAdapter_BuildConfigFromReader(
            runtime_read_proposed, NULL, &current_comm, &proposal.comm_cfg)) {
        memset(&proposal, 0, sizeof(proposal));
        return false;
    }
    if (validate_vibration && !VibrationAdapter_BuildConfigFromReader(
            runtime_read_proposed, NULL, &current_vibration,
            &proposal.vibration_cfg)) {
        memset(&proposal, 0, sizeof(proposal));
        return false;
    }
    proposal.comm_changed = validate_comm &&
        (memcmp(&proposal.comm_cfg, &current_comm, sizeof(current_comm)) != 0);
    proposal.vibration_changed = validate_vibration &&
        (memcmp(&proposal.vibration_cfg, &current_vibration,
                sizeof(current_vibration)) != 0);
    return true;
}

static void runtime_clear_proposal(void)
{
    memset(&proposal, 0, sizeof(proposal));
}

static bool runtime_commit_proposal(void)
{
    uint16_t *holding;
    uint16_t i;
    bool vibration_commit_ok = true;
    bool comm_commit_ok = true;

    if (!proposal.active || proposal.count == 0U) {
        return false;
    }
    if (proposal.system_command_present &&
        (proposal.system_action == MODBUS_SYSTEM_ACTION_NONE ||
         pending_system_action != MODBUS_SYSTEM_ACTION_NONE)) {
        return false;
    }
    holding = ModbusEngineMap_HoldingStorage();
    if ((uint32_t)proposal.start_addr + proposal.count >
        MODBUS_ENGINE_HOLDING_STORAGE_SIZE) {
        return false;
    }
    /* The prospective vibration configuration has already passed complete
     * cross-register validation.  The short critical section makes the
     * holding image and its application shadow visible together; persistence
     * remains a later lifecycle step. */
    if (proposal.vibration_changed &&
        !VibrationModule_ValidateConfig(&proposal.vibration_cfg)) {
        return false;
    }
    taskENTER_CRITICAL();
    if (proposal.vibration_changed) {
        vibration_commit_ok =
            VibrationModule_CommitValidatedRuntimeConfig(
                &proposal.vibration_cfg);
    }
    if (vibration_commit_ok && proposal.comm_changed) {
        comm_commit_ok = ModbusModule_CommitValidatedRuntimeConfig(
            &proposal.comm_cfg);
    }
    if (vibration_commit_ok && comm_commit_ok) {
        for (i = 0U; i < proposal.count; ++i) {
            if (!runtime_is_system_command_address(
                    (uint16_t)(proposal.start_addr + i))) {
                holding[proposal.start_addr + i] = proposal.values[i];
            } else {
                /* Commands are one-shot and are never published as a
                 * persistent/configured Holding value. */
                holding[proposal.start_addr + i] =
                    (uint16_t)PYXIS_SYSTEM_COMMAND_NONE;
            }
        }
    }
    taskEXIT_CRITICAL();
    if (!vibration_commit_ok || !comm_commit_ok) {
        return false;
    }
    if (proposal.system_command_present) {
        pending_system_action = proposal.system_action;
        diagnostics.system_command_requests++;
        diagnostics.system_command_state =
            (uint32_t)pending_system_action;
        g_modbus_system_command_state =
            (uint32_t)pending_system_action;
        if (pending_system_action == MODBUS_SYSTEM_ACTION_RESTART) {
            diagnostics.restart_requests++;
        } else if (pending_system_action ==
                   MODBUS_SYSTEM_ACTION_FACTORY_RESET) {
            diagnostics.factory_reset_requests++;
        }
    }
    if (proposal.comm_changed) {
        /* Communication is boot-applied.  Publish the configured value now,
         * but keep the active UART/slave snapshot immutable for this boot. */
        pending_comm_cfg = proposal.comm_cfg;
        comm_persistence_pending = true;
        persistence_state = MODBUS_PERSIST_STATE_PENDING;
        diagnostics.persistence_state = (uint32_t)persistence_state;
        g_modbus_persistence_state = (uint32_t)persistence_state;
        diagnostics.persistence_requests++;
    }
    diagnostics.restart_required = ModbusEngineRuntime_IsRestartRequired() ? 1U : 0U;
    g_modbus_restart_required = diagnostics.restart_required;
    diagnostics.write_commits_completed++;
    return true;
}

static modbus_core_access_result_t runtime_read_reg(
    uint16_t addr, modbus_core_reg_type_t type,
    modbus_core_reg_info_t *out, uint16_t *cursor_hint)
{
    const modbus_engine_map_entry_t *entry;

    if (out == NULL) {
        return MODBUS_ACCESS_INVALID_CONTEXT;
    }

    memset(out, 0, sizeof(*out));
    if (cursor_hint != NULL) {
        *cursor_hint = addr;
    }

    /* Dense storage is only backing storage; canonical map coverage decides
     * whether the protocol address exists in this register space. */
    entry = ModbusEngineMap_Find(type, addr);
    if (entry == NULL) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }

    if (type == MODBUS_REG_HOLDING) {
        if (addr >= MODBUS_ENGINE_HOLDING_STORAGE_SIZE) {
            return MODBUS_ACCESS_ILLEGAL_ADDRESS;
        }
        out->value = ModbusEngineMap_HoldingStorage()[addr];
    } else if (type == MODBUS_REG_INPUT) {
        if (addr >= MODBUS_ENGINE_INPUT_STORAGE_SIZE) {
            return MODBUS_ACCESS_ILLEGAL_ADDRESS;
        }
        out->value = ModbusEngineMap_InputStorage()[addr];
    } else {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }

    if (ModbusEngineMap_Read(type, addr, &out->value, &out->writable,
                             &out->min, &out->max) != MODBUS_ACCESS_OK) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
    out->has_range = true;
    return MODBUS_ACCESS_OK;
}

static modbus_core_access_result_t runtime_read_multiple(
    uint16_t start_addr, modbus_core_reg_type_t type,
    uint16_t *values, uint16_t quantity)
{
    uint16_t i;

    if ((values == NULL) || (quantity == 0U)) {
        return MODBUS_ACCESS_ILLEGAL_VALUE;
    }

    for (i = 0U; i < quantity; ++i) {
        modbus_core_reg_info_t info;
        modbus_core_access_result_t result = runtime_read_reg(
            (uint16_t)(start_addr + i), type, &info, NULL);
        if (result != MODBUS_ACCESS_OK) {
            return result;
        }
        values[i] = info.value;
    }

    return MODBUS_ACCESS_OK;
}

static modbus_core_access_result_t runtime_validate_write(
    uint16_t addr, modbus_core_reg_type_t type, uint16_t value,
    const modbus_core_reg_info_t *current)
{
    uint16_t proposed = value;

    if ((type != MODBUS_REG_HOLDING) || (current == NULL)) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
    {
        modbus_core_access_result_t result =
            ModbusEngineMap_ValidateWrite(type, addr, proposed);
        if (result != MODBUS_ACCESS_OK) {
            return result;
        }
        if (runtime_is_system_command_address(addr) &&
            (proposed != PYXIS_SYSTEM_COMMAND_NONE) &&
            ((pending_system_action != MODBUS_SYSTEM_ACTION_NONE) ||
             (persistence_state == MODBUS_PERSIST_STATE_PENDING))) {
            return MODBUS_ACCESS_DEVICE_BUSY;
        }
        return MODBUS_ACCESS_OK;
    }
}

static modbus_core_access_result_t runtime_validate_multiple(
    uint16_t start_addr, const uint16_t *values, uint16_t quantity)
{
    modbus_core_access_result_t result;

    result = ModbusEngineMap_ValidateWriteRange(start_addr, values, quantity);
    if (result != MODBUS_ACCESS_OK) {
        return result;
    }
    if ((uint32_t)start_addr <= PYXIS_REG_SYSTEM_COMMAND_ADDR &&
        ((uint32_t)start_addr + quantity) > PYXIS_REG_SYSTEM_COMMAND_ADDR) {
        /* Destructive commands are FC06-only. */
        return MODBUS_ACCESS_ILLEGAL_VALUE;
    }
    if (!runtime_set_proposal(start_addr, values, quantity)) {
        diagnostics.write_validation_failures++;
        return MODBUS_ACCESS_ILLEGAL_VALUE;
    }
    return MODBUS_ACCESS_OK;
}

static modbus_core_access_result_t runtime_write_reg(
    uint16_t addr, modbus_core_reg_type_t type, uint16_t value)
{
    modbus_core_access_result_t validation;

    if ((type != MODBUS_REG_HOLDING) ||
        (addr >= MODBUS_ENGINE_HOLDING_STORAGE_SIZE)) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
    validation = ModbusEngineMap_ValidateWrite(type, addr, value);
    if (validation != MODBUS_ACCESS_OK) {
        return validation;
    }
    if (!runtime_set_proposal(addr, &value, 1U)) {
        diagnostics.write_validation_failures++;
        return MODBUS_ACCESS_ILLEGAL_VALUE;
    }
    diagnostics.write_transactions_staged++;
    return MODBUS_ACCESS_OK;
}

static modbus_core_access_result_t runtime_write_multiple(
    uint16_t start_addr, const uint16_t *values, uint16_t quantity)
{
    if ((values == NULL) || (quantity == 0U) ||
        (quantity > MODBUS_ENGINE_MAX_STAGED_REGISTERS)) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
    if (!runtime_set_proposal(start_addr, values, quantity)) {
        diagnostics.write_validation_failures++;
        return MODBUS_ACCESS_ILLEGAL_VALUE;
    }
    diagnostics.write_transactions_staged++;
    return MODBUS_ACCESS_OK;
}

void ModbusEngineRuntime_Init(void)
{
    ModbusConfig_t config;

    memset(&core_ctx, 0, sizeof(core_ctx));
    memset(&diagnostics, 0, sizeof(diagnostics));
    runtime_clear_proposal();
    memset(&pending_comm_cfg, 0, sizeof(pending_comm_cfg));
    persistence_state = MODBUS_PERSIST_STATE_IDLE;
    memset(&active_comm_cfg, 0, sizeof(active_comm_cfg));
    comm_persistence_pending = false;
    persistence_fault = false;
    pending_system_action = MODBUS_SYSTEM_ACTION_NONE;
    g_modbus_restart_required = 0U;
    g_modbus_persistence_fault = 0U;
    g_modbus_persistence_state = (uint32_t)persistence_state;
    g_modbus_system_command_state =
        (uint32_t)MODBUS_SYSTEM_ACTION_NONE;
    g_modbus_system_command_last_error = 0U;
    g_modbus_frames_seen = 0U;
    g_modbus_frames_dropped = 0U;
    g_modbus_frames_processed = 0U;
    g_modbus_responses_generated = 0U;
    g_modbus_no_response_frames = 0U;
    g_modbus_last_frame_len = 0U;
    memset((void *)g_modbus_last_frame, 0, sizeof(g_modbus_last_frame));
    g_modbus_last_result = 0U;
    memset(tx_buffer, 0, sizeof(tx_buffer));

    (void)ModbusEngineMap_Validate();

    ModbusModule_GetConfig(&config);
#if MODBUS_ENGINE_ACTIVE && MODBUS_ENGINE_FORCE_FIXED_COMM
    core_ctx.slave_id = MODBUS_ENGINE_FIXED_SLAVE_ID;
#else
    core_ctx.slave_id = config.slave_id;
#endif
    active_comm_cfg = config;
    core_ctx.tx_max_len = MODBUS_ENGINE_TX_SIZE;
    core_ctx.read_reg = runtime_read_reg;
    core_ctx.write_reg = runtime_write_reg;
    core_ctx.validate_write = runtime_validate_write;
    core_ctx.validate_multiple_registers = runtime_validate_multiple;
    core_ctx.write_multiple_registers = runtime_write_multiple;
    core_ctx.read_multiple_registers = runtime_read_multiple;
}

void ModbusEngineRuntime_SetSlaveId(uint8_t slave_id)
{
    /* Compatibility setter: only startup code may establish the active ID. */
    core_ctx.slave_id = slave_id;
    active_comm_cfg.slave_id = slave_id;
}

uint8_t ModbusEngineRuntime_GetSlaveId(void)
{
    return core_ctx.slave_id;
}

bool ModbusEngineRuntime_ProcessFrame(const uint8_t *frame, uint16_t length,
                                      const uint8_t **response,
                                      uint16_t *response_length)
{
    uint16_t tx_length = 0U;
    bool need_reply = false;
    modbus_core_frame_meta_t meta;
    modbus_core_process_result_t result;

    g_modbus_process_tick = HAL_GetTick();

    if (response != NULL) {
        *response = NULL;
    }
    if (response_length != NULL) {
        *response_length = 0U;
    }

    if ((frame == NULL) || (length == 0U)) {
        return false;
    }

    /* Never execute destructive system commands from a broadcast frame.  A
     * broadcast write to the command address is deliberately ignored before
     * the generic core can publish it. */
    if ((length >= 4U) && (frame[0] == 0U) &&
        ((frame[1] == 6U) || (frame[1] == 16U))) {
        uint16_t command_start = (uint16_t)(((uint16_t)frame[2] << 8U) |
                                             frame[3]);
        uint16_t command_count = (frame[1] == 6U) ? 1U :
            ((length >= 6U) ? (uint16_t)(((uint16_t)frame[4] << 8U) |
                                        frame[5]) : 0U);
        if ((command_count != 0U) &&
            (command_start <= PYXIS_REG_SYSTEM_COMMAND_ADDR) &&
            ((uint32_t)command_start + command_count >
             PYXIS_REG_SYSTEM_COMMAND_ADDR)) {
            diagnostics.system_command_rejects++;
            return false;
        }
    }

    diagnostics.frames_seen++;
    g_modbus_frames_seen++;
    g_modbus_last_frame_len = (length > MODBUS_ENGINE_TX_SIZE) ?
        MODBUS_ENGINE_TX_SIZE : length;
    memcpy((void *)g_modbus_last_frame, frame, g_modbus_last_frame_len);
    runtime_clear_proposal();
    result = modbus_core_process_frame(
        &core_ctx, frame, length, tx_buffer, &tx_length,
        &need_reply, &meta);
    g_modbus_last_result = (uint32_t)result;

    if ((result != MODBUS_PROC_OK) && (result != MODBUS_PROC_NO_RESPONSE)) {
        runtime_clear_proposal();
        diagnostics.dropped_frames++;
        g_modbus_frames_dropped++;
        return false;
    }

    if (proposal.active) {
        if (!runtime_commit_proposal()) {
            runtime_clear_proposal();
            diagnostics.write_commit_failures++;
            return false;
        }
    }
    runtime_clear_proposal();
    diagnostics.frames_processed++;
    g_modbus_frames_processed++;
    if (need_reply && (tx_length != 0U)) {
        diagnostics.responses_generated++;
        g_modbus_responses_generated++;
        if (response != NULL) {
            *response = tx_buffer;
        }
        if (response_length != NULL) {
            *response_length = tx_length;
        }
    } else {
        diagnostics.no_response_frames++;
        g_modbus_no_response_frames++;
    }

    return need_reply && (tx_length != 0U);
}

const ModbusEngineRuntimeDiagnostics_t *ModbusEngineRuntime_GetDiagnostics(void)
{
    return &diagnostics;
}

uint32_t ModbusEngineRuntime_GetLastResult(void)
{
    return g_modbus_last_result;
}

bool ModbusEngineRuntime_LastFrameMatches(uint8_t slave_id)
{
    return (g_modbus_last_frame_len != 0U) &&
           (g_modbus_last_frame[0] == slave_id);
}

ModbusEngineCommState_t ModbusEngineRuntime_GetCommState(void)
{
    return (ModbusEngineCommState_t)persistence_state;
}

bool ModbusEngineRuntime_GetPendingCommConfig(ModbusConfig_t *out_cfg)
{
    if ((out_cfg == NULL) ||
        (persistence_state != MODBUS_PERSIST_STATE_PENDING)) {
        return false;
    }
    *out_cfg = pending_comm_cfg;
    return true;
}

void ModbusEngineRuntime_MarkCommResponseTransmitted(bool complete)
{
    if (complete) {
        diagnostics.comm_response_tx_complete++;
    }
}

void ModbusEngineRuntime_MarkCommPersistFailure(void)
{
    /* Keep configured RAM and the active boot snapshot intact.  A failed
     * request is terminal for this persistence attempt, but remains visible
     * as FAILED rather than being confused with an active communication
     * transition.  A later accepted write may retry. */
    comm_persistence_pending = false;
    persistence_state = MODBUS_PERSIST_STATE_FAILED;
    diagnostics.persistence_state = (uint32_t)persistence_state;
    g_modbus_persistence_state = (uint32_t)persistence_state;
    persistence_fault = true;
    diagnostics.persistence_fault = 1U;
    g_modbus_persistence_fault = 1U;
    diagnostics.persistence_failures++;
}

void ModbusEngineRuntime_MarkCommPersistSuccess(void)
{
    persistence_state = MODBUS_PERSIST_STATE_IDLE;
    diagnostics.persistence_state = (uint32_t)persistence_state;
    g_modbus_persistence_state = (uint32_t)persistence_state;
    comm_persistence_pending = false;
    persistence_fault = false;
    diagnostics.persistence_successes++;
    diagnostics.persistence_fault = 0U;
    g_modbus_persistence_fault = 0U;
}

void ModbusEngineRuntime_CompleteCommApply(bool applied)
{
    if (applied) {
        ModbusEngineRuntime_MarkCommPersistSuccess();
    } else {
        diagnostics.comm_apply_failures++;
    }
}

void ModbusEngineRuntime_SetActiveCommConfig(const ModbusConfig_t *config)
{
    if (config == NULL) {
        return;
    }
    active_comm_cfg = *config;
    core_ctx.slave_id = config->slave_id;
    diagnostics.restart_required = ModbusEngineRuntime_IsRestartRequired() ? 1U : 0U;
    g_modbus_restart_required = diagnostics.restart_required;
}

bool ModbusEngineRuntime_GetActiveCommConfig(ModbusConfig_t *out_config)
{
    if (out_config == NULL) {
        return false;
    }
    *out_config = active_comm_cfg;
    return true;
}

bool ModbusEngineRuntime_GetConfiguredCommConfig(ModbusConfig_t *out_config)
{
    if (out_config == NULL) {
        return false;
    }
    ModbusModule_GetConfig(out_config);
    return true;
}

bool ModbusEngineRuntime_HasCommPersistencePending(void)
{
    return persistence_state == MODBUS_PERSIST_STATE_PENDING;
}

ModbusPersistenceState_t ModbusEngineRuntime_GetPersistenceState(void)
{
    return persistence_state;
}

bool ModbusEngineRuntime_PersistConfiguredComm(void)
{
    ModbusConfig_t configured;

    if (!comm_persistence_pending ||
        !ModbusEngineRuntime_GetConfiguredCommConfig(&configured)) {
        return true;
    }
    if (!ModbusModule_PersistConfig(&configured)) {
        ModbusEngineRuntime_MarkCommPersistFailure();
        return false;
    }
    ModbusEngineRuntime_MarkCommPersistSuccess();
    return true;
}

void ModbusEngineRuntime_RecordPersistenceResult(bool success)
{
    diagnostics.persistence_requests++;
    if (success) {
        diagnostics.persistence_successes++;
        /* A successful non-communication persistence operation must not
         * erase a previously reported communication persistence fault. */
        if (persistence_state != MODBUS_PERSIST_STATE_FAILED) {
            persistence_fault = false;
            diagnostics.persistence_fault = 0U;
            g_modbus_persistence_fault = 0U;
        }
    } else {
        diagnostics.persistence_failures++;
        persistence_fault = true;
        diagnostics.persistence_fault = 1U;
        g_modbus_persistence_fault = 1U;
    }
}

bool ModbusEngineRuntime_IsRestartRequired(void)
{
    ModbusConfig_t configured;

    if (!ModbusEngineRuntime_GetConfiguredCommConfig(&configured)) {
        return false;
    }
    return (configured.slave_id != active_comm_cfg.slave_id) ||
           (configured.baud_rate != active_comm_cfg.baud_rate) ||
           (configured.parity != active_comm_cfg.parity);
}

bool ModbusEngineRuntime_HasPersistenceFault(void)
{
    return persistence_fault;
}

static bool runtime_build_default_configs(ModbusConfig_t *comm_cfg,
                                          VibConfig_t *vibration_cfg)
{
    ModbusConfig_t current_comm;
    VibConfig_t current_vibration;

    if ((comm_cfg == NULL) || (vibration_cfg == NULL)) {
        return false;
    }
    ModbusModule_GetConfig(&current_comm);
    VibrationModule_GetConfig(&current_vibration);
    if (!ModbusAdapter_BuildConfigFromReader(
            runtime_read_defaults, NULL, &current_comm, comm_cfg) ||
        !VibrationAdapter_BuildConfigFromReader(
            runtime_read_defaults, NULL, &current_vibration, vibration_cfg)) {
        return false;
    }
    return ModbusModule_ValidateConfig(comm_cfg) &&
           VibrationModule_ValidateConfig(vibration_cfg);
}

static bool runtime_apply_factory_defaults(void)
{
    ModbusConfig_t default_comm;
    VibConfig_t default_vibration;
    bool vibration_ok;
    bool comm_ok;

    if (!runtime_build_default_configs(&default_comm, &default_vibration)) {
        return false;
    }

    /* Mark the legacy migration source before publishing a new default
     * generation.  The old verified H1 slot remains the boot fallback if the
     * subsequent default save fails. */
    if (!ModbusPersistence_BlockLegacyConfig() ||
        !ModbusPersistence_SaveFactoryDefaults(&default_comm) ||
        !ModbusEngineMap_RestoreHoldingDefaults()) {
        g_modbus_system_command_last_error = 1U;
        return false;
    }

    taskENTER_CRITICAL();
    vibration_ok = VibrationModule_CommitValidatedRuntimeConfig(
        &default_vibration);
    comm_ok = vibration_ok && ModbusModule_CommitValidatedRuntimeConfig(
        &default_comm);
    taskEXIT_CRITICAL();
    if (!vibration_ok || !comm_ok) {
        return false;
    }

    /* The vibration baseline is runtime/application state, not a Modbus
     * register.  Factory reset must return it to its canonical empty state
     * together with the configuration defaults. */
    VibrationModule_ResetBaseline();

    comm_persistence_pending = false;
    persistence_state = MODBUS_PERSIST_STATE_IDLE;
    diagnostics.persistence_state = (uint32_t)persistence_state;
    g_modbus_persistence_state = (uint32_t)persistence_state;
    diagnostics.restart_required =
        ModbusEngineRuntime_IsRestartRequired() ? 1U : 0U;
    g_modbus_restart_required = diagnostics.restart_required;
    return true;
}

static bool runtime_persist_current_configuration(void)
{
    ModbusConfig_t configured;

    if (!ModbusEngineRuntime_GetConfiguredCommConfig(&configured) ||
        !ModbusModule_PersistConfig(&configured)) {
        g_modbus_system_command_last_error = 2U;
        return false;
    }
    return true;
}

bool ModbusEngineRuntime_ServiceSystemAction(void)
{
    ModbusSystemAction_t action = pending_system_action;
    bool reset_after_action = false;

    if (action == MODBUS_SYSTEM_ACTION_NONE) {
        return true;
    }
    pending_system_action = MODBUS_SYSTEM_ACTION_NONE;
    g_modbus_system_command_state =
        (uint32_t)MODBUS_SYSTEM_ACTION_NONE;

    if (action == MODBUS_SYSTEM_ACTION_RESTART) {
        if (!runtime_persist_current_configuration()) {
            diagnostics.system_command_rejects++;
            return false;
        }
        reset_after_action = true;
    } else if (action == MODBUS_SYSTEM_ACTION_SYSTEM_RESET) {
        VibrationModule_ResetBaseline();
        reset_after_action = true;
    } else if (action == MODBUS_SYSTEM_ACTION_FACTORY_RESET) {
        if (!runtime_apply_factory_defaults()) {
            diagnostics.factory_reset_failures++;
            return false;
        }
        diagnostics.factory_reset_successes++;
        reset_after_action = true;
    } else if (action == MODBUS_SYSTEM_ACTION_CAPTURE_BASELINE) {
        if (!VibrationModule_SaveBaseline()) {
            diagnostics.system_command_rejects++;
            g_modbus_system_command_last_error = 4U;
            return false;
        }
    } else if (action == MODBUS_SYSTEM_ACTION_RESET_BASELINE) {
        VibrationModule_ResetBaseline();
    }

#if MODBUS_G_TEST_HOOKS
    /* Host tests observe the successful post-TX action without resetting the
     * test process. */
    return true;
#else
    if (reset_after_action) {
        NVIC_SystemReset();
    }
    return true;
#endif
}

void ModbusEngineRuntime_CancelSystemAction(void)
{
    if (pending_system_action != MODBUS_SYSTEM_ACTION_NONE) {
        diagnostics.system_command_rejects++;
        if (pending_system_action == MODBUS_SYSTEM_ACTION_FACTORY_RESET) {
            diagnostics.factory_reset_failures++;
        }
        g_modbus_system_command_last_error = 3U;
    }
    pending_system_action = MODBUS_SYSTEM_ACTION_NONE;
    g_modbus_system_command_state =
        (uint32_t)MODBUS_SYSTEM_ACTION_NONE;
}

ModbusSystemAction_t ModbusEngineRuntime_GetPendingSystemAction(void)
{
    return pending_system_action;
}
