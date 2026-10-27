#include "vibration_modbus_adapter.h"

#include <stddef.h>

bool VibrationAdapter_IsValidMachineProfile(uint16_t value)
{
    const modbus_map_entry_t *entry = ModbusMap_Find(
        MODBUS_MAP_REGISTER_HOLDING, MB_REG_MACHINE_PROFILE);

    return (entry != NULL) && ModbusMap_ValidateValue(entry, value);
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
    const modbus_map_entry_t *entry = ModbusMap_Find(
        MODBUS_MAP_REGISTER_HOLDING, MB_REG_BASELINE_COMMAND);

    return (entry != NULL) && ModbusMap_ValidateValue(entry, value);
}

bool VibrationAdapter_IsValidRawConfigValue(uint16_t reg_addr, uint16_t value)
{
    const modbus_map_entry_t *entry = ModbusMap_Find(
        MODBUS_MAP_REGISTER_HOLDING, reg_addr);

    return (entry == NULL) ? false : ModbusMap_ValidateValue(entry, value);
}

bool VibrationAdapter_BuildConfigFromReader(
    VibrationAdapterRegisterReader_t reader, void *user,
    const VibConfig_t *current_cfg, VibConfig_t *out_cfg)
{
    uint16_t machine_profile;
    uint16_t gear_ratio;
    uint16_t gear_teeth;
    uint16_t motor_speed;
    uint16_t ball_count;
    uint16_t ball_size;
    uint16_t bearing_diameter;
    uint16_t alpha_angle;
    uint16_t filter_high_pass;
    uint16_t filter_low_pass;
    const uint16_t addresses[] = {
        MB_REG_MACHINE_PROFILE, MB_REG_GEAR_RATIO, MB_REG_GEAR_TEETH_NUMBER,
        MB_REG_BASE_FREQ_HZ, MB_REG_BEARING_BALL_COUNT,
        MB_REG_BEARING_BALL_SIZE, MB_REG_BEARING_DIAMETER,
        MB_REG_BEARING_ALPHA_ANGLE, MB_REG_FILTER_HIGH_PASS,
        MB_REG_FILTER_LOW_PASS
    };
    uint16_t *values[] = {
        &machine_profile, &gear_ratio, &gear_teeth, &motor_speed,
        &ball_count, &ball_size, &bearing_diameter, &alpha_angle,
        &filter_high_pass, &filter_low_pass
    };
    size_t i;

    if ((reader == NULL) || (current_cfg == NULL) || (out_cfg == NULL)) {
        return false;
    }
    for (i = 0U; i < (sizeof(addresses) / sizeof(addresses[0])); ++i) {
        if (!reader(user, addresses[i], values[i]) ||
            !VibrationAdapter_IsValidRawConfigValue(addresses[i], *values[i])) {
            return false;
        }
    }

    *out_cfg = *current_cfg;
    out_cfg->machine_profile = (uint8_t)machine_profile;
    out_cfg->gear_ratio = decode_ratio_x100(gear_ratio);
    out_cfg->gear_teeth_number = gear_teeth;
    out_cfg->motor_speed_rpm = motor_speed;
    out_cfg->bearing_ball_count = ball_count;
    out_cfg->bearing_ball_size_mm = decode_mm_x100(ball_size);
    out_cfg->bearing_pitch_diameter_mm = decode_mm_x100(bearing_diameter);
    out_cfg->bearing_alpha_angle_deg = decode_deg_x10(alpha_angle);
    out_cfg->filter_high_pass_hz = filter_high_pass;
    out_cfg->filter_low_pass_hz = filter_low_pass;
    return VibrationModule_ValidateConfig(out_cfg);
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
