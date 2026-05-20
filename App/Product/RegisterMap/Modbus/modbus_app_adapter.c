#include "modbus_app_adapter.h"

enum {
    REG_ADDR_SLAVE_ID = MB_REG_SLAVE_ID,
    REG_ADDR_BAUD = MB_REG_BAUDRATE
};

uint16_t ModbusAdapter_BaudToCode(uint32_t baud_rate)
{
    switch (baud_rate) {
        case 0U: return 0U;
        case 9600U: return 1U;
        case 19200U: return 2U;
        case 38400U: return 3U;
        case 115200U: return 4U;
        default: return 4U;
    }
}

static bool ModbusAdapter_IsValidCommValue(uint16_t reg_addr, uint16_t value)
{
    switch (reg_addr) {
        case MB_REG_SLAVE_ID:
            return (value > 0U) && (value <= MB_ADDRESS_MAX);
        case MB_REG_BAUDRATE:
            return ModbusAdapter_CodeToBaud(value, NULL);
        case MB_REG_PARITY:
            return value <= 3U;
        default:
            return true;
    }
}

bool ModbusAdapter_CodeToBaud(uint16_t baud_code, uint32_t *baud_rate)
{
    switch (baud_code) {
        case 0U:
            if (baud_rate != NULL) {
                *baud_rate = 0U;
            }
            return true;
        case 1U:
            if (baud_rate != NULL) {
                *baud_rate = 9600U;
            }
            return true;
        case 2U:
            if (baud_rate != NULL) {
                *baud_rate = 19200U;
            }
            return true;
        case 3U:
            if (baud_rate != NULL) {
                *baud_rate = 38400U;
            }
            return true;
        case 4U:
            if (baud_rate != NULL) {
                *baud_rate = 115200U;
            }
            return true;
        default: return false;
    }
}

bool ModbusAdapter_IsValidBaudCode(uint16_t baud_code)
{
    return ModbusAdapter_IsValidCommValue(MB_REG_BAUDRATE, baud_code);
}

bool ModbusAdapter_IsValidSlaveId(uint16_t value)
{
    return ModbusAdapter_IsValidCommValue(MB_REG_SLAVE_ID, value);
}

bool ModbusAdapter_IsValidParityCode(uint16_t value)
{
    return ModbusAdapter_IsValidCommValue(MB_REG_PARITY, value);
}

eMBParity ModbusAdapter_ParityFromCode(uint16_t parity_code)
{
    switch (parity_code) {
        case 0U: return MB_PAR_EVEN;
        case 1U: return MB_PAR_ODD;
        case 2U: return MB_PAR_NONE;
        case 3U: return MB_PAR_NONE;
        default: return MB_PAR_NONE;
    }
}

uint16_t ModbusAdapter_ParityToCode(uint8_t parity_code)
{
    return (parity_code <= 3U) ? parity_code : 2U;
}

void ModbusAdapter_InitMap(const ModbusAdapterContext_t *ctx, const ModbusConfig_t *current_cfg)
{
    if (ctx == NULL || current_cfg == NULL || ctx->holding_regs == NULL || ctx->reg_count <= REG_ADDR_BAUD) {
        return;
    }

    ctx->holding_regs[REG_ADDR_SLAVE_ID] = current_cfg->slave_id;
    ctx->holding_regs[REG_ADDR_BAUD] = ModbusAdapter_BaudToCode(current_cfg->baud_rate);
    ctx->holding_regs[MB_REG_PARITY] = ModbusAdapter_ParityToCode(current_cfg->parity);
}

bool ModbusAdapter_CheckConfigUpdate(const ModbusAdapterContext_t *ctx,
                                     const ModbusConfig_t *current_cfg,
                                     ModbusConfig_t *out_new_cfg)
{
    ModbusConfig_t next_cfg;
    bool changed = false;
    uint16_t req_slave_id;
    uint32_t req_baud;
    uint16_t req_baud_code;
    uint16_t req_parity_code;

    if (ctx == NULL || current_cfg == NULL || out_new_cfg == NULL ||
        ctx->holding_regs == NULL || ctx->reg_count <= MB_REG_PARITY) {
        return false;
    }

    next_cfg = *current_cfg;
    req_slave_id = ctx->holding_regs[REG_ADDR_SLAVE_ID];
    req_baud_code = ctx->holding_regs[REG_ADDR_BAUD];
    req_parity_code = ctx->holding_regs[MB_REG_PARITY];

    if (req_slave_id != current_cfg->slave_id) {
        if (ModbusAdapter_IsValidSlaveId(req_slave_id)) {
            next_cfg.slave_id = (uint8_t)req_slave_id;
            changed = true;
        } else {
            ctx->holding_regs[REG_ADDR_SLAVE_ID] = current_cfg->slave_id;
        }
    }

    if (ModbusAdapter_CodeToBaud(req_baud_code, &req_baud)) {
        if (req_baud != current_cfg->baud_rate) {
            next_cfg.baud_rate = req_baud;
            changed = true;
        }
    } else {
        ctx->holding_regs[REG_ADDR_BAUD] = ModbusAdapter_BaudToCode(current_cfg->baud_rate);
    }

    if (req_parity_code != current_cfg->parity) {
        if (ModbusAdapter_IsValidParityCode(req_parity_code)) {
            next_cfg.parity = (uint8_t)req_parity_code;
            changed = true;
        } else {
            ctx->holding_regs[MB_REG_PARITY] = ModbusAdapter_ParityToCode(current_cfg->parity);
        }
    }

    if (changed) {
        *out_new_cfg = next_cfg;
    }

    return changed;
}
