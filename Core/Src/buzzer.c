#include "buzzer.h"
#include "tim.h"

void Buzzer_Init(void)
{
  /* TIM3 CH3 PWM on PB0 */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);  /* off */
}

void Buzzer_On(void)
{
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 150);  /* 15% duty — gentle */
}

void Buzzer_Off(void)
{
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);  /* off */
}

/* Siren effect: sweep 800Hz ↔ 1600Hz, 3 cycles */
void Buzzer_Siren(void)
{
  /* Gentle reminder tone */
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 150);
  HAL_Delay(600);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
}
