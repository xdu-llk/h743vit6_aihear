#ifndef ST7789_H
#define ST7789_H

#include <stdbool.h>
#include <stdint.h>

#define ST7789_WIDTH  240U
#define ST7789_HEIGHT 240U

#define ST7789_RGB565(r, g, b) \
  ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | \
              (((uint16_t)(g) & 0xFCU) << 3) | ((uint16_t)(b) >> 3)))

bool ST7789_Init(void);
bool ST7789_IsReady(void);
void ST7789_SetBacklight(bool enabled);
void ST7789_FillScreen(uint16_t color);
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t width,
                     uint16_t height, uint16_t color);
bool ST7789_DrawRgb565(uint16_t x, uint16_t y, uint16_t width,
                       uint16_t height, const uint8_t *pixels,
                       uint32_t length);
void ST7789_DrawText(uint16_t x, uint16_t y, const char *text,
                     uint8_t scale, uint16_t foreground, uint16_t background);

#endif
