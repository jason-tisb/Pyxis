#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "vibration_common.h"

void VibrationModule_Init(void);
void VibrationModule_GetConfig(VibConfig_t* out_cfg);
bool VibrationModule_ValidateConfig(const VibConfig_t* candidate_cfg);
bool VibrationModule_UpdateConfig(const VibConfig_t* new_cfg);
/* Milestone F: separate volatile publication from persistence. */
bool VibrationModule_PersistConfig(const VibConfig_t* cfg);
bool VibrationModule_CommitRuntimeConfig(const VibConfig_t* cfg);
/* Caller must have completed VibrationModule_ValidateConfig(). */
bool VibrationModule_CommitValidatedRuntimeConfig(const VibConfig_t* cfg);
void VibrationModule_ProcessNewSample(float current_g);
void VibrationModule_GetRecord(VibRecord_t* out_record);
void VibrationModule_GetBaseline(VibrationBaseline_t* out_baseline);
bool VibrationModule_UpdateBaseline(const VibrationBaseline_t* new_baseline);
bool VibrationModule_SaveBaseline(void);
void VibrationModule_ResetBaseline(void);
void VibrationModule_GetTachState(VibrationTachState_t* out_tach);
void VibrationModule_UpdateTachState(const VibrationTachState_t* tach_state);
void VibrationModule_ProcessSnapshot(const VibrationHealthSnapshot_t* snapshot);
