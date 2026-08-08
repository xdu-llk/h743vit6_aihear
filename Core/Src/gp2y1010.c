#include "gp2y1010.h"

#include "adc.h"

#define GP2Y_ILED_PORT GPIOD
#define GP2Y_ILED_PIN  GPIO_PIN_7
#define GP2Y_SAMPLE_DELAY_US 280U
#define GP2Y_LED_OFF_DELAY_US  40U
#define GP2Y_PULSE_PERIOD_MS    10U
#define GP2Y_BURST_SAMPLES        9U
#define GP2Y_MIN_PULSE_DELTA_RAW 128U
#define GP2Y_MIN_VALID_PULSES      7U

static uint32_t s_cycles_per_us;
static bool s_initialized;
static bool s_signal_present;

static uint16_t Median9(uint16_t *samples)
{
  for (uint8_t i = 1; i < GP2Y_BURST_SAMPLES; i++) {
    uint16_t value = samples[i];
    uint8_t j = i;
    while (j > 0U && samples[j - 1U] > value) {
      samples[j] = samples[j - 1U];
      j--;
    }
    samples[j] = value;
  }
  return samples[GP2Y_BURST_SAMPLES / 2U];
}

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
  s_signal_present = false;
  return s_initialized;
}

bool Gp2y1010_ReadRaw(uint16_t *raw)
{
  if (!s_initialized || raw == NULL) return false;

  if (!Adc1_SelectChannel(ADC_CHANNEL_11, ADC_SAMPLETIME_8CYCLES_5))
    return false;

  uint16_t discard;
  if (!Adc1_ReadSelected(1U, &discard)) return false;
  uint16_t pulse_deltas[GP2Y_BURST_SAMPLES];
  uint8_t valid_pulses = 0U;
  for (uint8_t i = 0; i < GP2Y_BURST_SAMPLES; i++) {
    uint16_t low_sample;
    uint16_t high_sample;
    HAL_GPIO_WritePin(GP2Y_ILED_PORT, GP2Y_ILED_PIN, GPIO_PIN_RESET);
    if (!Adc1_ReadSelected(1U, &low_sample)) return false;
    HAL_GPIO_WritePin(GP2Y_ILED_PORT, GP2Y_ILED_PIN, GPIO_PIN_SET);
    DelayUs(GP2Y_SAMPLE_DELAY_US);
    bool ok = Adc1_ReadSelected(1U, &high_sample);
    DelayUs(GP2Y_LED_OFF_DELAY_US);
    HAL_GPIO_WritePin(GP2Y_ILED_PORT, GP2Y_ILED_PIN, GPIO_PIN_RESET);
    if (!ok) return false;
    uint16_t delta = high_sample > low_sample
                         ? (uint16_t)(high_sample - low_sample)
                         : 0U;
    pulse_deltas[i] = delta;
    if (delta >= GP2Y_MIN_PULSE_DELTA_RAW) valid_pulses++;
    if (i + 1U < GP2Y_BURST_SAMPLES) HAL_Delay(GP2Y_PULSE_PERIOD_MS);
  }

  *raw = Median9(pulse_deltas);
  s_signal_present = valid_pulses >= GP2Y_MIN_VALID_PULSES &&
                     *raw >= GP2Y_MIN_PULSE_DELTA_RAW;
  return true;
}

bool Gp2y1010_HasSignal(void)
{
  return s_signal_present;
}
