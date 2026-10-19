#include "modbus_autobaud_scan.h"

#include "main.h"
#include "modbus_autobaud_diag.h"
#include "modbus_engine_config.h"
#include "modbus_engine_port.h"
#include "modbus_engine_tx.h"

#if MODBUS_ENGINE_ACTIVE

typedef struct {
    uint32_t baud_rate;
    modbus_autobaud_framing_t framing_mode;
} modbus_autobaud_profile_t;

static const modbus_autobaud_profile_t profiles[MODBUS_AUTOBAUD_PROFILE_COUNT] = {
    { 19200U, MODBUS_AUTOBAUD_FRAMING_8E1 },
    {  9600U, MODBUS_AUTOBAUD_FRAMING_8E1 },
    { 38400U, MODBUS_AUTOBAUD_FRAMING_8E1 },
    { 57600U, MODBUS_AUTOBAUD_FRAMING_8E1 },
    {115200U, MODBUS_AUTOBAUD_FRAMING_8E1 },
    { 19200U, MODBUS_AUTOBAUD_FRAMING_8N2 },
    {  9600U, MODBUS_AUTOBAUD_FRAMING_8N2 },
    { 38400U, MODBUS_AUTOBAUD_FRAMING_8N2 },
    { 57600U, MODBUS_AUTOBAUD_FRAMING_8N2 },
    {115200U, MODBUS_AUTOBAUD_FRAMING_8N2 },
    { 19200U, MODBUS_AUTOBAUD_FRAMING_8O1 },
    {  9600U, MODBUS_AUTOBAUD_FRAMING_8O1 },
    { 38400U, MODBUS_AUTOBAUD_FRAMING_8O1 },
    { 57600U, MODBUS_AUTOBAUD_FRAMING_8O1 },
    {115200U, MODBUS_AUTOBAUD_FRAMING_8O1 },
    { 19200U, MODBUS_AUTOBAUD_FRAMING_8N1 },
    {  9600U, MODBUS_AUTOBAUD_FRAMING_8N1 },
    { 38400U, MODBUS_AUTOBAUD_FRAMING_8N1 },
    { 57600U, MODBUS_AUTOBAUD_FRAMING_8N1 },
    {115200U, MODBUS_AUTOBAUD_FRAMING_8N1 }
};

static modbus_autobaud_scan_result_t result;
static uint8_t running;
static uint8_t started_once;
static uint8_t profile_index;
static uint32_t started_tick;
static uint32_t candidate_started_tick;
static uint32_t previous_baud;
static uint8_t previous_data_bits;
static uint8_t previous_parity;
static uint32_t previous_stop_bits;
static uint32_t visited_mask;

static uint32_t elapsed_ms(void)
{
    return (uint32_t)(HAL_GetTick() - started_tick);
}

static uint32_t remaining_ms(void)
{
    uint32_t elapsed = elapsed_ms();
    return (elapsed >= result.budget_ms) ? 0U : result.budget_ms - elapsed;
}

static void update_result_time(void)
{
    result.elapsed_ms = elapsed_ms();
}

static bool restore_previous(void)
{
    return ModbusEnginePort_ReconfigureUart(
        previous_baud, previous_data_bits, previous_parity,
        previous_stop_bits);
}

static bool start_profile(uint8_t index)
{
    uint32_t remaining = remaining_ms();
    uint32_t dwell = (remaining < MODBUS_AUTOBAUD_PROFILE_DWELL_MS) ?
                     remaining : MODBUS_AUTOBAUD_PROFILE_DWELL_MS;

    if ((index >= MODBUS_AUTOBAUD_PROFILE_COUNT) || (dwell == 0U)) {
        return false;
    }
    if (!ModbusAutobaudValidate_StartWithTimeout(
            profiles[index].baud_rate, profiles[index].framing_mode, dwell)) {
        return false;
    }
    profile_index = index;
    candidate_started_tick = HAL_GetTick();
    result.profile_index = (uint8_t)(index + 1U);
    result.baud_rate = profiles[index].baud_rate;
    result.framing_mode = profiles[index].framing_mode;
    result.profiles_attempted++;
    if ((visited_mask & (1UL << index)) == 0U) {
        visited_mask |= (1UL << index);
        result.unique_profiles_visited++;
    }
    return true;
}

bool ModbusAutobaudScan_Start(uint32_t budget_ms)
{
    if ((running != 0U) || (budget_ms == 0U) || ModbusEngineTx_IsBusy() ||
        !ModbusEnginePort_GetUartConfig(&previous_baud, &previous_data_bits,
                                        &previous_parity, &previous_stop_bits)) {
        return false;
    }

    result.baud_rate = 0U;
    result.framing_mode = MODBUS_AUTOBAUD_FRAMING_8E1;
    result.profile_index = 0U;
    result.elapsed_ms = 0U;
    result.profiles_attempted = 0U;
    result.unique_profiles_visited = 0U;
    result.completed_cycles = 0U;
    result.budget_ms = budget_ms;
    result.locked = 0U;
    result.recovery_failed = 0U;
    visited_mask = 0U;
    profile_index = 0U;
    started_tick = HAL_GetTick();
    started_once = 1U;
    running = 1U;

    if (!start_profile(0U)) {
        running = 0U;
        result.status = MODBUS_AUTOBAUD_SCAN_ERROR;
        result.recovery_failed = restore_previous() ? 0U : 1U;
        return false;
    }
    result.status = MODBUS_AUTOBAUD_SCAN_RUNNING;
    return true;
}

modbus_autobaud_scan_status_t ModbusAutobaudScan_Service(void)
{
    modbus_autobaud_validation_status_t validation;
    uint8_t next;

    if (running == 0U) {
        return result.status;
    }
    update_result_time();
    validation = ModbusAutobaudValidate_Service();
    if (validation == MODBUS_AUTOBAUD_VALIDATE_VALID) {
        result.status = MODBUS_AUTOBAUD_SCAN_LOCKED;
        result.locked = 1U;
        running = 0U;
        return result.status;
    }
    if (validation == MODBUS_AUTOBAUD_VALIDATE_CONFIG_ERROR ||
        validation == MODBUS_AUTOBAUD_VALIDATE_RX_ERROR) {
        running = 0U;
        result.status = MODBUS_AUTOBAUD_SCAN_ERROR;
        result.recovery_failed = restore_previous() ? 0U : 1U;
        return result.status;
    }

    if (remaining_ms() == 0U) {
        ModbusAutobaudValidate_Stop();
        running = 0U;
        result.status = MODBUS_AUTOBAUD_SCAN_TIMEOUT;
        result.recovery_failed = restore_previous() ? 0U : 1U;
        return result.status;
    }

    if ((validation == MODBUS_AUTOBAUD_VALIDATE_TIMEOUT) ||
        ((uint32_t)(HAL_GetTick() - candidate_started_tick) >=
         MODBUS_AUTOBAUD_PROFILE_DWELL_MS)) {
        if (validation == MODBUS_AUTOBAUD_VALIDATE_TIMEOUT) {
            ModbusAutobaudDiag_RecordValidationTimeout();
        }
        next = (uint8_t)((profile_index + 1U) % MODBUS_AUTOBAUD_PROFILE_COUNT);
        if (next == 0U) {
            result.completed_cycles++;
        }
        if (!start_profile(next)) {
            running = 0U;
            result.status = MODBUS_AUTOBAUD_SCAN_TIMEOUT;
            result.recovery_failed = restore_previous() ? 0U : 1U;
            return result.status;
        }
    }
    return result.status;
}

void ModbusAutobaudScan_Stop(void)
{
    bool recovered = true;

    if (started_once == 0U) {
        return;
    }
    if (running != 0U) {
        ModbusAutobaudValidate_Stop();
        running = 0U;
    }
    if (result.status != MODBUS_AUTOBAUD_SCAN_LOCKED) {
        recovered = restore_previous();
    }
    if (!recovered) {
        result.recovery_failed = 1U;
        result.status = MODBUS_AUTOBAUD_SCAN_ERROR;
    } else if (result.status == MODBUS_AUTOBAUD_SCAN_RUNNING) {
        result.status = MODBUS_AUTOBAUD_SCAN_IDLE;
    }
    update_result_time();
    started_once = 0U;
}

bool ModbusAutobaudScan_GetResult(modbus_autobaud_scan_result_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = result;
    return true;
}

#else

bool ModbusAutobaudScan_Start(uint32_t budget_ms)
{
    (void)budget_ms;
    return false;
}

modbus_autobaud_scan_status_t ModbusAutobaudScan_Service(void)
{
    return MODBUS_AUTOBAUD_SCAN_ERROR;
}

void ModbusAutobaudScan_Stop(void)
{
}

bool ModbusAutobaudScan_GetResult(modbus_autobaud_scan_result_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = (modbus_autobaud_scan_result_t){
        MODBUS_AUTOBAUD_SCAN_ERROR, 0U, MODBUS_AUTOBAUD_FRAMING_8E1,
        0U, 0U, 0U, 0U, 0U, 0U, 1U
    };
    return true;
}

#endif
