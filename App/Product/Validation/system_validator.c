#include "system_validator.h"

#include "modbus_app_adapter.h"
#include "modbus_register_map.h"
#include "vibration_modbus_adapter.h"

bool SystemValidator_ModbusWriteValidateHoldingRegister(USHORT reg_addr, USHORT value)
{
    switch (reg_addr) {
        case MB_REG_SLAVE_ID:
            return ModbusAdapter_IsValidSlaveId(value);
        case MB_REG_BAUDRATE:
            return ModbusAdapter_IsValidBaudCode(value);
        case MB_REG_PARITY:
            return ModbusAdapter_IsValidParityCode(value);
        case MB_REG_MACHINE_PROFILE:
        case MB_REG_BASELINE_COMMAND:
        case MB_REG_GEAR_RATIO:
        case MB_REG_GEAR_TEETH_NUMBER:
        case MB_REG_MOTOR_SPEED:
        case MB_REG_BEARING_BALL_COUNT:
        case MB_REG_BEARING_BALL_SIZE:
        case MB_REG_BEARING_DIAMETER:
        case MB_REG_BEARING_ALPHA_ANGLE:
        case MB_REG_FILTER_HIGH_PASS:
        case MB_REG_FILTER_LOW_PASS:
            return VibrationAdapter_IsValidRawConfigValue(reg_addr, value);
        default:
            return true;
    }
}
