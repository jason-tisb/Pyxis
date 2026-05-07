#include "vibration_algorithm.h"

#include <stddef.h>

#if FFT_LENGTH == 16
static const arm_cfft_instance_f32* fft_instance = &arm_cfft_sR_f32_len16;
#elif FFT_LENGTH == 32
static const arm_cfft_instance_f32* fft_instance = &arm_cfft_sR_f32_len32;
#elif FFT_LENGTH == 64
static const arm_cfft_instance_f32* fft_instance = &arm_cfft_sR_f32_len64;
#elif FFT_LENGTH == 128
static const arm_cfft_instance_f32* fft_instance = &arm_cfft_sR_f32_len128;
#elif FFT_LENGTH == 256
static const arm_cfft_instance_f32* fft_instance = &arm_cfft_sR_f32_len256;
#elif FFT_LENGTH == 512
static const arm_cfft_instance_f32* fft_instance = &arm_cfft_sR_f32_len512;
#elif FFT_LENGTH == 1024
static const arm_cfft_instance_f32* fft_instance = &arm_cfft_sR_f32_len1024;
#elif FFT_LENGTH == 2048
static const arm_cfft_instance_f32* fft_instance = &arm_cfft_sR_f32_len2048;
#elif FFT_LENGTH == 4096
static const arm_cfft_instance_f32* fft_instance = &arm_cfft_sR_f32_len4096;
#else
    #error "Invalid FFT_LENGTH defined"
#endif

typedef struct {
    float unbalance_threshold;
    float misalignment_threshold;
    float looseness_threshold;
    float bearing_threshold;
    float gearmesh_threshold;
    float failure_weight;
} VibrationProfileRule_t;

typedef struct {
    float vel_time[FFT_LENGTH];
    float disp_time[FFT_LENGTH];
    float acc_mag[FFT_LENGTH];
    float vel_mag[FFT_LENGTH];
    float env_time[FFT_LENGTH];
    float env_mag[FFT_LENGTH];
    float cep_input[FFT_LENGTH];
    float cep_mag[FFT_LENGTH];
    float fft_buffer[FFT_LENGTH * 2U];
    float peak_work_mag[QUANTITY_USED_FFT_SAMPLE];
    uint16_t peak_work_bins[QUANTITY_USED_FFT_SAMPLE];
} VibrationAnalysisWorkspace_t;

float32_t g_gear_teeth = 0.0f;
static VibrationAnalysisWorkspace_t g_vibration_workspace;

static float clampf_local(float value, float min_value, float max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float sample_kurtosis(const float *x, uint16_t n)
{
    float mean = 0.0f;
    float m2 = 0.0f;
    float m4 = 0.0f;

    for (uint16_t i = 0; i < n; ++i) {
        mean += x[i];
    }
    mean /= (float)n;

    for (uint16_t i = 0; i < n; ++i) {
        float d = x[i] - mean;
        float d2 = d * d;
        m2 += d2;
        m4 += d2 * d2;
    }

    m2 /= (float)n;
    m4 /= (float)n;
    return (m2 > 0.0f) ? (m4 / (m2 * m2)) : 0.0f;
}

static float mean_in_band(const float *spec, float f1, float f2)
{
    int start_bin = (int)floorf(f1 / BIN_WIDTH);
    int end_bin = (int)ceilf(f2 / BIN_WIDTH);
    float sum = 0.0f;
    int count = 0;

    if (start_bin < 0) start_bin = 0;
    if (end_bin > (FFT_LENGTH / 2)) end_bin = FFT_LENGTH / 2;
    for (int i = start_bin; i <= end_bin; ++i) {
        sum += spec[i];
        ++count;
    }
    return (count > 0) ? (sum / (float)count) : 0.0f;
}

static float peak_near_frequency(const float *spec, float target_hz, int window_bins)
{
    int center = (int)lroundf(target_hz / BIN_WIDTH);
    int start = center - window_bins;
    int end = center + window_bins;
    float peak = 0.0f;

    if (start < 0) start = 0;
    if (end > (FFT_LENGTH / 2)) end = FFT_LENGTH / 2;
    for (int i = start; i <= end; ++i) {
        if (spec[i] > peak) {
            peak = spec[i];
        }
    }
    return peak;
}

static float order_peak(const float *spec, float shaft_freq_hz, float order)
{
    return peak_near_frequency(spec, shaft_freq_hz * order, 1);
}

static void fft_magnitude_from_time(const float *input, float *output, float *fft_buffer)
{
    for (uint16_t i = 0; i < FFT_LENGTH; ++i) {
        fft_buffer[2U * i] = input[i];
        fft_buffer[(2U * i) + 1U] = 0.0f;
    }

#if defined(VB001)
    arm_cfft_f32(fft_instance, fft_buffer, 0U, 1U);
    arm_cmplx_mag_f32(fft_buffer, output, FFT_LENGTH);
#else
    memset(output, 0, sizeof(float) * FFT_LENGTH);
#endif
}

static void acceleration_spectrum_to_velocity(const float *acc_mag, float *vel_mag)
{
    vel_mag[0] = 0.0f;
    for (uint16_t i = 1; i < FFT_LENGTH; ++i) {
        float freq = ((float)i * FS) / (float)FFT_LENGTH;
        vel_mag[i] = (freq > 0.0f) ? ((acc_mag[i] / (2.0f * PI * freq)) * 1000.0f) : 0.0f;
    }
}

static void envelope_signal(const float *input, float *env_signal)
{
    for (uint16_t i = 0; i < FFT_LENGTH; ++i) {
        env_signal[i] = fabsf(input[i]);
    }
}

static float sideband_density(const float *spec, float shaft_freq_hz)
{
    float carrier = order_peak(spec, shaft_freq_hz, 1.0f);
    float sideband = 0.0f;

    for (uint16_t k = 1; k <= 4U; ++k) {
        sideband += peak_near_frequency(spec, shaft_freq_hz * (1.0f - (0.05f * (float)k)), 1);
        sideband += peak_near_frequency(spec, shaft_freq_hz * (1.0f + (0.05f * (float)k)), 1);
    }

    return (carrier > 0.0f) ? (sideband / carrier) : 0.0f;
}

static float harmonic_consistency(const VibrationAnalysisResult_t *result)
{
    float harmonics[6] = {
        result->order_1,
        result->order_2,
        result->order_3,
        result->order_4,
        result->order_5,
        result->order_6
    };
    float reference = harmonics[0];
    float score = 0.0f;

    if (reference <= 0.0f) {
        return 0.0f;
    }

    for (uint16_t i = 1; i < 6U; ++i) {
        float normalized = harmonics[i] / reference;
        float expected = 1.0f / (float)(i + 1U);
        score += 1.0f - clampf_local(fabsf(normalized - expected), 0.0f, 1.0f);
    }

    return score / 5.0f;
}

static float peak_density_and_sort(const float *vel_mag,
                                   VibrationAnalysisWorkspace_t *workspace,
                                   USHORT *sorted_bins,
                                   USHORT *sorted_magnitudes)
{
    float mean_value = 0.0f;
    float std_value = 0.0f;
    uint16_t peaks = 0U;
    float *work_mag = workspace->peak_work_mag;
    uint16_t *work_bins = workspace->peak_work_bins;

    arm_mean_f32(vel_mag, QUANTITY_USED_FFT_SAMPLE, &mean_value);
    arm_std_f32(vel_mag, QUANTITY_USED_FFT_SAMPLE, &std_value);

    for (uint16_t i = 0; i < QUANTITY_USED_FFT_SAMPLE; ++i) {
        work_mag[i] = vel_mag[i];
        work_bins[i] = (uint16_t)((float)i * BIN_WIDTH);
        if (vel_mag[i] > (mean_value + (2.5f * std_value))) {
            ++peaks;
        }
    }

    for (uint16_t i = 0; i < QUANTITY_USED_FFT_SAMPLE; ++i) {
        for (uint16_t j = 0; j + 1U < QUANTITY_USED_FFT_SAMPLE - i; ++j) {
            if (work_mag[j] < work_mag[j + 1U]) {
                float tmp_mag = work_mag[j];
                uint16_t tmp_bin = work_bins[j];
                work_mag[j] = work_mag[j + 1U];
                work_bins[j] = work_bins[j + 1U];
                work_mag[j + 1U] = tmp_mag;
                work_bins[j + 1U] = tmp_bin;
            }
        }
    }

    for (uint16_t i = 0; i < ((RESULT_DATA_PER_AXIS - 10U) / 2U); ++i) {
        sorted_bins[i] = (USHORT)work_bins[i];
        sorted_magnitudes[i] = (USHORT)clampf_local(work_mag[i], 0.0f, 65535.0f);
    }

    return (float)peaks / (float)QUANTITY_USED_FFT_SAMPLE;
}

static float cepstrum_peak(const float *vel_mag,
                           float shaft_freq_hz,
                           VibrationAnalysisWorkspace_t *workspace)
{
    uint16_t target_bin;

    for (uint16_t i = 0; i < FFT_LENGTH; ++i) {
        workspace->cep_input[i] = logf(vel_mag[i] + 1.0e-6f);
    }

    fft_magnitude_from_time(workspace->cep_input, workspace->cep_mag, workspace->fft_buffer);
    target_bin = (shaft_freq_hz > 0.0f) ? (uint16_t)clampf_local((FS / shaft_freq_hz), 1.0f, (float)(FFT_LENGTH / 2U)) : 1U;
    return workspace->cep_mag[target_bin];
}

static float spectral_kurtosis_estimate(const float *vel_mag)
{
    float band_energy[16];

    for (uint16_t band = 0; band < 16U; ++band) {
        uint16_t start = (band * QUANTITY_USED_FFT_SAMPLE) / 16U;
        uint16_t end = ((band + 1U) * QUANTITY_USED_FFT_SAMPLE) / 16U;
        float sum = 0.0f;
        for (uint16_t i = start; i < end; ++i) {
            sum += vel_mag[i];
        }
        band_energy[band] = sum;
    }

    return sample_kurtosis(band_energy, 16U);
}

static VibrationProfileRule_t profile_rule(uint8_t profile)
{
    switch ((VibrationMachineProfile_t)profile) {
        case VIB_MACHINE_PROFILE_MOTOR:
            return (VibrationProfileRule_t){2.2f, 1.4f, 0.9f, 0.8f, 0.8f, 1.0f};
        case VIB_MACHINE_PROFILE_PUMP:
            return (VibrationProfileRule_t){2.5f, 1.6f, 1.0f, 0.9f, 1.0f, 1.1f};
        case VIB_MACHINE_PROFILE_FAN:
            return (VibrationProfileRule_t){2.0f, 1.3f, 0.8f, 0.8f, 0.7f, 0.9f};
        case VIB_MACHINE_PROFILE_COMPRESSOR:
            return (VibrationProfileRule_t){2.8f, 1.8f, 1.2f, 1.0f, 1.2f, 1.2f};
        case VIB_MACHINE_PROFILE_GEARBOX:
            return (VibrationProfileRule_t){2.5f, 1.7f, 1.1f, 1.0f, 0.7f, 1.3f};
        case VIB_MACHINE_PROFILE_GENERIC:
        default:
            return (VibrationProfileRule_t){2.5f, 1.5f, 1.0f, 0.8f, 0.8f, 1.0f};
    }
}

static uint32_t iso_analysis(float rms)
{
    if (rms <= ISO_LEVEL_LOW) return 1U;
    if (rms <= ISO_LEVEL_MEDIUM) return 2U;
    if (rms <= ISO_LEVEL_HIGH) return 3U;
    return 4U;
}

void VibrationTachTracker_Init(VibrationTachState_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

void VibrationTachTracker_UpdateCapture(VibrationTachState_t *state, uint32_t period_us)
{
    float rpm;
    float rel_err;

    if (state == NULL || period_us == 0U) {
        return;
    }

    rpm = 60000000.0f / (float)period_us;
    rel_err = (state->rpm_filtered > 1.0f) ? fabsf(rpm - state->rpm_filtered) / state->rpm_filtered : 0.0f;

    state->period_us = period_us;
    state->rpm_instant = rpm;
    state->rpm_filtered = (state->valid != 0U) ? ((0.8f * state->rpm_filtered) + (0.2f * rpm)) : rpm;
    state->shaft_freq_hz = state->rpm_filtered / 60.0f;
    state->quality = 1.0f - clampf_local(rel_err / 0.2f, 0.0f, 1.0f);
    state->valid = 1U;
}

bool VibrationAnalyzer_Analyze(const float32_t *acc_time,
                               char axis,
                               const VibrationAnalysisContext_t *ctx,
                               VibrationAnalysisResult_t *out_result,
                               USHORT *legacy_result,
                               uint16_t legacy_result_size_bytes)
{
    VibrationAnalysisWorkspace_t *workspace = &g_vibration_workspace;
    float *vel_time = workspace->vel_time;
    float *disp_time = workspace->disp_time;
    float *acc_mag = workspace->acc_mag;
    float *vel_mag = workspace->vel_mag;
    float *env_time = workspace->env_time;
    float *env_mag = workspace->env_mag;
    float acc_min = FLT_MAX;
    float acc_max = -FLT_MAX;
    float disp_min = FLT_MAX;
    float disp_max = -FLT_MAX;
    float acc_rms = 0.0f;
    float acc_peak = 0.0f;
    float vel_rms = 0.0f;
    float vel_peak = 0.0f;
    float baseline_factor = 1.0f;
    float shaft_freq_hz = FREQ_BASE;
    float tach_quality = 0.0f;
    float gear_teeth = (float)GEAR_TEETH_NUMBER;
    VibrationProfileRule_t rule;

    if (acc_time == NULL || out_result == NULL || legacy_result == NULL || legacy_result_size_bytes < (RESULT_DATA_PER_AXIS * sizeof(USHORT))) {
        return false;
    }

    memset(out_result, 0, sizeof(*out_result));

    if (ctx != NULL) {
        if (ctx->shaft_freq_hz_override > 0.0f) {
            shaft_freq_hz = ctx->shaft_freq_hz_override;
        } else if (ctx->tach_state != NULL && ctx->tach_state->valid != 0U && ctx->tach_state->shaft_freq_hz > 0.0f) {
            shaft_freq_hz = ctx->tach_state->shaft_freq_hz;
        }

        if (ctx->tach_state != NULL) {
            tach_quality = ctx->tach_state->quality;
        }

        if (ctx->config != NULL && ctx->config->gear_teeth_number > 0U) {
            gear_teeth = (float)ctx->config->gear_teeth_number;
        }

        if (ctx->baseline != NULL && ctx->baseline->vel_rms > 0.05f) {
            baseline_factor = ctx->baseline->vel_rms;
        }
    }

    rule = profile_rule((ctx != NULL && ctx->config != NULL) ? ctx->config->machine_profile : VIB_MACHINE_PROFILE_GENERIC);

    for (uint16_t i = 0; i < FFT_LENGTH; ++i) {
        float a = acc_time[i];
        acc_rms += a * a;
        if (a > acc_max) acc_max = a;
        if (a < acc_min) acc_min = a;
        if (fabsf(a) > acc_peak) acc_peak = fabsf(a);
    }
    acc_rms = sqrtf(acc_rms / (float)FFT_LENGTH);

    vel_time[0] = 0.0f;
    disp_time[0] = 0.0f;
    for (uint16_t i = 1; i < FFT_LENGTH; ++i) {
        vel_time[i] = vel_time[i - 1U] + (0.5f * (acc_time[i - 1U] + acc_time[i]) * (1.0f / FS));
        disp_time[i] = disp_time[i - 1U] + (0.5f * (vel_time[i - 1U] + vel_time[i]) * (1.0f / FS));
    }

    for (uint16_t i = 0; i < FFT_LENGTH; ++i) {
        vel_rms += vel_time[i] * vel_time[i];
        if (fabsf(vel_time[i]) > vel_peak) vel_peak = fabsf(vel_time[i]);
        if (disp_time[i] > disp_max) disp_max = disp_time[i];
        if (disp_time[i] < disp_min) disp_min = disp_time[i];
    }
    vel_rms = sqrtf(vel_rms / (float)FFT_LENGTH);

    fft_magnitude_from_time(acc_time, acc_mag, workspace->fft_buffer);
    acceleration_spectrum_to_velocity(acc_mag, vel_mag);
    envelope_signal(acc_time, env_time);
    fft_magnitude_from_time(env_time, env_mag, workspace->fft_buffer);

    out_result->axis = axis;
    out_result->shaft_freq_hz = shaft_freq_hz;
    out_result->tach_quality = tach_quality;
    out_result->acc_rms_g = acc_rms;
    out_result->acc_peak_g = acc_peak;
    out_result->acc_p2p_g = acc_max - acc_min;
    out_result->crest_factor = (acc_rms > 0.0f) ? (acc_peak / acc_rms) : 0.0f;
    out_result->vel_rms = vel_rms * 1000.0f;
    out_result->vel_peak = vel_peak * 1000.0f;
    out_result->disp_p2p = (disp_max - disp_min) * 1000.0f;
    out_result->order_0p5 = order_peak(vel_mag, shaft_freq_hz, 0.5f);
    out_result->order_1 = order_peak(vel_mag, shaft_freq_hz, 1.0f);
    out_result->order_2 = order_peak(vel_mag, shaft_freq_hz, 2.0f);
    out_result->order_3 = order_peak(vel_mag, shaft_freq_hz, 3.0f);
    out_result->order_4 = order_peak(vel_mag, shaft_freq_hz, 4.0f);
    out_result->order_5 = order_peak(vel_mag, shaft_freq_hz, 5.0f);
    out_result->order_6 = order_peak(vel_mag, shaft_freq_hz, 6.0f);
    out_result->sideband_density = sideband_density(vel_mag, shaft_freq_hz);
    out_result->peak_density = peak_density_and_sort(vel_mag, workspace, out_result->sorted_peak_bins, out_result->sorted_peak_magnitudes);
    out_result->broadband_mid = mean_in_band(vel_mag, 500.0f, 2000.0f);
    out_result->broadband_hf = mean_in_band(vel_mag, 2000.0f, 8000.0f);
    out_result->bearing_bpfo = peak_near_frequency(env_mag, g_bpfo, 1);
    out_result->bearing_bpfi = peak_near_frequency(env_mag, g_bpfi, 1);
    out_result->bearing_bsf = peak_near_frequency(env_mag, g_bsf, 1);
    out_result->bearing_ftf = peak_near_frequency(env_mag, g_ftf, 1);
    out_result->envelope_kurtosis = sample_kurtosis(env_time, FFT_LENGTH);
    out_result->cepstrum_peak = cepstrum_peak(vel_mag, shaft_freq_hz, workspace);
    out_result->spectral_kurtosis = spectral_kurtosis_estimate(vel_mag);
    out_result->cyclostationary_index = out_result->sideband_density * out_result->envelope_kurtosis;
    out_result->harmonic_consistency = harmonic_consistency(out_result);

    if (out_result->order_1 > rule.unbalance_threshold && out_result->order_2 < (0.5f * out_result->order_1)) {
        out_result->fault_flags |= VIB_FLAG_UNBALANCE;
    }
    if ((out_result->order_2 > rule.misalignment_threshold) || (out_result->sideband_density > rule.looseness_threshold)) {
        out_result->fault_flags |= VIB_FLAG_MISALIGNMENT;
    }
    if ((out_result->envelope_kurtosis > 4.5f) || (out_result->peak_density > 0.08f)) {
        out_result->fault_flags |= VIB_FLAG_LOOSENESS;
    }
    if ((out_result->bearing_bpfo > rule.bearing_threshold) ||
        (out_result->bearing_bpfi > rule.bearing_threshold) ||
        (out_result->bearing_bsf > rule.bearing_threshold) ||
        (out_result->bearing_ftf > rule.bearing_threshold)) {
        out_result->fault_flags |= VIB_FLAG_BEARING;
    }
    if ((gear_teeth > 0.0f) &&
        (peak_near_frequency(vel_mag, shaft_freq_hz * gear_teeth, 2) > rule.gearmesh_threshold)) {
        out_result->fault_flags |= VIB_FLAG_GEAR_MESH;
    }
    if ((ctx != NULL) && (ctx->tach_state != NULL) && (ctx->tach_state->valid != 0U) && (ctx->tach_state->quality < 0.35f)) {
        out_result->fault_flags |= VIB_FLAG_SPEED_UNSTABLE;
    }
    if (out_result->spectral_kurtosis > 4.0f) {
        out_result->fault_flags |= VIB_FLAG_RESONANCE;
    }

    out_result->degradation_index =
        (0.35f * (out_result->vel_rms / baseline_factor)) +
        (0.20f * (out_result->broadband_hf / fmaxf(0.1f, (ctx != NULL && ctx->baseline != NULL) ? ctx->baseline->broadband_hf : out_result->broadband_hf))) +
        (0.15f * out_result->sideband_density) +
        (0.15f * out_result->peak_density * 10.0f) +
        (0.15f * out_result->envelope_kurtosis / 3.0f);
    out_result->degradation_index *= rule.failure_weight;
    out_result->failure_probability = clampf_local((out_result->degradation_index - 0.8f) / 2.2f, 0.0f, 1.0f);
    out_result->remaining_useful_life_hours =
        (out_result->failure_probability < 0.01f) ? 12000.0f :
        (12000.0f * (1.0f - out_result->failure_probability));

    legacy_result[0] = (USHORT)clampf_local(out_result->acc_rms_g * 1000.0f, 0.0f, 65535.0f);
    legacy_result[1] = (USHORT)clampf_local(out_result->acc_peak_g * 1000.0f, 0.0f, 65535.0f);
    legacy_result[2] = (USHORT)clampf_local(out_result->acc_p2p_g * 1000.0f, 0.0f, 65535.0f);
    legacy_result[3] = (USHORT)clampf_local(out_result->crest_factor * 1000.0f, 0.0f, 65535.0f);
    legacy_result[4] = (USHORT)clampf_local(out_result->vel_rms, 0.0f, 65535.0f);
    legacy_result[5] = (USHORT)clampf_local(out_result->vel_peak, 0.0f, 65535.0f);
    legacy_result[6] = (USHORT)clampf_local(out_result->disp_p2p, 0.0f, 65535.0f);
    legacy_result[7] = (USHORT)clampf_local(out_result->envelope_kurtosis * 100.0f, 0.0f, 65535.0f);
    legacy_result[8] = (USHORT)iso_analysis(out_result->vel_rms / 1000.0f);
    legacy_result[9] = (USHORT)(out_result->fault_flags & 0xFFFFu);
    memcpy(&legacy_result[10], out_result->sorted_peak_bins, sizeof(out_result->sorted_peak_bins));
    memcpy(&legacy_result[10 + ((RESULT_DATA_PER_AXIS - 10) / 2)], out_result->sorted_peak_magnitudes, sizeof(out_result->sorted_peak_magnitudes));

    return true;
}

void analyze_vibration(float32_t* pAccData, USHORT * pResult, uint16_t resultSize, char axis)
{
    VibrationAnalysisContext_t ctx;
    VibrationAnalysisResult_t result;

    memset(&ctx, 0, sizeof(ctx));
    ctx.shaft_freq_hz_override = FREQ_BASE;
    (void)VibrationAnalyzer_Analyze(pAccData, axis, &ctx, &result, pResult, resultSize);
}
