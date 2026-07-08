#ifndef __RGB_LED_H__
#define __RGB_LED_H__

#include "main.h"
#include <stdint.h>

/* Color struct: 0-255 per channel */
typedef struct {
  uint8_t r, g, b;
} RGB_Color;

/* Predefined alarm colors */
extern const RGB_Color RGB_BLACK;    /* off */
extern const RGB_Color RGB_RED;      /* alert */
extern const RGB_Color RGB_GREEN;    /* normal */
extern const RGB_Color RGB_BLUE;     /* armed / info */
extern const RGB_Color RGB_YELLOW;   /* warning */
extern const RGB_Color RGB_CYAN;     /* detecting */
extern const RGB_Color RGB_WHITE;    /* full */

void RGB_Init(void);
void RGB_Set(uint8_t r, uint8_t g, uint8_t b);
void RGB_SetColor(RGB_Color c);
void RGB_Tick(void);   /* call from TIM6 ISR at ~10kHz */

#endif
