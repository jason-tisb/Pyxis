#include "iis3dwb_sensor.h"
#include "vibration_product_config.h"

#define IIS3DWB_FIFO_OVERFLOW_LEVEL 1000U
#define IIS3DWB_FIFO_READ_CHUNK     512U

/* ST driver delay callback doesn't pass handle; keep one active sensor context. */
static IIS3DWB_Sensor_t *g_iis3dwb_active_sensor = 0;

static bool iis3dwb_wait_reset_clear(IIS3DWB_Sensor_t *sensor, uint32_t stage_base)
{
    uint8_t rst = 0U;
    uint32_t guard = 0U;

    sensor->init_stage = stage_base;
    do {
        if (iis3dwb_reset_get(&sensor->io_ctx, &rst) != 0) {
            sensor->init_stage = stage_base + 1000U;
            return false;
        }
        SensorBus_DelayMs(&sensor->bus_port, 1U);
        ++guard;
    } while ((rst != 0U) && (guard < 100U));

    if (rst != 0U) {
        sensor->init_stage = stage_base + 1001U;
        return false;
    }

    return true;
}

static bool iis3dwb_wait_boot_clear(IIS3DWB_Sensor_t *sensor, uint32_t stage_base)
{
    uint8_t boot = 0U;
    uint32_t guard = 0U;

    sensor->init_stage = stage_base;
    do {
        if (iis3dwb_boot_get(&sensor->io_ctx, &boot) != 0) {
            sensor->init_stage = stage_base + 1000U;
            return false;
        }
        SensorBus_DelayMs(&sensor->bus_port, 1U);
        ++guard;
    } while ((boot != 0U) && (guard < 100U));

    if (boot != 0U) {
        sensor->init_stage = stage_base + 1001U;
        return false;
    }

    return true;
}

static bool iis3dwb_prepare_clean_start(IIS3DWB_Sensor_t *sensor)
{
    iis3dwb_pin_int2_route_t int2_route;

    sensor->init_stage = 30U;
    memset(&int2_route, 0, sizeof(int2_route));
    (void)iis3dwb_pin_int2_route_set(&sensor->io_ctx, &int2_route);
    (void)iis3dwb_fifo_mode_set(&sensor->io_ctx, IIS3DWB_BYPASS_MODE);
    (void)iis3dwb_xl_data_rate_set(&sensor->io_ctx, IIS3DWB_XL_ODR_OFF);
    (void)iis3dwb_timestamp_set(&sensor->io_ctx, PROPERTY_DISABLE);
    SensorBus_DelayMs(&sensor->bus_port, 2U);

    sensor->init_stage = 31U;
    if (iis3dwb_boot_set(&sensor->io_ctx, PROPERTY_ENABLE) != 0) {
        sensor->init_stage = 1031U;
        return false;
    }
    if (!iis3dwb_wait_boot_clear(sensor, 32U)) {
        return false;
    }

    sensor->init_stage = 34U;
    if (iis3dwb_reset_set(&sensor->io_ctx, PROPERTY_ENABLE) != 0) {
        sensor->init_stage = 1034U;
        return false;
    }
    if (!iis3dwb_wait_reset_clear(sensor, 35U)) {
        return false;
    }

    return true;
}

static int32_t iis3dwb_bus_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
    IIS3DWB_Sensor_t *sensor = (IIS3DWB_Sensor_t *)handle;
    return SensorBus_WriteReg(&sensor->bus_port, reg, bufp, len);
}

static int32_t iis3dwb_bus_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
    IIS3DWB_Sensor_t *sensor = (IIS3DWB_Sensor_t *)handle;
    return SensorBus_ReadReg(&sensor->bus_port, reg, bufp, len);
}

static void iis3dwb_bus_delay(uint32_t ms)
{
    if (g_iis3dwb_active_sensor != 0) {
        SensorBus_DelayMs(&g_iis3dwb_active_sensor->bus_port, ms);
    }
}

bool IIS3DWB_Sensor_Create(IIS3DWB_Sensor_t *sensor, const SensorBusPort_t *bus_port, float32_t full_scale_g)
{
    if (sensor == 0 || bus_port == 0 || bus_port->vtable == 0) {
        return false;
    }

    sensor->bus_port = *bus_port;
    sensor->full_scale_g = full_scale_g;
    sensor->fifo_watermark = 0U;
    sensor->fs_mode = 0U;
    sensor->init_stage = 0U;
    g_iis3dwb_active_sensor = sensor;
    sensor->io_ctx.write_reg = iis3dwb_bus_write;
    sensor->io_ctx.read_reg = iis3dwb_bus_read;
    sensor->io_ctx.mdelay = iis3dwb_bus_delay;
    sensor->io_ctx.handle = sensor;

    return true;
}

bool IIS3DWB_Sensor_Init(IIS3DWB_Sensor_t *sensor, uint16_t fifo_watermark, uint8_t fs_mode)
{
    uint8_t whoamI = 0U;
    iis3dwb_pin_int2_route_t int2_route;

    if (sensor == 0) {
        return false;
    }

    sensor->init_stage = 10U;
    sensor->fifo_watermark = fifo_watermark;
    sensor->fs_mode = fs_mode;
    SensorBus_DelayMs(&sensor->bus_port, BOOT_TIME);

    sensor->init_stage = 20U;
    if (iis3dwb_device_id_get(&sensor->io_ctx, &whoamI) != 0) {
        sensor->init_stage = 1020U;
        return false;
    }
    if (whoamI != IIS3DWB_ID) {
        sensor->init_stage = 1021U;
        return false;
    }

    if (!iis3dwb_prepare_clean_start(sensor)) {
        return false;
    }

    sensor->init_stage = 40U;
    if (iis3dwb_auto_increment_set(&sensor->io_ctx, PROPERTY_ENABLE) != 0) {
        sensor->init_stage = 1040U;
        return false;
    }
    sensor->init_stage = 41U;
    if (iis3dwb_block_data_update_set(&sensor->io_ctx, PROPERTY_ENABLE) != 0) {
        sensor->init_stage = 1041U;
        return false;
    }
    sensor->init_stage = 42U;
    if (iis3dwb_xl_full_scale_set(&sensor->io_ctx, fs_mode) != 0) {
        sensor->init_stage = 1042U;
        return false;
    }
    sensor->init_stage = 43U;
    if (iis3dwb_fifo_watermark_set(&sensor->io_ctx, fifo_watermark) != 0) {
        sensor->init_stage = 1043U;
        return false;
    }
    sensor->init_stage = 44U;
    if (iis3dwb_fifo_xl_batch_set(&sensor->io_ctx, IIS3DWB_XL_BATCHED_AT_26k7Hz) != 0) {
        sensor->init_stage = 1044U;
        return false;
    }
    sensor->init_stage = 45U;
    if (iis3dwb_fifo_mode_set(&sensor->io_ctx, IIS3DWB_STREAM_MODE) != 0) {
        sensor->init_stage = 1045U;
        return false;
    }
    sensor->init_stage = 46U;
    memset(&int2_route, 0, sizeof(int2_route));
    int2_route.fifo_th = 1U;
    if (iis3dwb_pin_int2_route_set(&sensor->io_ctx, &int2_route) != 0) {
        sensor->init_stage = 1046U;
        return false;
    }
    sensor->init_stage = 47U;
    if (iis3dwb_xl_data_rate_set(&sensor->io_ctx, IIS3DWB_XL_ODR_26k7Hz) != 0) {
        sensor->init_stage = 1047U;
        return false;
    }
    sensor->init_stage = 48U;
    if (iis3dwb_fifo_timestamp_batch_set(&sensor->io_ctx, IIS3DWB_DEC_32) != 0) {
        sensor->init_stage = 1048U;
        return false;
    }
    sensor->init_stage = 49U;
    if (iis3dwb_timestamp_set(&sensor->io_ctx, PROPERTY_ENABLE) != 0) {
        sensor->init_stage = 1049U;
        return false;
    }

    sensor->init_stage = 100U;
    return true;
}

bool IIS3DWB_Sensor_Recover(IIS3DWB_Sensor_t *sensor)
{
    if (sensor == 0 || sensor->fifo_watermark == 0U) {
        return false;
    }

    return IIS3DWB_Sensor_Init(sensor, sensor->fifo_watermark, sensor->fs_mode);
}

bool IIS3DWB_Sensor_FetchSamples(IIS3DWB_Sensor_t *sensor,
                                 float32_t *raw_x,
                                 float32_t *raw_y,
                                 float32_t *raw_z,
                                 uint16_t sample_offset,
                                 uint16_t max_samples,
                                 uint16_t *out_count)
{
    iis3dwb_fifo_status_t fifo_status;
    uint16_t write_index = sample_offset;
    uint16_t last_fifo_level = 0xFFFFU;
    uint16_t loop_guard = 0U;

    if (sensor == 0 || raw_x == 0 || raw_y == 0 || raw_z == 0 || out_count == 0 || sample_offset >= max_samples) {
        return false;
    }

    *out_count = 0U;
    if (iis3dwb_fifo_status_get(&sensor->io_ctx, &fifo_status) != 0) {
        return false;
    }

    if (fifo_status.fifo_level == 0U) {
        return true;
    }

    if ((fifo_status.fifo_ovr != 0U) || (fifo_status.fifo_level >= IIS3DWB_FIFO_OVERFLOW_LEVEL)) {
        if (iis3dwb_fifo_mode_set(&sensor->io_ctx, IIS3DWB_BYPASS_MODE) != 0) {
            return false;
        }
        if (iis3dwb_fifo_mode_set(&sensor->io_ctx, IIS3DWB_STREAM_MODE) != 0) {
            return false;
        }
        if (iis3dwb_fifo_status_get(&sensor->io_ctx, &fifo_status) != 0) {
            return false;
        }
        if (fifo_status.fifo_level == 0U) {
            return true;
        }
    }

    while ((fifo_status.fifo_level > 0U) && (write_index < max_samples)) {
        uint16_t chunk = fifo_status.fifo_level;
        uint16_t write_index_before = write_index;
        static iis3dwb_fifo_out_raw_t fifo_data[IIS3DWB_FIFO_READ_CHUNK];

        if (loop_guard++ > 32U) {
            break;
        }

        if (chunk > IIS3DWB_FIFO_READ_CHUNK) {
            chunk = IIS3DWB_FIFO_READ_CHUNK;
        }

        if (iis3dwb_fifo_out_multi_raw_get(&sensor->io_ctx, fifo_data, chunk) != 0) {
            return false;
        }

        for (uint16_t k = 0U; k < chunk && write_index < max_samples; ++k) {
            iis3dwb_fifo_out_raw_t *sample = &fifo_data[k];
            if ((sample->tag >> 3) == IIS3DWB_XL_TAG) {
                int16_t *datax = (int16_t *)&sample->data[0];
                int16_t *datay = (int16_t *)&sample->data[2];
                int16_t *dataz = (int16_t *)&sample->data[4];

                raw_x[write_index] = ((float32_t)(*datax) / 32767.0f) * sensor->full_scale_g;
                raw_y[write_index] = ((float32_t)(*datay) / 32767.0f) * sensor->full_scale_g;
                raw_z[write_index] = ((float32_t)(*dataz) / 32767.0f) * sensor->full_scale_g;
                ++write_index;
            }
        }

        if (iis3dwb_fifo_status_get(&sensor->io_ctx, &fifo_status) != 0) {
            return false;
        }

        if ((write_index == write_index_before) && (fifo_status.fifo_level == last_fifo_level)) {
            break;
        }
        last_fifo_level = fifo_status.fifo_level;
    }

    *out_count = (uint16_t)(write_index - sample_offset);
    return true;
}

bool IIS3DWB_Sensor_ReadTemperatureX10(IIS3DWB_Sensor_t *sensor, int16_t *temperature_x10)
{
    uint8_t status = 0U;
    int16_t temperature_raw = 0;
    float temperature_c = 25.0f;

    if (sensor == 0 || temperature_x10 == 0) {
        return false;
    }

    if (iis3dwb_temp_flag_data_ready_get(&sensor->io_ctx, &status) != 0) {
        return false;
    }
    if (status == 0U) {
        return false;
    }

    if (iis3dwb_temperature_raw_get(&sensor->io_ctx, &temperature_raw) != 0) {
        return false;
    }
    temperature_c = ((float)temperature_raw / 256.0f) + 25.0f;
    *temperature_x10 = (int16_t)(temperature_c * 10.0f);
    return true;
}
