/* oled.h — SSD1306 128x64 via hardware I2C2 (PB10=SCL, PB11=SDA) */
#ifndef __OLED_H__
#define __OLED_H__
#include "main.h"
void OLED_Init(void);
void OLED_SetEnvironment(float temp_c, float humi_pct, uint8_t valid);
void OLED_ShowStatus(const char *state, const char *cls, float dbfs);
#endif
