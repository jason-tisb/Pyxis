/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h5xx_it.c
  * @brief   Interrupt Service Routines.
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
#include "main.h"
#include "stm32h5xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "mbport.h"
#include "modbus_engine_config.h"
#include "modbus_engine_port.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */
extern volatile uint8_t ErasingOnGoing;

volatile uint32_t g_usart3_irq_count = 0;
volatile uint32_t g_usart3_idle_count = 0;
volatile uint32_t g_usart3_error_count = 0;
volatile uint32_t g_usart3_txe_count = 0;
volatile uint32_t g_usart3_tc_count = 0;
volatile uint32_t g_gpdma_uart3_irq_count = 0;

#if (FEATURE_STORAGE_USE_RTOS == 1)
extern osSemaphoreId_t  kv_erase_sem;
#endif
/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_spi1_rx;
extern DMA_HandleTypeDef hdma_spi1_tx;
extern DMA_HandleTypeDef hdma_usart3_rx;
extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart3;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim1;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32H5xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h5xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles FLASH global interrupt.
  */
void FLASH_IRQHandler(void)
{
  HAL_FLASH_IRQHandler();
}

/**
 * @brief This function handles EXTI line 9 interrupt.
 */
void EXTI9_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(VBS_INT_Pin);
}

/**
  * @brief This function handles GPDMA1 Channel 0 global interrupt.
  */
void GPDMA1_Channel0_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi1_rx);
}

/**
  * @brief This function handles GPDMA1 Channel 1 global interrupt.
  */
void GPDMA1_Channel1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi1_tx);
}

/**
  * @brief This function handles GPDMA1 Channel 2 global interrupt.
  */
void GPDMA1_Channel2_IRQHandler(void)
{
	g_gpdma_uart3_irq_count++;
  HAL_DMA_IRQHandler(&hdma_usart3_rx);
}

/**
  * @brief This function handles SPI1 global interrupt.
  */
void SPI1_IRQHandler(void)
{
  HAL_SPI_IRQHandler(&hspi1);
}

/**
  * @brief This function handles TIM1 Update interrupt.
  */
void TIM1_UP_IRQHandler(void)
{
  /* USER CODE BEGIN TIM1_UP_IRQn 0 */

  /* USER CODE END TIM1_UP_IRQn 0 */
  HAL_TIM_IRQHandler(&htim1);
  /* USER CODE BEGIN TIM1_UP_IRQn 1 */

  /* USER CODE END TIM1_UP_IRQn 1 */
}

/**
  * @brief This function handles USART3 global interrupt.
  */
void USART3_IRQHandler(void)
{
    uint32_t isr;
    uint32_t cr1;
    uint32_t errorflags;

    g_usart3_irq_count++;

    isr = huart3.Instance->ISR;
    cr1 = huart3.Instance->CR1;

#if MODBUS_ENGINE_ACTIVE && MODBUS_PYXIS_HW_RTO
    if (((isr & USART_ISR_RTOF) != 0U) &&
        ((cr1 & USART_CR1_RTOIE) != 0U))
    {
        __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_RTOF);
        ModbusEnginePort_RxTimeoutIsr();
    }
#endif

    errorflags = isr & (USART_ISR_PE |
                        USART_ISR_FE |
                        USART_ISR_ORE |
                        USART_ISR_NE);
#if !MODBUS_PYXIS_HW_RTO
    errorflags |= isr & USART_ISR_RTOF;
#endif

    if (errorflags != 0U)
    {
        g_usart3_error_count++;
        /* Let HAL clear FE/PE/NE/ORE (and any non-hardware-RTO timeout
         * flag included above) before returning.  Leaving an error flag
         * pending retriggers USART3_IRQHandler continuously and can starve
         * the RTOS, including the autobaud restore path. */
        HAL_UART_IRQHandler(&huart3);
        return;
    }

    if ((huart3.ReceptionType == HAL_UART_RECEPTION_TOIDLE) &&
        ((isr & USART_ISR_IDLE) != 0U) &&
        ((cr1 & USART_CR1_IDLEIE) != 0U))
    {
        g_usart3_idle_count++;
        HAL_UART_IRQHandler(&huart3);
        return;
    }

#if MODBUS_ENGINE_ACTIVE && MODBUS_PYXIS_TX_IT
    /* HAL's interrupt transmitter needs both TXE and TC servicing.  The
     * legacy path only handled TXE, which can leave HAL_UART_Transmit_IT()
     * waiting forever for its completion callback (and can release RS485 DE
     * at the wrong time). */
    if (((isr & USART_ISR_TXE_TXFNF) != 0U) &&
        __HAL_UART_GET_IT_SOURCE(&huart3, UART_IT_TXE))
    {
        HAL_UART_IRQHandler(&huart3);
        return;
    }

    if (((isr & USART_ISR_TC) != 0U) &&
        __HAL_UART_GET_IT_SOURCE(&huart3, UART_IT_TC))
    {
    	g_usart3_tc_count++;
        HAL_UART_IRQHandler(&huart3);
        return;
    }
#else
    if (((isr & USART_ISR_TXE_TXFNF) != 0U) &&
        __HAL_UART_GET_IT_SOURCE(&huart3, UART_IT_TXE))
    {
        g_usart3_txe_count++;
        prvvUARTTxReadyISR();
        return;
    }
#endif
}

void TIM2_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim2);
}

/* USER CODE BEGIN 1 */
void HAL_FLASH_EndOfOperationCallback(uint32_t ReturnValue) {
    if (ErasingOnGoing == 1) {
        ErasingOnGoing = 0;
#if (FEATURE_STORAGE_USE_RTOS == 1)
        if (kv_erase_sem != NULL) {
            osSemaphoreRelease(kv_erase_sem);
        }
#endif
    }
}
/* USER CODE END 1 */
