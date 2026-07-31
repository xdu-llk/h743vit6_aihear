#ifndef TFT_UI_H
#define TFT_UI_H

#include <stdbool.h>
#include <stdint.h>

bool TftUi_Init(void);
void TftUi_SetStatus(const char *status, const char *detail, float dbfs);
void TftUi_SetEnvironment(float temp_c, float humidity_pct, uint8_t valid);

#endif
