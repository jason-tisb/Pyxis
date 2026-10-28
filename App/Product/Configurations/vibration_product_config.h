/*
 * vibration_product_config.h
 *
 * Product-specific defaults and bindings for the vibration feature.
 */

#pragma once
#include "main.h"
extern SPI_HandleTypeDef hspi1;
#define VB001
#define BOOT_TIME                          10U
#define FIFO_WATERMARK                     354U
#define IIS3DWB_TEMPERATURE_PERIOD_MS      1000U
/* Temporary FIFO/EXTI diagnostic switch.  Set to 1U to restore periodic
 * IIS3DWB temperature reads after vibration acquisition is verified. */
#define VIBRATION_TEMPERATURE_SERVICE_ENABLE 1U
#define RESULT_DATA_PER_AXIS               50U
#define QUANTITY_USED_FFT_SAMPLE           200U /* never more than FFT_LENGTH / 2 */
#define SENSOR_BUS                         hspi1
#define CS_up_GPIO_Port                    VBS_CS_GPIO_Port
#define CS_up_Pin                          VBS_CS_Pin
#define PWM_3V3                            915U
#define IIS3DWB_XL_FS                      IIS3DWB_8g
#define FFT_LENGTH                         4096U
#define FS                                 26667.0f
#define BIN_WIDTH                          (FS / (float)FFT_LENGTH)
#define FREQ_BASE                          50.0f
#define RPM                                (FREQ_BASE * 60.0f)
#define BEARING_NUMBER_OF_ROLLING_ELEMENTS 8U
#define BEARING_PITCH_DIAMETER             0.072f
#define BEARING_ROLLING_DIAMETER           0.012f
#define BEARING_CONTACT_ANGLE              0.0f
#define GEAR_TEETH_NUMBER                  8U
#define GEAR_MESH_FREQ                     (GEAR_TEETH_NUMBER * FREQ_BASE)

#define ISO_CLASS                          2
