/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : FreeRTOS applicative file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "app_freertos.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_led.h"
#include "app_led_config.h"
#include "user_mb_app.h"
#include "sys_storage_partition.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
	osEventFlagsId_t event_start_modbus_thread = NULL;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static osThreadId_t ledTaskHandle;
static const osThreadAttr_t ledTask_attributes = {
  .name = "ledTask",
  //.priority = (osPriority_t) osPriorityLow,
  .priority = (osPriority_t) osPriorityNormal3,
  .stack_size = 512U
};
/* USER CODE END Variables */
/* Definitions for vTaskVibration */
osThreadId_t vTaskVibrationHandle;
const osThreadAttr_t vTaskVibration_attributes = {
  .name = "vTaskVibration",
  .priority = (osPriority_t) osPriorityNormal1,
  .stack_size = 30000 * 4
};
/* Definitions for vTaskModbus */
osThreadId_t vTaskModbusHandle;
const osThreadAttr_t vTaskModbus_attributes = {
  .name = "vTaskModbus",
  .priority = (osPriority_t) osPriorityNormal2,
  .stack_size = 7000 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
// globals
osMutexId_t regbuf_mtx;
const osMutexAttr_t regbuf_mtx_attr = {.name = "regbuf_mtx"};
EventGroupHandle_t xSettingsEventGroup;
static void LED_ServiceTask(void *argument);
/* USER CODE END FunctionPrototypes */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
	event_start_modbus_thread = osEventFlagsNew(NULL);
	regbuf_mtx = osMutexNew(&regbuf_mtx_attr);
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */
  /* USER CODE BEGIN RTOS_EVENTS */
  /* Create events before tasks that consume them to avoid NULL-handle races. */
	xSettingsEventGroup = xEventGroupCreate();
  /* USER CODE END RTOS_EVENTS */

  /* creation of vTaskVibration */
  vTaskVibrationHandle = osThreadNew(main_TaskVibration, NULL, &vTaskVibration_attributes);

  /* creation of vTaskModbus */
   vTaskModbusHandle = osThreadNew(main_TaskModbus, NULL, &vTaskModbus_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  ledTaskHandle = osThreadNew(LED_ServiceTask, NULL, &ledTask_attributes);
  /* USER CODE END RTOS_THREADS */

}
/* USER CODE BEGIN Header_main_TaskVibration */
/**
* @brief Function implementing the vTaskVibration thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_main_TaskVibration */
__weak void main_TaskVibration(void *argument)
{
  /* USER CODE BEGIN vTaskVibration */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END vTaskVibration */
}

/* USER CODE BEGIN Header_main_TaskModbus */
/**
* @brief Function implementing the vTaskModbus thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_main_TaskModbus */
__weak void main_TaskModbus(void *argument)
{
  /* USER CODE BEGIN vTaskModbus */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END vTaskModbus */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
volatile uint32_t g_led_task_runs = 0;
volatile uint32_t g_led_before_service = 0;
volatile uint32_t g_led_after_service = 0;
static void LED_ServiceTask(void *argument)
{
  (void)argument;

  for (;;)
  {
      g_led_task_runs++;
      g_led_before_service++;

      LED_Task_10ms();

      g_led_after_service++;
    osDelay(LED_TASK_PERIOD_MS);
  }
}

/* USER CODE END Application */

