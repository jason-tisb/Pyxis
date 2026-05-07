#ifndef TASKS_SENSORS_IIS3DWB_SENSOR_H_
#define TASKS_SENSORS_IIS3DWB_SENSOR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "arm_math.h"
#include "iis3dwb_reg.h"
#include "sensor_bus_port.h"

typedef struct {
    stmdev_ctx_t io_ctx;
    SensorBusPort_t bus_port;
    float32_t full_scale_g;
} IIS3DWB_Sensor_t;

bool IIS3DWB_Sensor_Create(IIS3DWB_Sensor_t *sensor, const SensorBusPort_t *bus_port, float32_t full_scale_g);
bool IIS3DWB_Sensor_Init(IIS3DWB_Sensor_t *sensor, uint16_t fifo_watermark, uint8_t fs_mode);
bool IIS3DWB_Sensor_FetchSamples(IIS3DWB_Sensor_t *sensor,
                                 float32_t *raw_x,
                                 float32_t *raw_y,
                                 float32_t *raw_z,
                                 uint16_t sample_offset,
                                 uint16_t max_samples,
                                 uint16_t *out_count);
bool IIS3DWB_Sensor_ReadTemperatureX10(IIS3DWB_Sensor_t *sensor, int16_t *temperature_x10);

#ifdef __cplusplus
}
#endif

#endif
