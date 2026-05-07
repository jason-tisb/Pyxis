#include "bearing_freq_calculation.h"

float32_t g_bpfo = 0.0f;
float32_t g_bpfi = 0.0f;
float32_t g_bsf  = 0.0f;
float32_t g_ftf  = 0.0f;

void generateBearingFreq(int   nRolling,
                         float32_t pitchDia,
                         float32_t rollDia,
                         float32_t contactAngle,
                         float32_t shaftFreq)
{
    const float32_t ratio  = rollDia / pitchDia;
    const float32_t cosPhi = cosf(contactAngle);

    g_bpfo = 0.5f * nRolling * shaftFreq * (1.0f - ratio * cosPhi);
    g_bpfi = 0.5f * nRolling * shaftFreq * (1.0f + ratio * cosPhi);
    g_bsf  = (pitchDia / (2.0f * rollDia)) * shaftFreq *  (1.0f - ratio * ratio * cosPhi * cosPhi);
    g_ftf  = 0.5f * shaftFreq * (1.0f - ratio * cosPhi);
}
