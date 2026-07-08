#include "rgb_led.h"

/* 3 GPIO pins — all on board headers */
#define RGB_PORT_R  GPIOB
#define RGB_PIN_R   GPIO_PIN_4   /* PB4 */
#define RGB_PORT_G  GPIOB
#define RGB_PIN_G   GPIO_PIN_5   /* PB5 */
#define RGB_PORT_B  GPIOB
#define RGB_PIN_B   GPIO_PIN_8   /* PB8 */

/* Predefined colors */
const RGB_Color RGB_BLACK  = {   0,   0,   0 };
const RGB_Color RGB_RED    = { 255,   0,   0 };
const RGB_Color RGB_GREEN  = {   0, 255,   0 };
const RGB_Color RGB_BLUE   = {   0,   0, 255 };
const RGB_Color RGB_YELLOW = { 255, 255,   0 };
const RGB_Color RGB_CYAN   = {   0, 255, 255 };
const RGB_Color RGB_WHITE  = { 255, 255, 255 };

/* Software PWM state — 8-bit (256 levels), ~39Hz at 10kHz tick */
static uint8_t target_r, target_g, target_b;
static uint8_t cnt = 0;

void RGB_Init(void)
{
  GPIO_InitTypeDef g = {0};
  g.Pin   = RGB_PIN_R | RGB_PIN_G | RGB_PIN_B;
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_PULLDOWN;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &g);

  HAL_GPIO_WritePin(GPIOB, RGB_PIN_R | RGB_PIN_G | RGB_PIN_B, GPIO_PIN_RESET);
}

void RGB_Set(uint8_t r, uint8_t g, uint8_t b)
{
  target_r = r;
  target_g = g;
  target_b = b;
}

void RGB_SetColor(RGB_Color c)
{
  RGB_Set(c.r, c.g, c.b);
}

void RGB_Tick(void)
{
  cnt++;

  /* R */
  if (cnt == 0)
    HAL_GPIO_WritePin(RGB_PORT_R, RGB_PIN_R, target_r > 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  else if (cnt == target_r && target_r < 255)
    HAL_GPIO_WritePin(RGB_PORT_R, RGB_PIN_R, GPIO_PIN_RESET);

  /* G */
  if (cnt == 0)
    HAL_GPIO_WritePin(RGB_PORT_G, RGB_PIN_G, target_g > 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  else if (cnt == target_g && target_g < 255)
    HAL_GPIO_WritePin(RGB_PORT_G, RGB_PIN_G, GPIO_PIN_RESET);

  /* B */
  if (cnt == 0)
    HAL_GPIO_WritePin(RGB_PORT_B, RGB_PIN_B, target_b > 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  else if (cnt == target_b && target_b < 255)
    HAL_GPIO_WritePin(RGB_PORT_B, RGB_PIN_B, GPIO_PIN_RESET);
}
