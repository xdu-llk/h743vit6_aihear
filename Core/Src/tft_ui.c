#include "tft_ui.h"
#include "main.h"
#include "st7789.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLOR_BG       ST7789_RGB565(10, 15, 22)
#define COLOR_PANEL    ST7789_RGB565(22, 31, 42)
#define COLOR_LINE     ST7789_RGB565(48, 63, 78)
#define COLOR_TEXT     ST7789_RGB565(235, 241, 245)
#define COLOR_MUTED    ST7789_RGB565(142, 157, 171)
#define COLOR_GREEN    ST7789_RGB565(51, 209, 122)
#define COLOR_AMBER    ST7789_RGB565(245, 180, 52)
#define COLOR_RED      ST7789_RGB565(244, 75, 82)
#define COLOR_BLUE     ST7789_RGB565(62, 151, 238)

static bool s_ready;
static char s_status[16];
static char s_detail[24];
static int s_dbfs = -1000;
static uint32_t s_last_level_tick;
static uint8_t s_env_pulse;

static uint16_t StatusColor(const char *status)
{
  if (strstr(status, "ALERT") != NULL) return COLOR_RED;
  if (strstr(status, "VOTING") != NULL || strstr(status, "DETECT") != NULL)
    return COLOR_AMBER;
  if (strstr(status, "DISARM") != NULL || strstr(status, "BOOT") != NULL)
    return COLOR_MUTED;
  return COLOR_GREEN;
}

static uint16_t CenterX(const char *text, uint8_t scale)
{
  size_t width = strlen(text) * 6U * scale;
  return width < ST7789_WIDTH ? (uint16_t)((ST7789_WIDTH - width) / 2U) : 0U;
}

static void DrawStaticLayout(void)
{
  ST7789_FillScreen(COLOR_BG);
  ST7789_FillRect(0, 0, 240, 34, COLOR_PANEL);
  ST7789_DrawText(60, 9, "AI HEAR", 2, COLOR_TEXT, COLOR_PANEL);
  ST7789_FillRect(0, 106, 240, 1, COLOR_LINE);
  ST7789_FillRect(0, 171, 240, 1, COLOR_LINE);
  ST7789_DrawText(14, 116, "SOUND", 2, COLOR_MUTED, COLOR_BG);
  ST7789_DrawText(14, 184, "TEMP", 2, COLOR_MUTED, COLOR_BG);
  ST7789_DrawText(132, 184, "HUMI", 2, COLOR_MUTED, COLOR_BG);
}

static void DrawStatus(const char *status, const char *detail)
{
  uint16_t color = StatusColor(status);
  uint8_t scale = strlen(status) > 9U ? 3U : 4U;
  ST7789_FillRect(0, 35, 240, 70, COLOR_BG);
  ST7789_DrawText(CenterX(status, scale), 48, status, scale, color, COLOR_BG);
  if (detail != NULL && detail[0] != '\0') {
    ST7789_DrawText(CenterX(detail, 1), 87, detail, 1, COLOR_MUTED, COLOR_BG);
  }
}

static void DrawLevel(int dbfs)
{
  char value[16];
  snprintf(value, sizeof(value), "%d DB", dbfs);
  ST7789_FillRect(132, 113, 98, 20, COLOR_BG);
  ST7789_DrawText(132, 116, value, 2, COLOR_TEXT, COLOR_BG);

  int clamped = dbfs;
  if (clamped < -60) clamped = -60;
  if (clamped > 0) clamped = 0;
  uint16_t width = (uint16_t)((clamped + 60) * 212 / 60);
  ST7789_FillRect(14, 145, 212, 12, COLOR_PANEL);
  if (width != 0U) ST7789_FillRect(14, 145, width, 12, COLOR_BLUE);
}

bool TftUi_Init(void)
{
  if (!ST7789_Init()) return false;
  s_ready = true;
  DrawStaticLayout();
  TftUi_SetStatus("BOOTING", NULL, -60.0f);
  TftUi_SetEnvironment(0.0f, 0.0f, 0);
  ST7789_SetBacklight(true);
  return true;
}

void TftUi_SetStatus(const char *status, const char *detail, float dbfs)
{
  if (!s_ready || status == NULL) return;
  const char *safe_detail = detail != NULL ? detail : "";
  if (strncmp(status, s_status, sizeof(s_status)) != 0 ||
      strncmp(safe_detail, s_detail, sizeof(s_detail)) != 0) {
    snprintf(s_status, sizeof(s_status), "%s", status);
    snprintf(s_detail, sizeof(s_detail), "%s", safe_detail);
    DrawStatus(s_status, s_detail);
  }

  if (!isfinite(dbfs)) return;
  int rounded_dbfs = (int)lroundf(dbfs);
  uint32_t now = HAL_GetTick();
  if (s_dbfs == -1000 ||
      ((now - s_last_level_tick) >= 250U && abs(rounded_dbfs - s_dbfs) >= 1)) {
    s_dbfs = rounded_dbfs;
    s_last_level_tick = now;
    DrawLevel(rounded_dbfs);
  }
}

void TftUi_SetEnvironment(float temp_c, float humidity_pct, uint8_t valid)
{
  if (!s_ready) return;
  s_env_pulse ^= 1U;
  ST7789_FillRect(222, 13, 7, 7,
                  valid ? (s_env_pulse ? COLOR_GREEN : COLOR_BLUE) : COLOR_MUTED);
  char temp[16];
  char humidity[16];
  if (valid) {
    snprintf(temp, sizeof(temp), "%.1f C", (double)temp_c);
    snprintf(humidity, sizeof(humidity), "%.1f %%", (double)humidity_pct);
  } else {
    snprintf(temp, sizeof(temp), "--.- C");
    snprintf(humidity, sizeof(humidity), "--.- %%");
  }
  ST7789_FillRect(12, 205, 108, 26, COLOR_BG);
  ST7789_FillRect(130, 205, 108, 26, COLOR_BG);
  ST7789_DrawText(14, 210, temp, 2, valid ? COLOR_TEXT : COLOR_MUTED, COLOR_BG);
  ST7789_DrawText(132, 210, humidity, 2, valid ? COLOR_TEXT : COLOR_MUTED, COLOR_BG);
}
