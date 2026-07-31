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
static bool s_snapshot_visible;
static float s_temp_c;
static float s_humidity_pct;
static float s_particle_index;
static float s_gas_index;
static uint8_t s_env_valid;
static uint8_t s_particle_ready, s_particle_alarm, s_particle_fault;
static uint8_t s_gas_ready, s_gas_alarm, s_gas_fault;

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
  ST7789_DrawText(5, 184, "TEMP", 2, COLOR_MUTED, COLOR_BG);
  ST7789_DrawText(64, 184, "HUMI", 2, COLOR_MUTED, COLOR_BG);
  ST7789_DrawText(138, 184, "PM", 2, COLOR_MUTED, COLOR_BG);
  ST7789_DrawText(190, 184, "GAS", 2, COLOR_MUTED, COLOR_BG);
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

static void DrawEnvironmentValues(void)
{
  s_env_pulse ^= 1U;
  ST7789_FillRect(222, 13, 7, 7,
                  s_env_valid ? (s_env_pulse ? COLOR_GREEN : COLOR_BLUE) : COLOR_MUTED);
  char temp[16];
  char humidity[16];
  if (s_env_valid) {
    snprintf(temp, sizeof(temp), "%.0fC", (double)s_temp_c);
    snprintf(humidity, sizeof(humidity), "%.0f%%", (double)s_humidity_pct);
  } else {
    snprintf(temp, sizeof(temp), "--C");
    snprintf(humidity, sizeof(humidity), "--%%");
  }
  ST7789_FillRect(4, 205, 56, 26, COLOR_BG);
  ST7789_FillRect(62, 205, 64, 26, COLOR_BG);
  ST7789_DrawText(8, 210, temp, 2,
                  s_env_valid ? COLOR_TEXT : COLOR_MUTED, COLOR_BG);
  ST7789_DrawText(68, 210, humidity, 2,
                  s_env_valid ? COLOR_TEXT : COLOR_MUTED, COLOR_BG);
}

bool TftUi_Init(void)
{
  if (!ST7789_Init()) return false;
  s_ready = true;
  DrawStaticLayout();
  TftUi_SetStatus("BOOTING", NULL, -60.0f);
  TftUi_SetEnvironment(0.0f, 0.0f, 0);
  TftUi_SetParticleQuality(0.0f, 0, 0, 0);
  TftUi_SetGasQuality(0.0f, 0, 0, 0);
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
    if (!s_snapshot_visible) DrawStatus(s_status, s_detail);
  }

  if (!isfinite(dbfs)) return;
  int rounded_dbfs = (int)lroundf(dbfs);
  uint32_t now = HAL_GetTick();
  if (s_dbfs == -1000 ||
      ((now - s_last_level_tick) >= 250U && abs(rounded_dbfs - s_dbfs) >= 1)) {
    s_dbfs = rounded_dbfs;
    s_last_level_tick = now;
    if (!s_snapshot_visible) DrawLevel(rounded_dbfs);
  }
}

void TftUi_SetEnvironment(float temp_c, float humidity_pct, uint8_t valid)
{
  if (!s_ready) return;
  s_temp_c = temp_c;
  s_humidity_pct = humidity_pct;
  s_env_valid = valid;
  if (!s_snapshot_visible) DrawEnvironmentValues();
}

static void DrawAirValue(uint16_t x, float index, uint8_t ready,
                         uint8_t alarm, uint8_t fault,
                         const char *pending_text)
{
  if (!s_ready) return;
  char value[5];
  uint16_t color = COLOR_MUTED;
  if (fault) {
    snprintf(value, sizeof(value), "ERR");
  } else if (!ready) {
    snprintf(value, sizeof(value), "%s", pending_text);
    color = COLOR_AMBER;
  } else {
    if (index > 999.0f) snprintf(value, sizeof(value), "999+");
    else snprintf(value, sizeof(value), "%.0f", (double)index);
    color = alarm ? COLOR_RED : COLOR_TEXT;
  }
  ST7789_FillRect(x, 205, 54, 26, COLOR_BG);
  ST7789_DrawText((uint16_t)(x + 4U), 210, value, 2, color, COLOR_BG);
}

void TftUi_SetParticleQuality(float index, uint8_t ready, uint8_t alarm,
                              uint8_t fault)
{
  s_particle_index = index;
  s_particle_ready = ready;
  s_particle_alarm = alarm;
  s_particle_fault = fault;
  if (!s_snapshot_visible)
    DrawAirValue(128, index, ready, alarm, fault, "CAL");
}

void TftUi_SetGasQuality(float index, uint8_t ready, uint8_t alarm,
                         uint8_t fault)
{
  s_gas_index = index;
  s_gas_ready = ready;
  s_gas_alarm = alarm;
  s_gas_fault = fault;
  if (!s_snapshot_visible)
    DrawAirValue(184, index, ready, alarm, fault, "WRM");
}

bool TftUi_ShowSnapshot(const uint8_t *rgb565, uint32_t length)
{
  if (!s_ready) return false;
  if (!ST7789_DrawRgb565(0, 0, ST7789_WIDTH, ST7789_HEIGHT,
                         rgb565, length)) return false;
  s_snapshot_visible = true;
  return true;
}

void TftUi_RestoreDashboard(void)
{
  if (!s_ready || !s_snapshot_visible) return;
  s_snapshot_visible = false;
  DrawStaticLayout();
  DrawStatus(s_status[0] ? s_status : "ARMED", s_detail);
  DrawLevel(s_dbfs == -1000 ? -60 : s_dbfs);
  DrawEnvironmentValues();
  DrawAirValue(128, s_particle_index, s_particle_ready,
               s_particle_alarm, s_particle_fault, "CAL");
  DrawAirValue(184, s_gas_index, s_gas_ready,
               s_gas_alarm, s_gas_fault, "WRM");
}
