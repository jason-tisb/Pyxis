#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "user_mb_app.h"
#include "vibration_module.h"

typedef struct {
    USHORT *holding_regs;
    uint16_t reg_count;
} VibrationAdapterContext_t;

typedef enum {
    VIBRATION_ADAPTER_CMD_NONE = 0,
    VIBRATION_ADAPTER_CMD_CAPTURE_BASELINE = 1,
    VIBRATION_ADAPTER_CMD_RESET_BASELINE = 2
} VibrationAdapterCommand_t;

bool VibrationAdapter_IsValidMachineProfile(uint16_t value);
bool VibrationAdapter_IsValidCommand(uint16_t value);
bool VibrationAdapter_IsValidRawConfigValue(uint16_t reg_addr, uint16_t value);

void VibrationAdapter_InitMap(const VibrationAdapterContext_t *ctx, const VibConfig_t *current_cfg);
bool VibrationAdapter_CheckConfigUpdate(const VibrationAdapterContext_t *ctx,
                                        const VibConfig_t *current_cfg,
                                        VibConfig_t *out_new_cfg);
bool VibrationAdapter_ReadCommand(const VibrationAdapterContext_t *ctx,
                                  VibrationAdapterCommand_t *out_cmd);
void VibrationAdapter_ClearCommand(const VibrationAdapterContext_t *ctx);

#ifdef __cplusplus
}
#endif
