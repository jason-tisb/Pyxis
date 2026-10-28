#include "task_vibration.h"

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "app_freertos.h"
#include "bearing_freq_calculation.h"
#include "iis3dwb_reg.h"
#include "iis3dwb_sensor.h"
#include "modbus_register_map.h"
#include "modbus_engine_config.h"
#if MODBUS_ENGINE_ACTIVE
#include "modbus_engine_map.h"
#define MODBUS_INPUT_REGS (ModbusEngineMap_InputStorage())
#else
#define MODBUS_INPUT_REGS usSRegInBuf
#endif
#include "vibration_product_config.h"
#include "vibration_algorithm.h"
#include "vibration_module.h"

extern osMutexId_t regbuf_mtx;
extern const osMutexAttr_t regbuf_mtx_attr;

enum {
    SENSOR_SPI_DMA_FIFO_CHUNK = 512U,
    SENSOR_SPI_DMA_BUFFER_SIZE = (SENSOR_SPI_DMA_FIFO_CHUNK * sizeof(iis3dwb_fifo_out_raw_t)) + 1U,
    SENSOR_SPI_DMA_MIN_TRANSFER_SIZE = 64U
};

static float32_t raw_x[FFT_LENGTH];
static float32_t raw_y[FFT_LENGTH];
static float32_t raw_z[FFT_LENGTH];
static uint16_t fft_sample_count = 0U;
static osThreadId_t modbusEventHandle;
static IIS3DWB_Sensor_t g_sensor;
static SensorBusSpiContext_t g_spi_sensor_ctx;
static SensorBusSpiDmaConfig_t g_spi_sensor_dma_cfg;
static osSemaphoreId_t g_fifo_wm_sem;
static bool g_use_event_helper_task = false;
static uint8_t g_spi_sensor_dma_tx_buf[SENSOR_SPI_DMA_BUFFER_SIZE];
static uint8_t g_spi_sensor_dma_rx_buf[SENSOR_SPI_DMA_BUFFER_SIZE];
static void refresh_bearing_frequency_if_events_pending(void);

volatile uint32_t g_iis3dwb_temperature_attempts = 0U;
volatile uint32_t g_iis3dwb_temperature_successes = 0U;
volatile uint32_t g_iis3dwb_temperature_failures = 0U;
volatile uint32_t g_iis3dwb_temperature_last_attempt_tick = 0U;
volatile uint32_t g_iis3dwb_temperature_last_success_tick = 0U;
volatile int16_t g_iis3dwb_temperature_last_value_x10 = 250;
volatile bool g_iis3dwb_temperature_valid = false;
volatile bool g_iis3dwb_sensor_ready = false;

typedef enum {
    VIB_PROCESS_PROGRESS_NONE = 0,
    VIB_PROCESS_PROGRESS_PARTIAL,
    VIB_PROCESS_PROGRESS_FRAME_READY,
    VIB_PROCESS_PROGRESS_ERROR
} VibrationProcessProgress_t;

static void wait_forever_with_delay(uint32_t delay_ms)
{
    while (1) {
        osDelay(delay_ms);
    }
}

static void sensor_delay_ms(uint32_t ms)
{
    osDelay(ms);
}

#if (VIBRATION_TEMPERATURE_SERVICE_ENABLE != 0U)
static uint32_t vibration_temperature_period_ticks(void)
{
    uint32_t frequency_hz = osKernelGetTickFreq();
    uint64_t period_ticks;

    if (frequency_hz == 0U) {
        frequency_hz = 1000U;
    }

    period_ticks = ((uint64_t)IIS3DWB_TEMPERATURE_PERIOD_MS * frequency_hz + 999U) / 1000U;
    return (period_ticks == 0U) ? 1U : (uint32_t)period_ticks;
}

static void vibration_publish_temperature(int16_t temperature_x10)
{
    if (regbuf_mtx == NULL || osMutexAcquire(regbuf_mtx, 5U) != osOK) {
        return;
    }

    MODBUS_INPUT_REGS[MB_REG_TEMPERATURE] = (USHORT)temperature_x10;
    (void)osMutexRelease(regbuf_mtx);
}

static void vibration_service_temperature(void)
{
    static uint32_t last_service_tick = 0U;
    static uint32_t period_ticks = 0U;
    static bool schedule_initialized = false;
    uint32_t now;
    int16_t temperature_x10;

    if (!g_iis3dwb_sensor_ready) {
        schedule_initialized = false;
        return;
    }

    now = osKernelGetTickCount();
    if (period_ticks == 0U) {
        period_ticks = vibration_temperature_period_ticks();
    }

    /* Make the first read immediately eligible after initialization/recovery. */
    if (!schedule_initialized) {
        last_service_tick = now - period_ticks;
        schedule_initialized = true;
    }

    if ((uint32_t)(now - last_service_tick) < period_ticks) {
        return;
    }

    last_service_tick = now;
    g_iis3dwb_temperature_last_attempt_tick = now;
    ++g_iis3dwb_temperature_attempts;

    temperature_x10 = 0;
    if (!IIS3DWB_Sensor_ReadTemperatureX10(&g_sensor, &temperature_x10)) {
        ++g_iis3dwb_temperature_failures;
        return;
    }

    g_iis3dwb_temperature_last_value_x10 = temperature_x10;
    g_iis3dwb_temperature_last_success_tick = now;
    ++g_iis3dwb_temperature_successes;
    g_iis3dwb_temperature_valid = true;
    vibration_publish_temperature(temperature_x10);
}
#endif /* VIBRATION_TEMPERATURE_SERVICE_ENABLE */

/* STM32H5 HAL dispatches GPIO EXTI events to edge-specific callbacks.
 * FIFO watermark is configured on PA9 as a rising-edge event. */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if ((GPIO_Pin == VBS_INT_Pin) && (g_fifo_wm_sem != NULL)) {
        (void)osSemaphoreRelease(g_fifo_wm_sem);
    }
}

uint64_t os_time_us(void)
{
    static uint32_t last_cnt = 0U;
    static uint64_t acc_cnt = 0U;

    uint32_t now = osKernelGetSysTimerCount();
    acc_cnt += (uint32_t)(now - last_cnt);
    last_cnt = now;

    {
        uint32_t f = osKernelGetSysTimerFreq();
        return (acc_cnt * 1000000ULL) / (f ? f : 1U);
    }
}

static void write_u64_to_regs(USHORT *reg_buf, uint16_t start_idx, uint64_t value)
{
    reg_buf[start_idx + 0U] = (USHORT)(value & 0xFFFFU);
    reg_buf[start_idx + 1U] = (USHORT)((value >> 16) & 0xFFFFU);
    reg_buf[start_idx + 2U] = (USHORT)((value >> 32) & 0xFFFFU);
    reg_buf[start_idx + 3U] = (USHORT)((value >> 48) & 0xFFFFU);
}

static USHORT scale_to_u16(float value, float scale, float min_value, float max_value)
{
    float scaled = value * scale;

    if (scaled < min_value) {
        scaled = min_value;
    }
    if (scaled > max_value) {
        scaled = max_value;
    }

    return (USHORT)scaled;
}

static USHORT bool_to_reg(bool value)
{
    return value ? 1U : 0U;
}

static USHORT speed_state_to_reg(const VibrationTachState_t *tach_state)
{
    float delta_rpm;

    if (tach_state == NULL || tach_state->valid == 0U) {
        return 0U;
    }

    delta_rpm = tach_state->rpm_instant - tach_state->rpm_filtered;
    if (delta_rpm > 30.0f) {
        return 1U;
    }
    if (delta_rpm < -30.0f) {
        return 2U;
    }
    return 0U;
}

static USHORT aging_factor_to_reg(const VibrationAnalysisResult_t *result,
                                  const VibrationBaseline_t *baseline)
{
    if (result == NULL || baseline == NULL || baseline->vel_rms <= 0.01f) {
        return 100U;
    }

    return scale_to_u16(result->vel_rms / baseline->vel_rms, 100.0f, 0.0f, 65535.0f);
}

static float deg_to_rad(float degrees)
{
    return degrees * (3.14159265358979f / 180.0f);
}

static float shaft_freq_from_config_hz(const VibConfig_t *cfg)
{
    if (cfg == NULL) {
        return FREQ_BASE;
    }

    /* Interpret motor_speed_rpm field as shaft base frequency (Hz x100). */
    return ((float)cfg->motor_speed_rpm) / 100.0f;
}

static void refresh_bearing_frequency_from_config(const VibConfig_t *cfg)
{
    float shaft_freq_hz = shaft_freq_from_config_hz(cfg);

    generateBearingFreq((int)cfg->bearing_ball_count,
                        cfg->bearing_pitch_diameter_mm / 1000.0f,
                        cfg->bearing_ball_size_mm / 1000.0f,
                        deg_to_rad(cfg->bearing_alpha_angle_deg),
                        shaft_freq_hz);
}

static void eventHandlerTask(void *argument)
{
    (void)argument;

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(xSettingsEventGroup,
                                               EVT_GEAR_RATIO_CHANGED | EVT_MOTOR_SPEED_CHANGED | EVT_BEARING_BALL_COUNT_CHANGED |
                                               EVT_BEARING_BALL_SIZE_CHANGED | EVT_BEARING_DIAMETER_CHANGED | EVT_BEARING_ALPHA_ANGLE_CHANGED |
                                               EVT_FILTER_HIGH_PASS_CHANGED | EVT_FILTER_LOW_PASS_CHANGED,
                                               pdTRUE,
                                               pdFALSE,
                                               portMAX_DELAY);

        if (bits != 0U) {
            VibConfig_t vib_cfg;
            VibrationModule_GetConfig(&vib_cfg);
            refresh_bearing_frequency_from_config(&vib_cfg);
        }
    }
}

static bool vibration_try_recover_sensor(void)
{
    static uint32_t s_last_recovery_tick = 0U;
    uint32_t now = HAL_GetTick();

    if ((now - s_last_recovery_tick) < 5000U) {
        return false;
    }

    s_last_recovery_tick = now;
    g_iis3dwb_sensor_ready = false;
    fft_sample_count = 0U;
    g_iis3dwb_sensor_ready = IIS3DWB_Sensor_Recover(&g_sensor);
    return g_iis3dwb_sensor_ready;
}

static inline VibrationProcessProgress_t processVibration(void)
{
    enum { AXIS_RESULT_COUNT = 3 * RESULT_DATA_PER_AXIS };

    static uint64_t time_prev = 0U;
    VibConfig_t vib_cfg;
    VibrationBaseline_t baseline;
    VibrationTachState_t tach_state;
    VibrationAnalysisContext_t analysis_ctx;
    VibrationAnalysisResult_t analysis_x;
    VibrationAnalysisResult_t analysis_y;
    VibrationAnalysisResult_t analysis_z;
    VibrationAnalysisResult_t *dominant_result;
    VibrationHealthSnapshot_t snapshot;
    uint16_t captured = 0U;
    uint16_t shadow[AXIS_RESULT_COUNT];
    int16_t temperature_x10 = g_iis3dwb_temperature_last_value_x10;
    uint64_t time_now;
    uint64_t time_diff;

    if (!g_iis3dwb_sensor_ready) {
        osDelay(1U);
        return VIB_PROCESS_PROGRESS_ERROR;
    }

    if (!IIS3DWB_Sensor_FetchSamples(&g_sensor, raw_x, raw_y, raw_z, fft_sample_count, FFT_LENGTH, &captured)) {
        osDelay(1);
        return VIB_PROCESS_PROGRESS_ERROR;
    }

    if (captured == 0U) {
        osThreadYield();
        return VIB_PROCESS_PROGRESS_NONE;
    }

    fft_sample_count = (uint16_t)(fft_sample_count + captured);
    if (fft_sample_count < FFT_LENGTH) {
        return VIB_PROCESS_PROGRESS_PARTIAL;
    }

    fft_sample_count = 0U;

    time_now = os_time_us();
    time_diff = time_now - time_prev;
    time_prev = time_now;

    VibrationModule_GetConfig(&vib_cfg);
    VibrationModule_GetBaseline(&baseline);
    VibrationModule_GetTachState(&tach_state);

    memset(&analysis_ctx, 0, sizeof(analysis_ctx));
    analysis_ctx.config = &vib_cfg;
    analysis_ctx.baseline = &baseline;
    analysis_ctx.tach_state = &tach_state;
    analysis_ctx.shaft_freq_hz_override = (tach_state.valid != 0U) ? 0.0f : shaft_freq_from_config_hz(&vib_cfg);

    (void)VibrationAnalyzer_Analyze(raw_x, 'x', &analysis_ctx, &analysis_x, &shadow[0 * RESULT_DATA_PER_AXIS], RESULT_DATA_PER_AXIS * sizeof(uint16_t));
    (void)VibrationAnalyzer_Analyze(raw_y, 'y', &analysis_ctx, &analysis_y, &shadow[1 * RESULT_DATA_PER_AXIS], RESULT_DATA_PER_AXIS * sizeof(uint16_t));
    (void)VibrationAnalyzer_Analyze(raw_z, 'z', &analysis_ctx, &analysis_z, &shadow[2 * RESULT_DATA_PER_AXIS], RESULT_DATA_PER_AXIS * sizeof(uint16_t));

    dominant_result = &analysis_x;
    if (analysis_y.vel_rms > dominant_result->vel_rms) {
        dominant_result = &analysis_y;
    }
    if (analysis_z.vel_rms > dominant_result->vel_rms) {
        dominant_result = &analysis_z;
    }

    VibrationModule_ProcessNewSample(dominant_result->acc_peak_g);
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.shaft_freq_hz = dominant_result->shaft_freq_hz;
    snapshot.tach_quality = dominant_result->tach_quality;
    snapshot.vel_rms = dominant_result->vel_rms;
    snapshot.broadband_hf = dominant_result->broadband_hf;
    snapshot.envelope_kurtosis = dominant_result->envelope_kurtosis;
    snapshot.sideband_density = dominant_result->sideband_density;
    snapshot.harmonic_consistency = dominant_result->harmonic_consistency;
    snapshot.peak_density = dominant_result->peak_density;
    snapshot.failure_probability = dominant_result->failure_probability;
    snapshot.degradation_index = dominant_result->degradation_index;
    VibrationModule_ProcessSnapshot(&snapshot);

    osMutexAcquire(regbuf_mtx, osWaitForever);
    MODBUS_INPUT_REGS[MB_REG_TEMPERATURE] = (USHORT)temperature_x10;
    write_u64_to_regs(MODBUS_INPUT_REGS, MB_REG_TIME_PREV_W0, time_prev - time_diff);
    write_u64_to_regs(MODBUS_INPUT_REGS, MB_REG_TIME_NOW_W0, time_now);
    write_u64_to_regs(MODBUS_INPUT_REGS, MB_REG_TIME_DIFF_W0, time_diff);
    memcpy(&MODBUS_INPUT_REGS[MB_REG_X_ACC_RMS],
           &shadow[0 * RESULT_DATA_PER_AXIS],
           RESULT_DATA_PER_AXIS * sizeof(uint16_t));
    memcpy(&MODBUS_INPUT_REGS[MB_REG_Y_ACC_RMS],
           &shadow[1 * RESULT_DATA_PER_AXIS],
           RESULT_DATA_PER_AXIS * sizeof(uint16_t));
    memcpy(&MODBUS_INPUT_REGS[MB_REG_Z_ACC_RMS],
           &shadow[2 * RESULT_DATA_PER_AXIS],
           RESULT_DATA_PER_AXIS * sizeof(uint16_t));

    MODBUS_INPUT_REGS[MB_REG_CURRENT_RPM] = scale_to_u16(tach_state.rpm_filtered, 1.0f, 0.0f, 65535.0f);
    MODBUS_INPUT_REGS[MB_REG_TACH_QUALITY] = scale_to_u16(dominant_result->tach_quality, 100.0f, 0.0f, 100.0f);
    MODBUS_INPUT_REGS[MB_REG_SPEED_STATE] = speed_state_to_reg(&tach_state);
    MODBUS_INPUT_REGS[MB_REG_ORDER_1X] = scale_to_u16(dominant_result->order_1, 1000.0f, 0.0f, 65535.0f);
    MODBUS_INPUT_REGS[MB_REG_ADV_ENV_KURTOSIS] = scale_to_u16(dominant_result->envelope_kurtosis, 100.0f, 0.0f, 65535.0f);
    MODBUS_INPUT_REGS[MB_REG_SIDEBAND_DENSITY] = scale_to_u16(dominant_result->sideband_density, 1000.0f, 0.0f, 65535.0f);
    MODBUS_INPUT_REGS[MB_REG_HARMONIC_CONSISTENCY] = scale_to_u16(dominant_result->harmonic_consistency, 100.0f, 0.0f, 100.0f);
    MODBUS_INPUT_REGS[MB_REG_CEPSTRUM_GEAR_INDEX] = scale_to_u16(dominant_result->cepstrum_peak, 1000.0f, 0.0f, 65535.0f);
    MODBUS_INPUT_REGS[MB_REG_SPECTRAL_KURTOSIS] = scale_to_u16(dominant_result->spectral_kurtosis, 100.0f, 0.0f, 65535.0f);
    MODBUS_INPUT_REGS[MB_REG_CYCLOSTATIONARY_INDEX] = scale_to_u16(dominant_result->cyclostationary_index, 100.0f, 0.0f, 65535.0f);
    MODBUS_INPUT_REGS[MB_REG_RUL_DAYS] = scale_to_u16(dominant_result->remaining_useful_life_hours, 1.0f / 24.0f, 0.0f, 65535.0f);
    MODBUS_INPUT_REGS[MB_REG_FAILURE_PROBABILITY] = scale_to_u16(dominant_result->failure_probability, 100.0f, 0.0f, 100.0f);
    MODBUS_INPUT_REGS[MB_REG_DEGRADATION_INDEX] = scale_to_u16(dominant_result->degradation_index, 100.0f, 0.0f, 65535.0f);
    MODBUS_INPUT_REGS[MB_REG_TIME_TO_MAINTENANCE] = scale_to_u16(dominant_result->remaining_useful_life_hours, 1.0f / 24.0f, 0.0f, 65535.0f);
    MODBUS_INPUT_REGS[MB_REG_BASELINE_UPDATE_COUNT] = (USHORT)((baseline.update_count > 65535U) ? 65535U : baseline.update_count);
    MODBUS_INPUT_REGS[MB_REG_BASELINE_ACTIVE] = bool_to_reg(baseline.update_count > 0U);
    MODBUS_INPUT_REGS[MB_REG_ACTIVE_MACHINE_PROFILE] = (USHORT)vib_cfg.machine_profile;
    MODBUS_INPUT_REGS[MB_REG_AGING_FACTOR] = aging_factor_to_reg(dominant_result, &baseline);
    osMutexRelease(regbuf_mtx);

    return VIB_PROCESS_PROGRESS_FRAME_READY;
}

void main_TaskVibration(void *argument)
{
    SensorBusPort_t bus_port;

    (void)argument;

    VibrationModule_Init();
    {
        VibConfig_t vib_cfg;
        VibrationModule_GetConfig(&vib_cfg);
        refresh_bearing_frequency_from_config(&vib_cfg);
    }

    SensorBusSpi_Init(&bus_port, &g_spi_sensor_ctx, &SENSOR_BUS, CS_up_GPIO_Port, CS_up_Pin, sensor_delay_ms);

    /* Enable SPI DMA for sensor bursts (FIFO block reads). */
    #if 1
    SensorBusSpi_ResetDmaDebug();
    g_spi_sensor_dma_cfg.done_sem = osSemaphoreNew(1U, 0U, NULL);
    g_spi_sensor_dma_cfg.bus_lock = osMutexNew(NULL);
    g_spi_sensor_dma_cfg.tx_buf = g_spi_sensor_dma_tx_buf;
    g_spi_sensor_dma_cfg.rx_buf = g_spi_sensor_dma_rx_buf;
    g_spi_sensor_dma_cfg.buffer_size = (uint16_t)sizeof(g_spi_sensor_dma_tx_buf);
    g_spi_sensor_dma_cfg.min_transfer_len = SENSOR_SPI_DMA_MIN_TRANSFER_SIZE;
    g_spi_sensor_dma_cfg.timeout_ms = 1000U;

    if (g_spi_sensor_dma_cfg.done_sem != NULL && g_spi_sensor_dma_cfg.bus_lock != NULL) {
        SensorBusSpi_EnableDma(&g_spi_sensor_ctx, &g_spi_sensor_dma_cfg);
    }
    #endif
    if (!IIS3DWB_Sensor_Create(&g_sensor, &bus_port, 8.0f)) {
        while (1) {
            osDelay(1000);
        }
    }

    g_iis3dwb_sensor_ready = IIS3DWB_Sensor_Init(&g_sensor, FIFO_WATERMARK, IIS3DWB_XL_FS);
    if (!g_iis3dwb_sensor_ready) {
        /* Reuse the normal recovery owner instead of creating a second
         * initialization path for temperature servicing. */
        for (;;) {
            if (vibration_try_recover_sensor()) {
                break;
            }
            osDelay(100U);
        }
    }

    modbusEventHandle = osThreadNew(eventHandlerTask, NULL, NULL);
    g_use_event_helper_task = (modbusEventHandle != NULL);

    g_fifo_wm_sem = osSemaphoreNew(32U, 0U, NULL);
    if (g_fifo_wm_sem == NULL) {
        wait_forever_with_delay(1000U);
    }

    {
        uint32_t last_progress_tick = HAL_GetTick();
        uint16_t consecutive_failures = 0U;
        uint16_t consecutive_idle_loops = 0U;

        for (;;) {
            VibrationProcessProgress_t progress;

#if (VIBRATION_TEMPERATURE_SERVICE_ENABLE != 0U)
            vibration_service_temperature();
#endif

            if (!g_use_event_helper_task) {
                refresh_bearing_frequency_if_events_pending();
            }
            if (osSemaphoreAcquire(g_fifo_wm_sem, 50U) == osOK) {
                progress = processVibration();
            } else {
                /* Fallback path: keep accumulating samples even if INT edge is missed. */
                progress = processVibration();
                osDelay(1U);
            }

            if (progress == VIB_PROCESS_PROGRESS_FRAME_READY ||
                progress == VIB_PROCESS_PROGRESS_PARTIAL) {
                last_progress_tick = HAL_GetTick();
                consecutive_failures = 0U;
                consecutive_idle_loops = 0U;
                continue;
            }

            if (progress == VIB_PROCESS_PROGRESS_ERROR) {
                ++consecutive_failures;
            } else {
                ++consecutive_idle_loops;
            }

            if (consecutive_failures >= 3U ||
                ((HAL_GetTick() - last_progress_tick) > 5000U && consecutive_idle_loops >= 50U)) {
                if (vibration_try_recover_sensor()) {
                    last_progress_tick = HAL_GetTick();
                    consecutive_failures = 0U;
                    consecutive_idle_loops = 0U;
                }
            }
        }
    }
}

static void refresh_bearing_frequency_if_events_pending(void)
{
    EventBits_t bits = xEventGroupWaitBits(xSettingsEventGroup,
                                           EVT_GEAR_RATIO_CHANGED | EVT_MOTOR_SPEED_CHANGED | EVT_BEARING_BALL_COUNT_CHANGED |
                                           EVT_BEARING_BALL_SIZE_CHANGED | EVT_BEARING_DIAMETER_CHANGED | EVT_BEARING_ALPHA_ANGLE_CHANGED |
                                           EVT_FILTER_HIGH_PASS_CHANGED | EVT_FILTER_LOW_PASS_CHANGED,
                                           pdTRUE,
                                           pdFALSE,
                                           0U);

    if (bits != 0U) {
        VibConfig_t vib_cfg;
        VibrationModule_GetConfig(&vib_cfg);
        refresh_bearing_frequency_from_config(&vib_cfg);
    }
}
