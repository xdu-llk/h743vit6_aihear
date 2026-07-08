/* button.c — GPIO PC4, pull-up, press = LOW
 * Short press (<3s): edge-triggered toggle
 * Long press (>=3s): hold detection for soft reset */
#include "button.h"

#define BTN_PIN  GPIO_PIN_4
#define BTN_PORT GPIOC

static volatile uint8_t edge_flag = 0;
static uint8_t debounce_cnt = 0;
static uint8_t last_stable = 1;
static uint8_t is_down = 0;
static uint32_t press_since = 0;  /* tick when press confirmed */

void Button_Init(void) {
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef g = {0};
  g.Pin = BTN_PIN;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BTN_PORT, &g);
}

void Button_Poll(void) {
  uint8_t raw = (HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN) == GPIO_PIN_RESET) ? 0 : 1;
  if (raw == 0) {
    if (++debounce_cnt >= 3) {
      if (!is_down) {
        is_down = 1;
        press_since = HAL_GetTick();  /* start timing */
      }
      last_stable = 0;
    }
  } else {
    if (is_down && last_stable == 0) {
      edge_flag = 1;  /* released = short press confirmed */
    }
    debounce_cnt = 0;
    last_stable = 1;
    is_down = 0;
  }
}

uint8_t Button_WasPressed(void) {
  uint8_t r = edge_flag;
  edge_flag = 0;
  return r;
}

uint32_t Button_HeldMs(void) {
  if (!is_down) return 0;
  return HAL_GetTick() - press_since;
}
