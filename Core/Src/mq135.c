#include "mq135.h"
#include "adc.h"

#define MQ135_ADC_SAMPLES 16U

bool Mq135_Init(void)
{
  return Adc1_Calibrate();
}

bool Mq135_ReadRaw(uint16_t *raw)
{
  if (raw == NULL ||
      !Adc1_SelectChannel(ADC_CHANNEL_15, ADC_SAMPLETIME_64CYCLES_5)) {
    return false;
  }

  uint16_t discard;
  if (!Adc1_ReadSelected(1U, &discard)) return false;
  return Adc1_ReadSelected(MQ135_ADC_SAMPLES, raw);
}
