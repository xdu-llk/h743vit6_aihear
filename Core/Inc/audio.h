#ifndef __AUDIO_H__
#define __AUDIO_H__

#include "main.h"

#define AUDIO_BUF_SIZE    256   // 半帧音频样本数
#define RING_BUF_SIZE     16384  // 环形缓冲区，必须是2的幂 (96帧=15712样本)

void     Audio_Init(void);
uint32_t Audio_GetPeak(void);
float    Audio_GetDBFS(void);
int      Audio_ReadSamples(int32_t *dst, int offset, int count);

#endif
