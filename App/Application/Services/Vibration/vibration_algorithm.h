#pragma once

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include "arm_const_structs.h"
#include "arm_math.h"
#include "bearing_freq_calculation.h"
#include "user_mb_app.h"
#include "vibration_product_config.h"
#include "vibration_common.h"

#if ISO_CLASS == 1
    #define ISO_LEVEL_LOW    0.71f
    #define ISO_LEVEL_MEDIUM 1.8f
    #define ISO_LEVEL_HIGH   4.5f
#elif ISO_CLASS == 2
    #define ISO_LEVEL_LOW    1.12f
    #define ISO_LEVEL_MEDIUM 2.8f
    #define ISO_LEVEL_HIGH   7.1f
#elif ISO_CLASS == 3
    #define ISO_LEVEL_LOW    1.8f
    #define ISO_LEVEL_MEDIUM 4.5f
    #define ISO_LEVEL_HIGH   11.2f
#elif ISO_CLASS == 4
    #define ISO_LEVEL_LOW    1.8f
    #define ISO_LEVEL_MEDIUM 4.5f
    #define ISO_LEVEL_HIGH   11.2f
#else
    #error "Unknown ISO_CLASS value"
#endif

#define PI 3.14159265358979f

enum {
    VIB_FLAG_UNBALANCE      = 1u << 0,
    VIB_FLAG_MISALIGNMENT   = 1u << 1,
    VIB_FLAG_LOOSENESS      = 1u << 2,
    VIB_FLAG_BEARING        = 1u << 3,
    VIB_FLAG_GEAR_MESH      = 1u << 4,
    VIB_FLAG_RESONANCE      = 1u << 5,
    VIB_FLAG_SPEED_UNSTABLE = 1u << 6
};

typedef struct {
    const VibConfig_t *config;
    const VibrationBaseline_t *baseline;
    const VibrationTachState_t *tach_state;
    float shaft_freq_hz_override;
} VibrationAnalysisContext_t;

typedef struct {
    char axis;
    float shaft_freq_hz;
    float tach_quality;
    float acc_rms_g;
    float acc_peak_g;
    float acc_p2p_g;
    float crest_factor;
    float vel_rms;
    float vel_peak;
    float disp_p2p;
    float order_0p5;
    float order_1;
    float order_2;
    float order_3;
    float order_4;
    float order_5;
    float order_6;
    float sideband_density;
    float harmonic_consistency;
    float peak_density;
    float envelope_kurtosis;
    float cepstrum_peak;
    float spectral_kurtosis;
    float cyclostationary_index;
    float broadband_mid;
    float broadband_hf;
    float bearing_bpfo;
    float bearing_bpfi;
    float bearing_bsf;
    float bearing_ftf;
    float degradation_index;
    float failure_probability;
    float remaining_useful_life_hours;
    uint32_t fault_flags;
    USHORT sorted_peak_bins[(RESULT_DATA_PER_AXIS - 10) / 2];
    USHORT sorted_peak_magnitudes[(RESULT_DATA_PER_AXIS - 10) / 2];
} VibrationAnalysisResult_t;

extern float32_t g_gear_teeth;

void VibrationTachTracker_Init(VibrationTachState_t *state);
void VibrationTachTracker_UpdateCapture(VibrationTachState_t *state, uint32_t period_us);
bool VibrationAnalyzer_Analyze(const float32_t *acc_time,
                               char axis,
                               const VibrationAnalysisContext_t *ctx,
                               VibrationAnalysisResult_t *out_result,
                               USHORT *legacy_result,
                               uint16_t legacy_result_size_bytes);
void analyze_vibration(float32_t* pAccData, USHORT * pResult, uint16_t resultSize, char axis);
