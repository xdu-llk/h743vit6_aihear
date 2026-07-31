#ifndef ADC_H
#define ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern ADC_HandleTypeDef hadc1;

void MX_ADC1_Init(void);
bool Adc1_Calibrate(void);
bool Adc1_SelectChannel(uint32_t channel, uint32_t sampling_time);
bool Adc1_ReadSelected(uint16_t sample_count, uint16_t *raw);
bool Adc1_ReadChannel(uint32_t channel, uint32_t sampling_time,
                      uint16_t sample_count, uint16_t *raw);

#ifdef __cplusplus
}
#endif

#endif
