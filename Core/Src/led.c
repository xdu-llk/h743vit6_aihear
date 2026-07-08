#include "led.h"

void LED_On(void)
{
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
}

void LED_Off(void)
{
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
}

void LED_Toggle(void)
{
  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
}
