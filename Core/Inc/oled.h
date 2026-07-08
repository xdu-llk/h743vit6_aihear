/* oled.h — SSD1306 128x64 I2C via PB6/SCL PB7/SDA (bit-bang) */
#ifndef __OLED_H__
#define __OLED_H__
#include "main.h"
void OLED_Init(void);
void OLED_ShowStatus(const char *state, const char *cls, uint32_t peak);
#endif
