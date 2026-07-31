#include "adc.h"

ADC_HandleTypeDef hadc1;
static bool s_calibrated;

void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef channel = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_16B;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

  channel.Channel = ADC_CHANNEL_15;
  channel.Rank = ADC_REGULAR_RANK_1;
  channel.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
  channel.SingleDiff = ADC_SINGLE_ENDED;
  channel.OffsetNumber = ADC_OFFSET_NONE;
  channel.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) Error_Handler();
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *adc)
{
  if (adc->Instance != ADC1) return;

  /* PLL2P is already configured by SAI3 before ADC1 is initialized. */
  __HAL_RCC_ADC_CONFIG(RCC_ADCCLKSOURCE_PLL2);
  __HAL_RCC_ADC12_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GPIO_PIN_3;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_1;
  HAL_GPIO_Init(GPIOC, &gpio);
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *adc)
{
  if (adc->Instance != ADC1) return;
  __HAL_RCC_ADC12_CLK_DISABLE();
  HAL_GPIO_DeInit(GPIOA, GPIO_PIN_3);
  HAL_GPIO_DeInit(GPIOC, GPIO_PIN_1);
}

bool Adc1_Calibrate(void)
{
  if (s_calibrated) return true;
  s_calibrated = HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET,
                                              ADC_SINGLE_ENDED) == HAL_OK;
  return s_calibrated;
}

bool Adc1_SelectChannel(uint32_t channel, uint32_t sampling_time)
{
  if (!s_calibrated) return false;

  ADC_ChannelConfTypeDef config = {0};
  config.Channel = channel;
  config.Rank = ADC_REGULAR_RANK_1;
  config.SamplingTime = sampling_time;
  config.SingleDiff = ADC_SINGLE_ENDED;
  config.OffsetNumber = ADC_OFFSET_NONE;
  config.Offset = 0;
  return HAL_ADC_ConfigChannel(&hadc1, &config) == HAL_OK;
}

bool Adc1_ReadSelected(uint16_t sample_count, uint16_t *raw)
{
  if (!s_calibrated || sample_count == 0U || raw == NULL) return false;

  uint64_t sum = 0;
  for (uint16_t i = 0; i < sample_count; i++) {
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return false;
    if (HAL_ADC_PollForConversion(&hadc1, 5) != HAL_OK) {
      HAL_ADC_Stop(&hadc1);
      return false;
    }
    sum += HAL_ADC_GetValue(&hadc1);
    if (HAL_ADC_Stop(&hadc1) != HAL_OK) return false;
  }

  *raw = (uint16_t)((sum + sample_count / 2U) / sample_count);
  return true;
}

bool Adc1_ReadChannel(uint32_t channel, uint32_t sampling_time,
                      uint16_t sample_count, uint16_t *raw)
{
  return Adc1_SelectChannel(channel, sampling_time) &&
         Adc1_ReadSelected(sample_count, raw);
}
