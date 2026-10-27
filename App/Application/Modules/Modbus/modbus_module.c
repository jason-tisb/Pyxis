#include "modbus_module.h"

#include <stddef.h>
#include "modbus_persistence.h"
#include "storage_kv.h"
#include "mb.h"
#include "modbus_engine_config.h"

#include <string.h>

static ModbusConfig_t s_mb_cfg;
static ModbusStats_t s_mb_stats;
static bool s_stats_need_save = false;

void ModbusModule_Init(void)
{
    if (!ModbusPersistence_LoadConfig(&s_mb_cfg) ||
        !ModbusModule_ValidateConfig(&s_mb_cfg)) {
        s_mb_cfg.slave_id = 3U;
        s_mb_cfg.baud_rate = 9600U;
        s_mb_cfg.parity = 3U;
        /* H1 deliberately does not write flash during startup recovery.
         * The canonical defaults remain dirty until normal persistence
         * service or a subsequent accepted configuration write. */
    }

    if (!Storage_Read(KV_KEY_MODBUS_STATS, &s_mb_stats, sizeof(s_mb_stats))) {
        memset(&s_mb_stats, 0, sizeof(s_mb_stats));
    }
}

void ModbusModule_GetConfig(ModbusConfig_t* out_cfg)
{
    if (out_cfg != NULL) {
        memcpy(out_cfg, &s_mb_cfg, sizeof(ModbusConfig_t));
    }
}

static bool is_allowed_baud_rate(uint32_t baud_rate)
{
    /* Baud code 0 is reserved for autobaud and is rejected while the
     * autobaud coordinator is disabled. */
    switch (baud_rate) {
#if MODBUS_AUTOBAUD_ENABLE
        case 0U:
#endif
        case 9600U:
        case 19200U:
        case 38400U:
        case 115200U:
            return true;
        default:
            return false;
    }
}

bool ModbusModule_ValidateConfig(const ModbusConfig_t* candidate_cfg)
{
    if (candidate_cfg == NULL) {
        return false;
    }

    if (candidate_cfg->slave_id == 0U || candidate_cfg->slave_id > MB_ADDRESS_MAX) {
        return false;
    }

    if (!is_allowed_baud_rate(candidate_cfg->baud_rate)) {
        return false;
    }

    if (candidate_cfg->parity > 3U) {
        return false;
    }

    return true;
}

bool ModbusModule_UpdateConfig(const ModbusConfig_t* new_cfg)
{
    if (new_cfg == NULL) {
        return false;
    }

    if (!ModbusModule_ValidateConfig(new_cfg)) {
        return false;
    }

    if (memcmp(&s_mb_cfg, new_cfg, sizeof(ModbusConfig_t)) == 0) {
        return true;
    }

    if (!ModbusModule_PersistConfig(new_cfg)) {
        return false;
    }
    return ModbusModule_CommitRuntimeConfig(new_cfg);
}

bool ModbusModule_PersistConfig(const ModbusConfig_t* new_cfg)
{
    if (!ModbusModule_ValidateConfig(new_cfg)) {
        return false;
    }
    return ModbusPersistence_SaveConfig(new_cfg);
}

bool ModbusModule_CommitRuntimeConfig(const ModbusConfig_t* new_cfg)
{
    if (!ModbusModule_ValidateConfig(new_cfg)) {
        return false;
    }
    return ModbusModule_CommitValidatedRuntimeConfig(new_cfg);
}

bool ModbusModule_CommitValidatedRuntimeConfig(const ModbusConfig_t* new_cfg)
{
    if (new_cfg == NULL) {
        return false;
    }
    memcpy(&s_mb_cfg, new_cfg, sizeof(ModbusConfig_t));
    return true;
}

void ModbusModule_RecordRxOk(void)
{
    s_mb_stats.total_rx_ok++;
    s_stats_need_save = true;

    if ((s_mb_stats.total_rx_ok % 1000U) == 0U) {
        (void)Storage_Write(KV_KEY_MODBUS_STATS, &s_mb_stats, sizeof(ModbusStats_t));
        s_stats_need_save = false;
    }
}

void ModbusModule_RecordCrcError(void)
{
    s_mb_stats.crc_errors++;
    s_stats_need_save = true;

    if ((s_mb_stats.crc_errors % 10U) == 0U) {
        (void)Storage_Write(KV_KEY_MODBUS_STATS, &s_mb_stats, sizeof(ModbusStats_t));
        s_stats_need_save = false;
    }
}

void ModbusModule_GetStats(ModbusStats_t* out_stats)
{
    if (out_stats != NULL) {
        memcpy(out_stats, &s_mb_stats, sizeof(ModbusStats_t));
    }
}

void ModbusModule_ClearStats(void)
{
    memset(&s_mb_stats, 0, sizeof(s_mb_stats));
    (void)Storage_Write(KV_KEY_MODBUS_STATS, &s_mb_stats, sizeof(ModbusStats_t));
    s_stats_need_save = false;
}
