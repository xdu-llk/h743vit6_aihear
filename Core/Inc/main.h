/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

typedef enum {
  RUNTIME_STAGE_UNKNOWN = 0,
  RUNTIME_STAGE_BOOT,
  RUNTIME_STAGE_AUDIO_WAIT,
  RUNTIME_STAGE_AUDIO_CAPTURE,
  RUNTIME_STAGE_INFERENCE,
  RUNTIME_STAGE_WIFI,
  RUNTIME_STAGE_COMMAND,
  RUNTIME_STAGE_CAMERA_CAPTURE,
  RUNTIME_STAGE_CAMERA_DISPLAY,
  RUNTIME_STAGE_CAMERA_UPLOAD,
  RUNTIME_STAGE_MQ135,
  RUNTIME_STAGE_PARTICLE,
  RUNTIME_STAGE_DHT11,
  RUNTIME_STAGE_UI,
  RUNTIME_STAGE_HEALTHY,
  RUNTIME_STAGE_ERROR_HANDLER,
  RUNTIME_STAGE_HARDFAULT,
  RUNTIME_STAGE_MEMFAULT,
  RUNTIME_STAGE_BUSFAULT,
  RUNTIME_STAGE_USAGEFAULT,
  RUNTIME_STAGE_STACK_OVERFLOW,
  RUNTIME_STAGE_MALLOC_FAILED
} RuntimeStage;

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

void RuntimeStage_Set(RuntimeStage stage);
void RuntimeFault_Record(RuntimeStage stage, uint32_t detail0,
                         uint32_t detail1);
uint32_t RuntimeStage_GetPreserved(uint32_t *detail0, uint32_t *detail1);
const char *RuntimeStage_Name(uint32_t stage);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
