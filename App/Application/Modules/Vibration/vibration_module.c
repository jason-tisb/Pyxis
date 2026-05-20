#include "vibration_module.h"

#include <stddef.h>
#include "storage_kv.h"
#include "vibration_product_config.h"

#include <string.h>

#define VIBRATION_BASELINE_SIGNATURE 0x56494242UL

static VibConfig_t s_vib_cfg;
static VibRecord_t s_vib_record;
static VibrationBaseline_t s_vib_baseline;
static VibrationTachState_t s_vib_tach;

static void vibration_config_defaults(VibConfig_t *cfg)
{
    cfg->alarm_threshold_g = 5.0f;
    /* Keep sample rate consistent with the vibration pipeline (FS). */
    cfg->sample_rate_hz = (uint16_t)FS;
    cfg->machine_profile = VIB_MACHINE_PROFILE_GENERIC;
    cfg->auto_baseline_enabled = 1U;
    /* Stored as shaft base frequency (Hz x100). */
    cfg->motor_speed_rpm = (uint16_t)(FREQ_BASE * 100.0f);
    cfg->gear_ratio = 1.0f;
    cfg->gear_teeth_number = (uint16_t)GEAR_TEETH_NUMBER;
    cfg->bearing_ball_count = (uint16_t)BEARING_NUMBER_OF_ROLLING_ELEMENTS;
    cfg->bearing_ball_size_mm = BEARING_ROLLING_DIAMETER * 1000.0f;
    cfg->bearing_pitch_diameter_mm = BEARING_PITCH_DIAMETER * 1000.0f;
    cfg->bearing_alpha_angle_deg = BEARING_CONTACT_ANGLE * (180.0f / 3.14159265358979f);
    cfg->filter_high_pass_hz = 10U;
    cfg->filter_low_pass_hz = (uint16_t)(cfg->sample_rate_hz / 2U);
    cfg->baseline_learning_rate = 0.01f;
    cfg->tach_quality_threshold = 0.55f;
}

bool VibrationModule_ValidateConfig(const VibConfig_t* candidate_cfg)
{
    if (candidate_cfg == NULL) {
        return false;
    }

    if (candidate_cfg->machine_profile > (uint8_t)VIB_MACHINE_PROFILE_GEARBOX) {
        return false;
    }
    if (candidate_cfg->sample_rate_hz == 0U) {
        return false;
    }
    if (candidate_cfg->motor_speed_rpm == 0U) {
        return false;
    }
    if (candidate_cfg->gear_ratio <= 0.0f || candidate_cfg->gear_ratio > 50.0f) {
        return false;
    }
    if (candidate_cfg->gear_teeth_number == 0U || candidate_cfg->gear_teeth_number > 1024U) {
        return false;
    }
    if (candidate_cfg->bearing_ball_count < 3U || candidate_cfg->bearing_ball_count > 32U) {
        return false;
    }
    if (candidate_cfg->bearing_ball_size_mm <= 0.0f) {
        return false;
    }
    if (candidate_cfg->bearing_pitch_diameter_mm <= candidate_cfg->bearing_ball_size_mm) {
        return false;
    }
    if (candidate_cfg->bearing_alpha_angle_deg < 0.0f || candidate_cfg->bearing_alpha_angle_deg > 90.0f) {
        return false;
    }
    if (candidate_cfg->filter_high_pass_hz >= candidate_cfg->filter_low_pass_hz) {
        return false;
    }
    if (candidate_cfg->filter_low_pass_hz > (candidate_cfg->sample_rate_hz / 2U)) {
        return false;
    }
    if (candidate_cfg->baseline_learning_rate < 0.0f || candidate_cfg->baseline_learning_rate > 0.10f) {
        return false;
    }
    if (candidate_cfg->tach_quality_threshold < 0.0f || candidate_cfg->tach_quality_threshold > 1.0f) {
        return false;
    }
    if (candidate_cfg->alarm_threshold_g < 0.0f) {
        return false;
    }

    return true;
}

void VibrationModule_Init(void)
{
    if (!Storage_Read(KV_KEY_VIBRATION_CFG, &s_vib_cfg, sizeof(s_vib_cfg))) {
        vibration_config_defaults(&s_vib_cfg);
        (void)Storage_Write(KV_KEY_VIBRATION_CFG, &s_vib_cfg, sizeof(VibConfig_t));
    } else if (!VibrationModule_ValidateConfig(&s_vib_cfg)) {
        /* Migrate/repair invalid stored configs so Modbus updates can apply. */
        vibration_config_defaults(&s_vib_cfg);
        (void)Storage_Write(KV_KEY_VIBRATION_CFG, &s_vib_cfg, sizeof(VibConfig_t));
    }

    if (!Storage_Read(KV_KEY_VIB_RECORD, &s_vib_record, sizeof(s_vib_record))) {
        memset(&s_vib_record, 0, sizeof(s_vib_record));
    }

    if (!Storage_Read(KV_KEY_VIB_BASELINE, &s_vib_baseline, sizeof(s_vib_baseline)) ||
        s_vib_baseline.signature != VIBRATION_BASELINE_SIGNATURE) {
        memset(&s_vib_baseline, 0, sizeof(s_vib_baseline));
        s_vib_baseline.signature = VIBRATION_BASELINE_SIGNATURE;
    }

    if (!Storage_Read(KV_KEY_VIB_TACH, &s_vib_tach, sizeof(s_vib_tach))) {
        memset(&s_vib_tach, 0, sizeof(s_vib_tach));
    }
}

void VibrationModule_GetConfig(VibConfig_t* out_cfg)
{
    if (out_cfg != NULL) {
        memcpy(out_cfg, &s_vib_cfg, sizeof(VibConfig_t));
    }
}

bool VibrationModule_UpdateConfig(const VibConfig_t* new_cfg)
{
    if (new_cfg == NULL) {
        return false;
    }

    if (!VibrationModule_ValidateConfig(new_cfg)) {
        return false;
    }

    if (memcmp(&s_vib_cfg, new_cfg, sizeof(VibConfig_t)) == 0) {
        return true;
    }

    memcpy(&s_vib_cfg, new_cfg, sizeof(VibConfig_t));
    return Storage_Write(KV_KEY_VIBRATION_CFG, &s_vib_cfg, sizeof(VibConfig_t));
}

void VibrationModule_ProcessNewSample(float current_g)
{
    bool record_changed = false;

    if (current_g > s_vib_record.max_peak_g) {
        s_vib_record.max_peak_g = current_g;
        record_changed = true;
    }

    if (current_g > s_vib_cfg.alarm_threshold_g) {
        s_vib_record.total_alarms += 1U;
        record_changed = true;
    }

    if (record_changed) {
        (void)Storage_Write(KV_KEY_VIB_RECORD, &s_vib_record, sizeof(VibRecord_t));
    }
}

void VibrationModule_GetRecord(VibRecord_t* out_record)
{
    if (out_record != NULL) {
        memcpy(out_record, &s_vib_record, sizeof(VibRecord_t));
    }
}

void VibrationModule_GetBaseline(VibrationBaseline_t* out_baseline)
{
    if (out_baseline != NULL) {
        memcpy(out_baseline, &s_vib_baseline, sizeof(VibrationBaseline_t));
    }
}

bool VibrationModule_UpdateBaseline(const VibrationBaseline_t* new_baseline)
{
    if (new_baseline == NULL) {
        return false;
    }

    s_vib_baseline = *new_baseline;
    s_vib_baseline.signature = VIBRATION_BASELINE_SIGNATURE;
    return Storage_Write(KV_KEY_VIB_BASELINE, &s_vib_baseline, sizeof(VibrationBaseline_t));
}

void VibrationModule_ResetBaseline(void)
{
    memset(&s_vib_baseline, 0, sizeof(s_vib_baseline));
    s_vib_baseline.signature = VIBRATION_BASELINE_SIGNATURE;
    (void)Storage_Write(KV_KEY_VIB_BASELINE, &s_vib_baseline, sizeof(VibrationBaseline_t));
}

void VibrationModule_GetTachState(VibrationTachState_t* out_tach)
{
    if (out_tach != NULL) {
        memcpy(out_tach, &s_vib_tach, sizeof(VibrationTachState_t));
    }
}

void VibrationModule_UpdateTachState(const VibrationTachState_t* tach_state)
{
    if (tach_state == NULL) {
        return;
    }

    s_vib_tach = *tach_state;
    (void)Storage_Write(KV_KEY_VIB_TACH, &s_vib_tach, sizeof(VibrationTachState_t));
}

void VibrationModule_ProcessSnapshot(const VibrationHealthSnapshot_t* snapshot)
{
    VibrationBaseline_t updated_baseline;

    if (snapshot == NULL) {
        return;
    }

    s_vib_record.last_degradation_index = snapshot->degradation_index;
    s_vib_record.last_failure_probability = snapshot->failure_probability;
    (void)Storage_Write(KV_KEY_VIB_RECORD, &s_vib_record, sizeof(VibRecord_t));

    if (s_vib_baseline.update_count == 0U) {
        memset(&updated_baseline, 0, sizeof(updated_baseline));
        updated_baseline.signature = VIBRATION_BASELINE_SIGNATURE;
        updated_baseline.update_count = 1U;
        updated_baseline.shaft_freq_hz = snapshot->shaft_freq_hz;
        updated_baseline.vel_rms = snapshot->vel_rms;
        updated_baseline.broadband_hf = snapshot->broadband_hf;
        updated_baseline.envelope_kurtosis = snapshot->envelope_kurtosis;
        updated_baseline.sideband_density = snapshot->sideband_density;
        updated_baseline.harmonic_consistency = snapshot->harmonic_consistency;
        updated_baseline.peak_density = snapshot->peak_density;
        updated_baseline.failure_probability = snapshot->failure_probability;
        (void)VibrationModule_UpdateBaseline(&updated_baseline);
        return;
    }

    if ((s_vib_cfg.auto_baseline_enabled != 0U) &&
        (snapshot->failure_probability < 0.35f) &&
        (snapshot->tach_quality >= s_vib_cfg.tach_quality_threshold)) {
        float alpha = s_vib_cfg.baseline_learning_rate;
        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 0.10f) alpha = 0.10f;

        updated_baseline = s_vib_baseline;
        updated_baseline.update_count += 1U;
        updated_baseline.shaft_freq_hz += alpha * (snapshot->shaft_freq_hz - updated_baseline.shaft_freq_hz);
        updated_baseline.vel_rms += alpha * (snapshot->vel_rms - updated_baseline.vel_rms);
        updated_baseline.broadband_hf += alpha * (snapshot->broadband_hf - updated_baseline.broadband_hf);
        updated_baseline.envelope_kurtosis += alpha * (snapshot->envelope_kurtosis - updated_baseline.envelope_kurtosis);
        updated_baseline.sideband_density += alpha * (snapshot->sideband_density - updated_baseline.sideband_density);
        updated_baseline.harmonic_consistency += alpha * (snapshot->harmonic_consistency - updated_baseline.harmonic_consistency);
        updated_baseline.peak_density += alpha * (snapshot->peak_density - updated_baseline.peak_density);
        updated_baseline.failure_probability += alpha * (snapshot->failure_probability - updated_baseline.failure_probability);
        (void)VibrationModule_UpdateBaseline(&updated_baseline);
    }
}
