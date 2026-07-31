#include "mq135.h"
#include "adc.h"

#define MQ135_ADC_SAMPLES 16U

bool Mq135_Init(void)
{
  return Adc1_Calibrate();
}

bool Mq135_ReadRaw(uint16_t *raw)
{
  return Adc1_ReadChannel(ADC_CHANNEL_15, ADC_SAMPLETIME_64CYCLES_5,
                          MQ135_ADC_SAMPLES, raw);
}
