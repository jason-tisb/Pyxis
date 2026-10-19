#include "modbus_engine_runtime.h"

#include <string.h>

#include "modbus_core.h"
#include "modbus_engine_config.h"
#include "modbus_engine_map.h"
#include "modbus_module.h"
#include "modbus_register_map.h"
#include "user_mb_app.h"

#define MODBUS_ENGINE_TX_SIZE 256U

static modbus_core_ctx_t core_ctx;
static uint8_t tx_buffer[MODBUS_ENGINE_TX_SIZE];
static ModbusEngineRuntimeDiagnostics_t diagnostics;
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

static modbus_core_access_result_t runtime_read_reg(
    uint16_t addr, modbus_core_reg_type_t type,
    modbus_core_reg_info_t *out, uint16_t *cursor_hint)
{
    if (out == NULL) {
        return MODBUS_ACCESS_INVALID_CONTEXT;
    }

    memset(out, 0, sizeof(*out));
    if (cursor_hint != NULL) {
        *cursor_hint = addr;
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
    (void)value;

    if ((type != MODBUS_REG_HOLDING) || (current == NULL)) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
    return ModbusEngineMap_ValidateWrite(type, addr, value);
}

static modbus_core_access_result_t runtime_validate_multiple(
    uint16_t start_addr, const uint16_t *values, uint16_t quantity)
{
    return ModbusEngineMap_ValidateRange(MODBUS_REG_HOLDING, start_addr,
                                         values, quantity);
}

static modbus_core_access_result_t runtime_write_reg(
    uint16_t addr, modbus_core_reg_type_t type, uint16_t value)
{
    if ((type != MODBUS_REG_HOLDING) ||
        (addr >= MODBUS_ENGINE_HOLDING_STORAGE_SIZE)) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
#if MODBUS_ENGINE_ACTIVE
    ModbusEngineMap_HoldingStorage()[addr] = value;
#else
    (void)value;
#endif
    return MODBUS_ACCESS_OK;
}

static modbus_core_access_result_t runtime_write_multiple(
    uint16_t start_addr, const uint16_t *values, uint16_t quantity)
{
    uint16_t i;

    if ((values == NULL) || (quantity == 0U) ||
        (start_addr >= MODBUS_ENGINE_HOLDING_STORAGE_SIZE) ||
        (quantity > (MODBUS_ENGINE_HOLDING_STORAGE_SIZE - start_addr))) {
        return MODBUS_ACCESS_ILLEGAL_ADDRESS;
    }
#if MODBUS_ENGINE_ACTIVE
    for (i = 0U; i < quantity; ++i) {
        ModbusEngineMap_HoldingStorage()[start_addr + i] = values[i];
    }
#else
    (void)i;
#endif
    return MODBUS_ACCESS_OK;
}

void ModbusEngineRuntime_Init(void)
{
    ModbusConfig_t config;

    memset(&core_ctx, 0, sizeof(core_ctx));
    memset(&diagnostics, 0, sizeof(diagnostics));
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
    core_ctx.slave_id = slave_id;
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

    diagnostics.frames_seen++;
    g_modbus_frames_seen++;
    g_modbus_last_frame_len = (length > MODBUS_ENGINE_TX_SIZE) ?
        MODBUS_ENGINE_TX_SIZE : length;
    memcpy((void *)g_modbus_last_frame, frame, g_modbus_last_frame_len);
    result = modbus_core_process_frame(
        &core_ctx, frame, length, tx_buffer, &tx_length,
        &need_reply, &meta);
    g_modbus_last_result = (uint32_t)result;

    if (result != MODBUS_PROC_OK) {
        diagnostics.dropped_frames++;
        g_modbus_frames_dropped++;
        return false;
    }

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
