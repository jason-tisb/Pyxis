#include "vibration_modbus_adapter.h"

bool VibrationAdapter_IsValidMachineProfile(uint16_t value)
{
    return value <= (uint16_t)VIB_MACHINE_PROFILE_GEARBOX;
}

static float decode_ratio_x100(uint16_t value)
{
    return (float)value / 100.0f;
}

static float decode_mm_x100(uint16_t value)
{
    return (float)value / 100.0f;
}

static float decode_deg_x10(uint16_t value)
{
    return (float)value / 10.0f;
}

static uint16_t encode_ratio_x100(float value)
{
    return (uint16_t)(value * 100.0f);
}

static uint16_t encode_mm_x100(float value)
{
    return (uint16_t)(value * 100.0f);
}

static uint16_t encode_deg_x10(float value)
{
    return (uint16_t)(value * 10.0f);
}

bool VibrationAdapter_IsValidCommand(uint16_t value)
{
    return value <= (uint16_t)VIBRATION_ADAPTER_CMD_RESET_BASELINE;
}

bool VibrationAdapter_IsValidRawConfigValue(uint16_t reg_addr, uint16_t value)
{
    switch (reg_addr) {
        case MB_REG_MACHINE_PROFILE:
            return VibrationAdapter_IsValidMachineProfile(value);
        case MB_REG_GEAR_RATIO:
            return (value >= 1U) && (value <= 5000U);
        case MB_REG_GEAR_TEETH_NUMBER:
            return (value > 0U) && (value <= 1024U);
        case MB_REG_BASE_FREQ_HZ:
            return (value >= 1U) && (value <= 65535U);
        case MB_REG_BEARING_BALL_COUNT:
            return (value >= 3U) && (value <= 32U);
        case MB_REG_BEARING_BALL_SIZE:
            return (value >= 1U) && (value <= 5000U);
        case MB_REG_BEARING_DIAMETER:
            return (value >= 1U) && (value <= 7200U);
        case MB_REG_BEARING_ALPHA_ANGLE:
            return value <= 900U;
        case MB_REG_FILTER_HIGH_PASS:
        case MB_REG_FILTER_LOW_PASS:
            return (value >= 1U) && (value <= 8000U);
        case MB_REG_REFRESH:
            return value <= 1U;
        case MB_REG_BASELINE_COMMAND:
            return VibrationAdapter_IsValidCommand(value);
        default:
            return true;
    }
}

void VibrationAdapter_InitMap(const VibrationAdapterContext_t *ctx, const VibConfig_t *current_cfg)
{
    if (ctx == 0 || current_cfg == 0 || ctx->holding_regs == 0 || ctx->reg_count <= MB_REG_GEAR_TEETH_NUMBER) {
        return;
    }

    ctx->holding_regs[MB_REG_MACHINE_PROFILE] = (USHORT)current_cfg->machine_profile;
    ctx->holding_regs[MB_REG_GEAR_RATIO] = encode_ratio_x100(current_cfg->gear_ratio);
    ctx->holding_regs[MB_REG_GEAR_TEETH_NUMBER] = (USHORT)current_cfg->gear_teeth_number;
    ctx->holding_regs[MB_REG_BASE_FREQ_HZ] = current_cfg->motor_speed_rpm;
    ctx->holding_regs[MB_REG_BEARING_BALL_COUNT] = current_cfg->bearing_ball_count;
    ctx->holding_regs[MB_REG_BEARING_BALL_SIZE] = encode_mm_x100(current_cfg->bearing_ball_size_mm);
    ctx->holding_regs[MB_REG_BEARING_DIAMETER] = encode_mm_x100(current_cfg->bearing_pitch_diameter_mm);
    ctx->holding_regs[MB_REG_BEARING_ALPHA_ANGLE] = encode_deg_x10(current_cfg->bearing_alpha_angle_deg);
    ctx->holding_regs[MB_REG_FILTER_HIGH_PASS] = current_cfg->filter_high_pass_hz;
    ctx->holding_regs[MB_REG_FILTER_LOW_PASS] = current_cfg->filter_low_pass_hz;
    ctx->holding_regs[MB_REG_BASELINE_COMMAND] = (USHORT)VIBRATION_ADAPTER_CMD_NONE;
}

bool VibrationAdapter_CheckConfigUpdate(const VibrationAdapterContext_t *ctx,
                                        const VibConfig_t *current_cfg,
                                        VibConfig_t *out_new_cfg)
{
    VibConfig_t next_cfg;
    uint16_t req_machine_profile;
    uint16_t req_gear_ratio;
    uint16_t req_gear_teeth;
    uint16_t req_motor_speed;
    uint16_t req_ball_count;
    uint16_t req_ball_size;
    uint16_t req_bearing_diameter;
    uint16_t req_alpha_angle;
    uint16_t req_filter_high_pass;
    uint16_t req_filter_low_pass;
    bool changed = false;

    if (ctx == 0 || current_cfg == 0 || out_new_cfg == 0 || ctx->holding_regs == 0 || ctx->reg_count <= MB_REG_GEAR_TEETH_NUMBER) {
        return false;
    }

    next_cfg = *current_cfg;
    req_machine_profile = ctx->holding_regs[MB_REG_MACHINE_PROFILE];
    req_gear_ratio = ctx->holding_regs[MB_REG_GEAR_RATIO];
    req_gear_teeth = ctx->holding_regs[MB_REG_GEAR_TEETH_NUMBER];
    req_motor_speed = ctx->holding_regs[MB_REG_BASE_FREQ_HZ];
    req_ball_count = ctx->holding_regs[MB_REG_BEARING_BALL_COUNT];
    req_ball_size = ctx->holding_regs[MB_REG_BEARING_BALL_SIZE];
    req_bearing_diameter = ctx->holding_regs[MB_REG_BEARING_DIAMETER];
    req_alpha_angle = ctx->holding_regs[MB_REG_BEARING_ALPHA_ANGLE];
    req_filter_high_pass = ctx->holding_regs[MB_REG_FILTER_HIGH_PASS];
    req_filter_low_pass = ctx->holding_regs[MB_REG_FILTER_LOW_PASS];

    if (req_machine_profile != current_cfg->machine_profile) {
        if (VibrationAdapter_IsValidMachineProfile(req_machine_profile)) {
            next_cfg.machine_profile = (uint8_t)req_machine_profile;
            changed = true;
        } else {
            ctx->holding_regs[MB_REG_MACHINE_PROFILE] = (USHORT)current_cfg->machine_profile;
        }
    }

    if (req_gear_ratio != encode_ratio_x100(current_cfg->gear_ratio)) {
        next_cfg.gear_ratio = decode_ratio_x100(req_gear_ratio);
        changed = true;
    }

    if (req_gear_teeth != current_cfg->gear_teeth_number) {
        if (VibrationAdapter_IsValidRawConfigValue(MB_REG_GEAR_TEETH_NUMBER, req_gear_teeth)) {
            next_cfg.gear_teeth_number = req_gear_teeth;
            changed = true;
        } else {
            ctx->holding_regs[MB_REG_GEAR_TEETH_NUMBER] = (USHORT)current_cfg->gear_teeth_number;
        }
    }

    if (req_motor_speed != current_cfg->motor_speed_rpm) {
        next_cfg.motor_speed_rpm = req_motor_speed;
        changed = true;
    }

    if (req_ball_count != current_cfg->bearing_ball_count) {
        next_cfg.bearing_ball_count = req_ball_count;
        changed = true;
    }

    if (req_ball_size != encode_mm_x100(current_cfg->bearing_ball_size_mm)) {
        next_cfg.bearing_ball_size_mm = decode_mm_x100(req_ball_size);
        changed = true;
    }

    if (req_bearing_diameter != encode_mm_x100(current_cfg->bearing_pitch_diameter_mm)) {
        next_cfg.bearing_pitch_diameter_mm = decode_mm_x100(req_bearing_diameter);
        changed = true;
    }

    if (req_alpha_angle != encode_deg_x10(current_cfg->bearing_alpha_angle_deg)) {
        next_cfg.bearing_alpha_angle_deg = decode_deg_x10(req_alpha_angle);
        changed = true;
    }

    if (req_filter_high_pass != current_cfg->filter_high_pass_hz) {
        next_cfg.filter_high_pass_hz = req_filter_high_pass;
        changed = true;
    }

    if (req_filter_low_pass != current_cfg->filter_low_pass_hz) {
        next_cfg.filter_low_pass_hz = req_filter_low_pass;
        changed = true;
    }

    if (changed) {
        if (VibrationModule_ValidateConfig(&next_cfg)) {
            *out_new_cfg = next_cfg;
        } else {
            VibrationAdapter_InitMap(ctx, current_cfg);
            changed = false;
        }
    }

    return changed;
}

bool VibrationAdapter_ReadCommand(const VibrationAdapterContext_t *ctx,
                                  VibrationAdapterCommand_t *out_cmd)
{
    uint16_t raw_cmd;

    if (ctx == 0 || out_cmd == 0 || ctx->holding_regs == 0 || ctx->reg_count <= MB_REG_BASELINE_COMMAND) {
        return false;
    }

    raw_cmd = ctx->holding_regs[MB_REG_BASELINE_COMMAND];
    if (!VibrationAdapter_IsValidCommand(raw_cmd) || raw_cmd == 0U) {
        return false;
    }

    *out_cmd = (VibrationAdapterCommand_t)raw_cmd;
    return true;
}

void VibrationAdapter_ClearCommand(const VibrationAdapterContext_t *ctx)
{
    if (ctx == 0 || ctx->holding_regs == 0 || ctx->reg_count <= MB_REG_BASELINE_COMMAND) {
        return;
    }

    ctx->holding_regs[MB_REG_BASELINE_COMMAND] = (USHORT)VIBRATION_ADAPTER_CMD_NONE;
}
