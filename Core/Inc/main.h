/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h5xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_spi1_rx;
extern DMA_HandleTypeDef hdma_spi1_tx;
extern DMA_HandleTypeDef hdma_usart3_rx;

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define VBS_CS_Pin GPIO_PIN_14
#define VBS_CS_GPIO_Port GPIOC
#define LED_PWR_Pin GPIO_PIN_15
#define LED_PWR_GPIO_Port GPIOB
#define LED_STAT_Pin GPIO_PIN_6
#define LED_STAT_GPIO_Port GPIOC
#define VBS_INT_Pin GPIO_PIN_9
#define VBS_INT_GPIO_Port GPIOA
#define TMP_CS_Pin GPIO_PIN_10
#define TMP_CS_GPIO_Port GPIOA
#define TMP_Alert_Pin GPIO_PIN_5
#define TMP_Alert_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
