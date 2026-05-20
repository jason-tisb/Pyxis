#include "system_validator.h"

#include "modbus_app_adapter.h"
#include "modbus_register_map.h"
#include "modbus_module.h"
#include "vibration_modbus_adapter.h"

static float decode_ratio_x100_u16(uint16_t value)
{
    return (float)value / 100.0f;
}

static float decode_mm_x100_u16(uint16_t value)
{
    return (float)value / 100.0f;
}

static float decode_deg_x10_u16(uint16_t value)
{
    return (float)value / 10.0f;
}

static bool is_vibration_config_reg(USHORT reg_addr)
{
    switch (reg_addr) {
        case MB_REG_MACHINE_PROFILE:
        case MB_REG_GEAR_RATIO:
        case MB_REG_GEAR_TEETH_NUMBER:
        case MB_REG_BASE_FREQ_HZ:
        case MB_REG_BEARING_BALL_COUNT:
        case MB_REG_BEARING_BALL_SIZE:
        case MB_REG_BEARING_DIAMETER:
        case MB_REG_BEARING_ALPHA_ANGLE:
        case MB_REG_FILTER_HIGH_PASS:
        case MB_REG_FILTER_LOW_PASS:
            return true;
        default:
            return false;
    }
}

static bool is_vibration_command_reg(USHORT reg_addr)
{
    return (reg_addr == MB_REG_BASELINE_COMMAND) || (reg_addr == MB_REG_REFRESH);
}

static bool is_modbus_comm_reg(USHORT reg_addr)
{
    return (reg_addr == MB_REG_SLAVE_ID) || (reg_addr == MB_REG_BAUDRATE) || (reg_addr == MB_REG_PARITY);
}

static bool validate_modbus_comm_write(USHORT reg_addr, USHORT value)
{
    ModbusConfig_t candidate;
    ModbusModule_GetConfig(&candidate);

    switch (reg_addr) {
        case MB_REG_SLAVE_ID:
            candidate.slave_id = (uint8_t)value;
            break;
        case MB_REG_BAUDRATE: {
            uint32_t baud_rate = 0U;
            if (!ModbusAdapter_CodeToBaud(value, &baud_rate)) {
                return false;
            }
            candidate.baud_rate = baud_rate;
        } break;
        case MB_REG_PARITY:
            candidate.parity = (uint8_t)value;
            break;
        default:
            break;
    }

    return ModbusModule_ValidateConfig(&candidate);
}

static bool validate_vibration_raw_write(USHORT reg_addr, USHORT value)
{
    VibConfig_t candidate;

    /* Start from current runtime config so cross-field constraints apply. */
    VibrationModule_GetConfig(&candidate);

    switch (reg_addr) {
        case MB_REG_MACHINE_PROFILE:
            candidate.machine_profile = (uint8_t)value;
            break;
        case MB_REG_GEAR_RATIO:
            candidate.gear_ratio = decode_ratio_x100_u16(value);
            break;
        case MB_REG_GEAR_TEETH_NUMBER:
            candidate.gear_teeth_number = value;
            break;
        case MB_REG_BASE_FREQ_HZ:
            /* Stored as Hz x100 in motor_speed_rpm field. */
            candidate.motor_speed_rpm = value;
            break;
        case MB_REG_BEARING_BALL_COUNT:
            candidate.bearing_ball_count = value;
            break;
        case MB_REG_BEARING_BALL_SIZE:
            candidate.bearing_ball_size_mm = decode_mm_x100_u16(value);
            break;
        case MB_REG_BEARING_DIAMETER:
            candidate.bearing_pitch_diameter_mm = decode_mm_x100_u16(value);
            break;
        case MB_REG_BEARING_ALPHA_ANGLE:
            candidate.bearing_alpha_angle_deg = decode_deg_x10_u16(value);
            break;
        case MB_REG_FILTER_HIGH_PASS:
            candidate.filter_high_pass_hz = value;
            break;
        case MB_REG_FILTER_LOW_PASS:
            candidate.filter_low_pass_hz = value;
            break;
        default:
            break;
    }

    return VibrationModule_ValidateConfig(&candidate);
}

bool SystemValidator_ModbusWriteValidateHoldingRegister(USHORT reg_addr, USHORT value)
{
    if (is_modbus_comm_reg(reg_addr)) {
        return validate_modbus_comm_write(reg_addr, value);
    }

    if (is_vibration_config_reg(reg_addr)) {
        if (!VibrationAdapter_IsValidRawConfigValue(reg_addr, value)) {
            return false;
        }
        /* Reject writes that would make the full vibration config invalid. */
        return validate_vibration_raw_write(reg_addr, value);
    }

    if (is_vibration_command_reg(reg_addr)) {
        return VibrationAdapter_IsValidRawConfigValue(reg_addr, value);
    }

    if (reg_addr >= MB_REG_REMAPPING_ADDR_START && reg_addr <= MB_REG_REMAPPING_ADDR_END) {
        return true;
    }

    return true;
}
