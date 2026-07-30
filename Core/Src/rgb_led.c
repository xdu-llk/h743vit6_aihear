#include "rgb_led.h"

/* 3 GPIO pins — all on board headers */
#define RGB_PORT_R  GPIOB
#define RGB_PIN_R   GPIO_PIN_4   /* PB4 */
#define RGB_PORT_G  GPIOB
#define RGB_PIN_G   GPIO_PIN_5   /* PB5 */
#define RGB_PORT_B  GPIOB
#define RGB_PIN_B   GPIO_PIN_3   /* PB3; adjacent to PB4/PB5 on the header */

/* Predefined colors */
const RGB_Color RGB_BLACK  = {   0,   0,   0 };
const RGB_Color RGB_RED    = { 255,   0,   0 };
const RGB_Color RGB_GREEN  = {   0, 255,   0 };
const RGB_Color RGB_BLUE   = {   0,   0, 255 };
const RGB_Color RGB_YELLOW = { 255, 255,   0 };
const RGB_Color RGB_CYAN   = {   0, 255, 255 };
const RGB_Color RGB_WHITE  = { 255, 255, 255 };

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
  HAL_GPIO_WritePin(RGB_PORT_R, RGB_PIN_R,
                    r ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RGB_PORT_G, RGB_PIN_G,
                    g ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(RGB_PORT_B, RGB_PIN_B,
                    b ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void RGB_SetColor(RGB_Color c)
{
  RGB_Set(c.r, c.g, c.b);
}
