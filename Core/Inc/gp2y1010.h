#ifndef GP2Y1010_H
#define GP2Y1010_H

#include <stdbool.h>
#include <stdint.h>

bool Gp2y1010_Init(void);
bool Gp2y1010_ReadRaw(uint16_t *raw);

#endif
