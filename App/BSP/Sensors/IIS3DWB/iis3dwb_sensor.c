#include "iis3dwb_sensor.h"
#include "vibration_product_config.h"

#define IIS3DWB_FIFO_OVERFLOW_LEVEL 500U
#define IIS3DWB_FIFO_READ_CHUNK     512U

/* ST driver delay callback doesn't pass handle; keep one active sensor context. */
static IIS3DWB_Sensor_t *g_iis3dwb_active_sensor = 0;

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
    uint8_t rst = 0U;
    uint32_t guard = 0U;

    if (sensor == 0) {
        return false;
    }

    SensorBus_DelayMs(&sensor->bus_port, BOOT_TIME);

    if (iis3dwb_device_id_get(&sensor->io_ctx, &whoamI) != 0) {
        return false;
    }
    if (whoamI != IIS3DWB_ID) {
        return false;
    }

    if (iis3dwb_reset_set(&sensor->io_ctx, PROPERTY_ENABLE) != 0) {
        return false;
    }
    do {
        if (iis3dwb_reset_get(&sensor->io_ctx, &rst) != 0) {
            return false;
        }
        SensorBus_DelayMs(&sensor->bus_port, 1U);
        ++guard;
    } while ((rst != 0U) && (guard < 100U));

    if (rst != 0U) {
        return false;
    }

    if (iis3dwb_auto_increment_set(&sensor->io_ctx, PROPERTY_ENABLE) != 0) {
        return false;
    }
    if (iis3dwb_block_data_update_set(&sensor->io_ctx, PROPERTY_ENABLE) != 0) {
        return false;
    }
    if (iis3dwb_xl_full_scale_set(&sensor->io_ctx, fs_mode) != 0) {
        return false;
    }
    if (iis3dwb_fifo_watermark_set(&sensor->io_ctx, fifo_watermark) != 0) {
        return false;
    }
    if (iis3dwb_fifo_xl_batch_set(&sensor->io_ctx, IIS3DWB_XL_BATCHED_AT_26k7Hz) != 0) {
        return false;
    }
    if (iis3dwb_fifo_mode_set(&sensor->io_ctx, IIS3DWB_STREAM_MODE) != 0) {
        return false;
    }
    if (iis3dwb_xl_data_rate_set(&sensor->io_ctx, IIS3DWB_XL_ODR_26k7Hz) != 0) {
        return false;
    }
    if (iis3dwb_fifo_timestamp_batch_set(&sensor->io_ctx, IIS3DWB_DEC_32) != 0) {
        return false;
    }
    if (iis3dwb_timestamp_set(&sensor->io_ctx, PROPERTY_ENABLE) != 0) {
        return false;
    }

    return true;
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

    if (fifo_status.fifo_level >= IIS3DWB_FIFO_OVERFLOW_LEVEL) {
        if (iis3dwb_fifo_mode_set(&sensor->io_ctx, IIS3DWB_BYPASS_MODE) != 0) {
            return false;
        }
        if (iis3dwb_fifo_mode_set(&sensor->io_ctx, IIS3DWB_STREAM_MODE) != 0) {
            return false;
        }
        return true;
    }

    while ((fifo_status.fifo_level > 0U) && (write_index < max_samples)) {
        uint16_t chunk = fifo_status.fifo_level;
        static iis3dwb_fifo_out_raw_t fifo_data[IIS3DWB_FIFO_READ_CHUNK];

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
