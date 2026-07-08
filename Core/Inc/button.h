/* button.h — simple GPIO button on PC4 (pull-up, press=GND) */
#ifndef __BUTTON_H__
#define __BUTTON_H__
#include "main.h"
void Button_Init(void);
void Button_Poll(void);            /* call frequently, edge-detect scan */
uint8_t Button_WasPressed(void);   /* edge-detect, clears after read */
uint32_t Button_HeldMs(void);      /* how long button has been held (ms) */
#endif
