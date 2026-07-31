#include "gp2y1010.h"

#include "adc.h"

#define GP2Y_ILED_PORT GPIOD
#define GP2Y_ILED_PIN  GPIO_PIN_7
#define GP2Y_SAMPLE_DELAY_US 280U
#define GP2Y_LED_OFF_DELAY_US  40U
#define GP2Y_PULSE_PERIOD_MS    10U
#define GP2Y_BURST_SAMPLES       9U

static uint32_t s_cycles_per_us;
static bool s_initialized;

static void DelayUs(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * s_cycles_per_us;
  while ((uint32_t)(DWT->CYCCNT - start) < ticks) {}
}

bool Gp2y1010_Init(void)
{
  __HAL_RCC_GPIOD_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GP2Y_ILED_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_WritePin(GP2Y_ILED_PORT, GP2Y_ILED_PIN, GPIO_PIN_RESET);
  HAL_GPIO_Init(GP2Y_ILED_PORT, &gpio);

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  s_cycles_per_us = SystemCoreClock / 1000000U;
  if (s_cycles_per_us == 0U) s_cycles_per_us = 1U;

  s_initialized = Adc1_Calibrate();
  return s_initialized;
}

bool Gp2y1010_ReadRaw(uint16_t *raw)
{
  if (!s_initialized || raw == NULL) return false;

  if (!Adc1_SelectChannel(ADC_CHANNEL_11, ADC_SAMPLETIME_8CYCLES_5))
    return false;

  uint16_t discard;
  if (!Adc1_ReadSelected(1U, &discard)) return false;

  uint16_t samples[GP2Y_BURST_SAMPLES];
  for (uint8_t i = 0; i < GP2Y_BURST_SAMPLES; i++) {
    HAL_GPIO_WritePin(GP2Y_ILED_PORT, GP2Y_ILED_PIN, GPIO_PIN_SET);
    DelayUs(GP2Y_SAMPLE_DELAY_US);
    bool ok = Adc1_ReadSelected(1U, &samples[i]);
    DelayUs(GP2Y_LED_OFF_DELAY_US);
    HAL_GPIO_WritePin(GP2Y_ILED_PORT, GP2Y_ILED_PIN, GPIO_PIN_RESET);
    if (!ok) return false;
    if (i + 1U < GP2Y_BURST_SAMPLES) HAL_Delay(GP2Y_PULSE_PERIOD_MS);
  }

  for (uint8_t i = 1; i < GP2Y_BURST_SAMPLES; i++) {
    uint16_t value = samples[i];
    uint8_t j = i;
    while (j > 0U && samples[j - 1U] > value) {
      samples[j] = samples[j - 1U];
      j--;
    }
    samples[j] = value;
  }
  *raw = samples[GP2Y_BURST_SAMPLES / 2U];
  return true;
}
