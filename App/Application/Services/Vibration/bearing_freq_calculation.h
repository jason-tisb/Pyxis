#pragma once

#include "arm_math.h"

extern float32_t g_bpfo;
extern float32_t g_bpfi;
extern float32_t g_bsf;
extern float32_t g_ftf;

void generateBearingFreq(int nRolling,
                         float pitchDia,
                         float rollDia,
                         float contactAngle,
                         float shaftFreq);
