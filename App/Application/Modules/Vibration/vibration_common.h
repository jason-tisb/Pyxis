#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VIB_MACHINE_PROFILE_GENERIC = 0,
    VIB_MACHINE_PROFILE_MOTOR,
    VIB_MACHINE_PROFILE_PUMP,
    VIB_MACHINE_PROFILE_FAN,
    VIB_MACHINE_PROFILE_COMPRESSOR,
    VIB_MACHINE_PROFILE_GEARBOX
} VibrationMachineProfile_t;

typedef struct {
    float alarm_threshold_g;
    uint16_t sample_rate_hz;
    uint8_t machine_profile;
    uint8_t auto_baseline_enabled;
    uint16_t motor_speed_rpm;
    float gear_ratio;
    uint16_t gear_teeth_number;
    uint16_t bearing_ball_count;
    float bearing_ball_size_mm;
    float bearing_pitch_diameter_mm;
    float bearing_alpha_angle_deg;
    uint16_t filter_high_pass_hz;
    uint16_t filter_low_pass_hz;
    float baseline_learning_rate;
    float tach_quality_threshold;
} VibConfig_t;

typedef struct {
    float max_peak_g;
    uint32_t total_alarms;
    float last_degradation_index;
    float last_failure_probability;
} VibRecord_t;

typedef struct {
    uint32_t signature;
    uint32_t update_count;
    float shaft_freq_hz;
    float vel_rms;
    float broadband_hf;
    float envelope_kurtosis;
    float sideband_density;
    float harmonic_consistency;
    float peak_density;
    float failure_probability;
} VibrationBaseline_t;

typedef struct {
    uint32_t period_us;
    float rpm_instant;
    float rpm_filtered;
    float shaft_freq_hz;
    float quality;
    uint8_t valid;
} VibrationTachState_t;

typedef struct {
    float shaft_freq_hz;
    float tach_quality;
    float vel_rms;
    float broadband_hf;
    float envelope_kurtosis;
    float sideband_density;
    float harmonic_consistency;
    float peak_density;
    float failure_probability;
    float degradation_index;
} VibrationHealthSnapshot_t;
