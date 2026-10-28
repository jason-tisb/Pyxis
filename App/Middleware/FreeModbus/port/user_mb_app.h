#ifndef USER_APP
#define USER_APP
/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mb_m.h"
#include "mbconfig.h"
#include "mbframe.h"
#include "mbutils.h"
#include "modbus_register_map.h"

#include "FreeRTOS.h"
#include "event_groups.h"

/* -----------------------Slave Defines -------------------------------------*/
#define S_DISCRETE_INPUT_START        	0
#define S_DISCRETE_INPUT_NDISCRETES   	16
#define S_COIL_START                  	0
#define S_COIL_NCOILS                 	64
#define S_REG_INPUT_START             	0
/* Canonical map sizes are owned by modbus_map.h. */
#define S_REG_INPUT_NREGS             	PYXIS_MODBUS_INPUT_STORAGE_SIZE
/* NOTE: holding register map includes the Excel remapping block. */
#define S_REG_HOLDING_START           	0
#define S_REG_HOLDING_NREGS           	PYXIS_MODBUS_HOLDING_STORAGE_SIZE
/* salve mode: holding register's all address */
#define S_HD_RESERVE                  	0
#define S_HD_CPU_USAGE_MAJOR          	1
#define S_HD_CPU_USAGE_MINOR          	2
/* salve mode: input register's all address */
#define S_IN_RESERVE                  	0
/* salve mode: coil's all address */
#define S_CO_RESERVE                  	0
/* salve mode: discrete's all address */
#define S_DI_RESERVE                  	0

/* -----------------------Master Defines -------------------------------------*/
#define M_DISCRETE_INPUT_START        	0
#define M_DISCRETE_INPUT_NDISCRETES   	16
#define M_COIL_START                  	0
#define M_COIL_NCOILS                 	64
#define M_REG_INPUT_START             	0
#define M_REG_INPUT_NREGS             	100
#define M_REG_HOLDING_START           	0
#define M_REG_HOLDING_NREGS           	100
/* master mode: holding register's all address */
#define M_HD_RESERVE                  	0
/* master mode: input register's all address */
#define M_IN_RESERVE                  	0
/* master mode: coil's all address */
#define M_CO_RESERVE                  	0
/* master mode: discrete's all address */
#define M_DI_RESERVE                  	0

extern EventGroupHandle_t xSettingsEventGroup;

#define EVT_SLAVE_ID_CHANGED   			(1 << 0)
#define EVT_BAUDRATE_CHANGED     		(1 << 1)

#define EVT_GEAR_RATIO_CHANGED			(1 << 2)
#define EVT_MOTOR_SPEED_CHANGED			(1 << 3)
#define EVT_BEARING_BALL_COUNT_CHANGED	(1 << 4)
#define EVT_BEARING_BALL_SIZE_CHANGED	(1 << 5)
#define EVT_BEARING_DIAMETER_CHANGED	(1 << 6)
#define EVT_BEARING_ALPHA_ANGLE_CHANGED	(1 << 7)
#define EVT_FILTER_HIGH_PASS_CHANGED	(1 << 8)
#define EVT_FILTER_LOW_PASS_CHANGED		(1 << 9)

#endif /* USER_APP */
