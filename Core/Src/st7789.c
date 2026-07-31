#include "st7789.h"
#include "main.h"
#include "spi.h"
#include <ctype.h>
#include <stddef.h>

#define LCD_CS_PORT GPIOE
#define LCD_CS_PIN  GPIO_PIN_11
#define LCD_DC_PORT GPIOE
#define LCD_DC_PIN  GPIO_PIN_15
#define LCD_BL_PORT GPIOD
#define LCD_BL_PIN  GPIO_PIN_15

#define LCD_TIMEOUT_MS 100U
#define LCD_PIXEL_CHUNK 128U

typedef struct {
  char ch;
  uint8_t columns[5];
} Glyph5x7;

static const Glyph5x7 kFont[] = {
  {' ', {0x00,0x00,0x00,0x00,0x00}}, {'!', {0x00,0x00,0x5f,0x00,0x00}},
  {'%', {0x62,0x64,0x08,0x13,0x23}}, {'+', {0x08,0x08,0x3e,0x08,0x08}},
  {'-', {0x08,0x08,0x08,0x08,0x08}}, {'.', {0x00,0x60,0x60,0x00,0x00}},
  {'/', {0x20,0x10,0x08,0x04,0x02}}, {':', {0x00,0x36,0x36,0x00,0x00}},
  {'?', {0x02,0x01,0x51,0x09,0x06}}, {'_', {0x40,0x40,0x40,0x40,0x40}},
  {'0', {0x3e,0x51,0x49,0x45,0x3e}}, {'1', {0x00,0x42,0x7f,0x40,0x00}},
  {'2', {0x42,0x61,0x51,0x49,0x46}}, {'3', {0x21,0x41,0x45,0x4b,0x31}},
  {'4', {0x18,0x14,0x12,0x7f,0x10}}, {'5', {0x27,0x45,0x45,0x45,0x39}},
  {'6', {0x3c,0x4a,0x49,0x49,0x30}}, {'7', {0x01,0x71,0x09,0x05,0x03}},
  {'8', {0x36,0x49,0x49,0x49,0x36}}, {'9', {0x06,0x49,0x49,0x29,0x1e}},
  {'A', {0x7e,0x11,0x11,0x11,0x7e}}, {'B', {0x7f,0x49,0x49,0x49,0x36}},
  {'C', {0x3e,0x41,0x41,0x41,0x22}}, {'D', {0x7f,0x41,0x41,0x22,0x1c}},
  {'E', {0x7f,0x49,0x49,0x49,0x41}}, {'F', {0x7f,0x09,0x09,0x09,0x01}},
  {'G', {0x3e,0x41,0x49,0x49,0x7a}}, {'H', {0x7f,0x08,0x08,0x08,0x7f}},
  {'I', {0x00,0x41,0x7f,0x41,0x00}}, {'J', {0x20,0x40,0x41,0x3f,0x01}},
  {'K', {0x7f,0x08,0x14,0x22,0x41}}, {'L', {0x7f,0x40,0x40,0x40,0x40}},
  {'M', {0x7f,0x02,0x0c,0x02,0x7f}}, {'N', {0x7f,0x04,0x08,0x10,0x7f}},
  {'O', {0x3e,0x41,0x41,0x41,0x3e}}, {'P', {0x7f,0x09,0x09,0x09,0x06}},
  {'Q', {0x3e,0x41,0x51,0x21,0x5e}}, {'R', {0x7f,0x09,0x19,0x29,0x46}},
  {'S', {0x46,0x49,0x49,0x49,0x31}}, {'T', {0x01,0x01,0x7f,0x01,0x01}},
  {'U', {0x3f,0x40,0x40,0x40,0x3f}}, {'V', {0x1f,0x20,0x40,0x20,0x1f}},
  {'W', {0x3f,0x40,0x38,0x40,0x3f}}, {'X', {0x63,0x14,0x08,0x14,0x63}},
  {'Y', {0x07,0x08,0x70,0x08,0x07}}, {'Z', {0x61,0x51,0x49,0x45,0x43}}
};

static bool s_ready;

static void Select(void) { HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET); }
static void Deselect(void) { HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET); }

static bool Transmit(const uint8_t *data, uint16_t length)
{
  return HAL_SPI_Transmit(&hspi4, (uint8_t *)data, length, LCD_TIMEOUT_MS) == HAL_OK;
}

static bool WriteCommandData(uint8_t command, const uint8_t *data, uint16_t length)
{
  bool ok;
  Select();
  HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_RESET);
  ok = Transmit(&command, 1);
  if (ok && length != 0U) {
    HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET);
    ok = Transmit(data, length);
  }
  Deselect();
  return ok;
}

static bool SetWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
  uint16_t x2 = (uint16_t)(x + width - 1U);
  uint16_t y2 = (uint16_t)(y + height - 1U);
  uint8_t columns[4] = {(uint8_t)(x >> 8), (uint8_t)x,
                        (uint8_t)(x2 >> 8), (uint8_t)x2};
  uint8_t rows[4] = {(uint8_t)(y >> 8), (uint8_t)y,
                     (uint8_t)(y2 >> 8), (uint8_t)y2};
  return WriteCommandData(0x2a, columns, sizeof(columns)) &&
         WriteCommandData(0x2b, rows, sizeof(rows)) &&
         WriteCommandData(0x2c, NULL, 0);
}

static const uint8_t *FindGlyph(char ch)
{
  char upper = (char)toupper((unsigned char)ch);
  for (size_t i = 0; i < sizeof(kFont) / sizeof(kFont[0]); ++i) {
    if (kFont[i].ch == upper) return kFont[i].columns;
  }
  return kFont[8].columns;
}

bool ST7789_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);

  gpio.Pin = LCD_CS_PIN | LCD_DC_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOE, &gpio);
  gpio.Pin = LCD_BL_PIN;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &gpio);

  HAL_Delay(10);
  static const uint8_t madctl[] = {0x00};
  static const uint8_t colmod[] = {0x05};
  static const uint8_t porch[] = {0x0c,0x0c,0x00,0x33,0x33};
  static const uint8_t gate[] = {0x35};
  static const uint8_t vcom[] = {0x19};
  static const uint8_t lcm[] = {0x2c};
  static const uint8_t vdv_vrh[] = {0x01};
  static const uint8_t vrh[] = {0x12};
  static const uint8_t vdv[] = {0x20};
  static const uint8_t frame[] = {0x0f};
  static const uint8_t power[] = {0xa4,0xa1};
  static const uint8_t gamma_pos[] = {
    0xd0,0x04,0x0d,0x11,0x13,0x2b,0x3f,0x54,0x4c,0x18,0x0d,0x0b,0x1f,0x23};
  static const uint8_t gamma_neg[] = {
    0xd0,0x04,0x0c,0x11,0x13,0x2c,0x3f,0x44,0x51,0x2f,0x1f,0x1f,0x20,0x23};

  bool ok = WriteCommandData(0x36, madctl, sizeof(madctl)) &&
            WriteCommandData(0x3a, colmod, sizeof(colmod)) &&
            WriteCommandData(0xb2, porch, sizeof(porch)) &&
            WriteCommandData(0xb7, gate, sizeof(gate)) &&
            WriteCommandData(0xbb, vcom, sizeof(vcom)) &&
            WriteCommandData(0xc0, lcm, sizeof(lcm)) &&
            WriteCommandData(0xc2, vdv_vrh, sizeof(vdv_vrh)) &&
            WriteCommandData(0xc3, vrh, sizeof(vrh)) &&
            WriteCommandData(0xc4, vdv, sizeof(vdv)) &&
            WriteCommandData(0xc6, frame, sizeof(frame)) &&
            WriteCommandData(0xd0, power, sizeof(power)) &&
            WriteCommandData(0xe0, gamma_pos, sizeof(gamma_pos)) &&
            WriteCommandData(0xe1, gamma_neg, sizeof(gamma_neg)) &&
            WriteCommandData(0x21, NULL, 0) &&
            WriteCommandData(0x11, NULL, 0);
  if (!ok) return false;

  HAL_Delay(120);
  if (!WriteCommandData(0x29, NULL, 0)) return false;
  s_ready = true;
  return true;
}

bool ST7789_IsReady(void) { return s_ready; }

void ST7789_SetBacklight(bool enabled)
{
  HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN,
                    enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t width,
                     uint16_t height, uint16_t color)
{
  if (!s_ready || width == 0U || height == 0U || x >= ST7789_WIDTH ||
      y >= ST7789_HEIGHT) return;
  if ((uint32_t)x + width > ST7789_WIDTH) width = (uint16_t)(ST7789_WIDTH - x);
  if ((uint32_t)y + height > ST7789_HEIGHT) height = (uint16_t)(ST7789_HEIGHT - y);
  if (!SetWindow(x, y, width, height)) return;

  uint8_t pixels[LCD_PIXEL_CHUNK * 2U];
  for (uint16_t i = 0; i < LCD_PIXEL_CHUNK; ++i) {
    pixels[2U * i] = (uint8_t)(color >> 8);
    pixels[2U * i + 1U] = (uint8_t)color;
  }

  uint32_t remaining = (uint32_t)width * height;
  Select();
  HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET);
  while (remaining != 0U) {
    uint16_t count = remaining > LCD_PIXEL_CHUNK ? LCD_PIXEL_CHUNK : (uint16_t)remaining;
    if (!Transmit(pixels, (uint16_t)(count * 2U))) break;
    remaining -= count;
  }
  Deselect();
}

void ST7789_FillScreen(uint16_t color)
{
  ST7789_FillRect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, color);
}

void ST7789_DrawText(uint16_t x, uint16_t y, const char *text,
                     uint8_t scale, uint16_t foreground, uint16_t background)
{
  if (!s_ready || text == NULL || scale == 0U) return;
  if (scale > 4U) scale = 4U;
  uint16_t cursor = x;
  while (*text != '\0' && cursor < ST7789_WIDTH) {
    const uint8_t *glyph = FindGlyph(*text++);
    uint16_t char_width = (uint16_t)(6U * scale);
    uint16_t char_height = (uint16_t)(7U * scale);
    if ((uint32_t)cursor + char_width > ST7789_WIDTH ||
        (uint32_t)y + char_height > ST7789_HEIGHT) break;

    uint8_t pixels[6U * 4U * 7U * 4U * 2U];
    uint16_t index = 0;
    for (uint16_t py = 0; py < char_height; ++py) {
      uint8_t row = (uint8_t)(py / scale);
      for (uint16_t px = 0; px < char_width; ++px) {
        uint8_t column = (uint8_t)(px / scale);
        uint8_t bits = column < 5U ? glyph[column] : 0U;
        uint16_t color = (bits & (1U << row)) ? foreground : background;
        pixels[index++] = (uint8_t)(color >> 8);
        pixels[index++] = (uint8_t)color;
      }
    }

    if (!SetWindow(cursor, y, char_width, char_height)) return;
    Select();
    HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET);
    bool ok = Transmit(pixels, index);
    Deselect();
    if (!ok) return;
    cursor = (uint16_t)(cursor + char_width);
  }
}
