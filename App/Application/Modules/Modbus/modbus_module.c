#include "modbus_module.h"

#include <stddef.h>
#include "storage_kv.h"

#include <string.h>

static ModbusConfig_t s_mb_cfg;
static ModbusStats_t s_mb_stats;
static bool s_stats_need_save = false;

void ModbusModule_Init(void)
{
    if (!Storage_Read(KV_KEY_MODBUS_CFG, &s_mb_cfg, sizeof(s_mb_cfg))) {
        s_mb_cfg.slave_id = 3U;
        s_mb_cfg.baud_rate = 9600U;
        s_mb_cfg.parity = 2U;
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

bool ModbusModule_UpdateConfig(const ModbusConfig_t* new_cfg)
{
    if (new_cfg == NULL) {
        return false;
    }

    if (memcmp(&s_mb_cfg, new_cfg, sizeof(ModbusConfig_t)) == 0) {
        return true;
    }

    memcpy(&s_mb_cfg, new_cfg, sizeof(ModbusConfig_t));
    return Storage_Write(KV_KEY_MODBUS_CFG, &s_mb_cfg, sizeof(ModbusConfig_t));
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
