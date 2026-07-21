#ifndef DHT11_H
#define DHT11_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void DHT11_Init(void);
bool DHT11_Read(float *temperature_c, float *humidity_pct);
const char *DHT11_GetLastError(void);
int DHT11_GetLastErrorBit(void);

#ifdef __cplusplus
}
#endif

#endif
