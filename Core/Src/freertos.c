/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* Failure reason: 1 = task stack overflow, 2 = FreeRTOS heap exhausted. */
volatile uint32_t g_rtos_failure_reason;
TaskHandle_t volatile g_rtos_failed_task;
const char * volatile g_rtos_failed_task_name;

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
  taskDISABLE_INTERRUPTS();
  g_rtos_failed_task = task;
  g_rtos_failed_task_name = task_name;
  g_rtos_failure_reason = 1U;
  /* The task name pointer may refer to corrupted memory; do not dereference it. */
  for (;;) { __NOP(); }
}

void vApplicationMallocFailedHook(void)
{
  taskDISABLE_INTERRUPTS();
  g_rtos_failure_reason = 2U;
  for (;;) { __NOP(); }
}

/* USER CODE END Application */

