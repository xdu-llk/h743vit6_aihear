#ifndef MQ135_H
#define MQ135_H

#include <stdbool.h>
#include <stdint.h>

bool Mq135_Init(void);
bool Mq135_ReadRaw(uint16_t *raw);

#endif
