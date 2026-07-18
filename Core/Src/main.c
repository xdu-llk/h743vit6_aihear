/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "dma.h"
#include "sai.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "led.h"
#include "buzzer.h"
#include "serial_io.h"
#include "audio.h"
#include "wifi_iot.h"
#include "alarm.h"
#include "rgb_led.h"
#include "tflm_runner.h"
#include "audio_preproc.h"
#include "arm_math.h"
#include <string.h>

void FreeRTOS_Init(void);
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

/* USER CODE BEGIN PV */
static volatile uint32_t irq_tick = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* AXI SRAM cold-boot: clear all 512KB for ECC parity init */
  memset((void *)0x24000000, 0, 512 * 1024);

  /* Boot watchdog: auto-retry if cold boot hangs (~4s, dual-model init) */
  __HAL_RCC_LSI_ENABLE();
  while (!__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY)) {}
  IWDG1->KR = 0x5555;
  IWDG1->PR = 5;
  IWDG1->RLR = 999;
  IWDG1->KR = 0xAAAA;
  IWDG1->KR = 0xCCCC;
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_TIM3_Init();
  MX_TIM6_Init();
  MX_UART4_Init();
  MX_SAI1_Init();
  /* USER CODE BEGIN 2 */
  Buzzer_Init();
  Serial_Init();
  Audio_Init();
  Alarm_Init();
  RGB_Init();

  AudioPreproc_Init();
  printf("[PREPROC] Log-mel spectrogram ready (%dx%d)\n",
         PREPROC_NUM_MELS, PREPROC_NUM_FRAMES);

  printf("[TFLM] Initializing Baby-Cry...\n");
  TFLM_BabyCry_Init();
  printf("[TFLM] Initializing KWS...\n");
  TFLM_KWS_Init();
  printf("[TFLM] Both models ready.\n");

  /* Dual-model: KWS (help-call) + DS-CNN (baby_cry), 40x96 input */

  /* Drain UART4 RX FIFO — ESP8266 may have sent junk during STM32 reset */
  HAL_UART_DeInit(&huart4);
  MX_UART4_Init();
  __HAL_UART_FLUSH_DRREGISTER(&huart4);

  WifiIoT_Init();
  HAL_Delay(200);  /* let UART4 settle after ESP32 boot burst */

  printf("[MQTT] Waiting for ESP8266...\r\n");
  for (int t = 0; t < 40; t++) {
    WifiIoT_QueryStatus();
    WifiIoT_Process();
    if (WifiIoT_IsReady()) break;
    if (t % 10 == 9) printf("  retry %d...\r\n", t + 1);
    HAL_Delay(250);
  }
  if (WifiIoT_IsReady()) {
    printf("[MQTT] Ready! Sending test publish...\r\n");
    WifiIoT_Publish("aihear/test", "hello_from_stm32");
    printf("[MQTT] Test publish sent\r\n");
  } else {
    printf("[MQTT] ESP32 not responding\r\n");
  }

  /* Wait for ring buffer to fill (DMA needs ~500ms for 8192 samples @ 16kHz) */
  printf("[AUDIO] Waiting for ring buffer to fill...\r\n");
  HAL_Delay(600);
  printf("[AUDIO] Ready.\r\n");

  /* IWDG: ~8s watchdog, auto-reset if main loop hangs */
  __HAL_RCC_LSI_ENABLE();
  while (!__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY)) {}
  IWDG1->KR = 0x5555;   /* Unlock */
  IWDG1->PR = 6;        /* /256 = 125Hz from 32kHz */
  IWDG1->RLR = 999;     /* 999/125 = ~8s */
  IWDG1->KR = 0xAAAA;   /* Reload */
  IWDG1->KR = 0xCCCC;   /* Start */

  /* Start TIM6 PWM for RGB LED — AFTER all init to avoid IRQ interference */
  __HAL_TIM_SET_AUTORELOAD(&htim6, 99);
  HAL_TIM_Base_Start_IT(&htim6);
  /* USER CODE END 2 */

  /* Start FreeRTOS scheduler — never returns */
  /* USER CODE BEGIN WHILE */
  FreeRTOS_Init();
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6) {
    RGB_Tick();
    irq_tick++;
    if (irq_tick >= 5000) {   /* 5000 × 0.1ms = 500ms */
      irq_tick = 0;
      LED_Toggle();
    }
  }
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
