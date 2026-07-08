#include "audio.h"
#include "sai.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* Task handle — set by freertos.c, used by ISR callbacks */
extern TaskHandle_t hAudioTask;

/* DMA 双缓冲必须放 AXI SRAM (0x24000000)，DTCM DDR 不能访问 */
#define DMA_BASE 0x30020000UL  /* RAM_D2: avoid overlapping tensor_arena in AXI SRAM */
static uint32_t * const dmabuf = (uint32_t *)DMA_BASE;

/* 环形缓冲区 — D2 SRAM (DTCM 不够放 16K 样本) */
#define RING_BASE 0x30010000UL
static int32_t * const ring = (int32_t *)RING_BASE;
static volatile uint32_t wr = 0;

/* Debug: DMA ISR fire counter */
volatile uint32_t dma_isr_count = 0;

void Audio_Init(void)
{
  HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t *)dmabuf,
                      AUDIO_BUF_SIZE * 2);
}

/* 从 32bit I2S 槽提取 24bit 有符号值
 * STM32 SAI DMA data is 24-bit RIGHT-aligned in DR[23:0]
 * Need to shift left 8 to put sign bit at bit31, then arithmetic right shift */
static inline int32_t i2s_to_pcm(uint32_t raw)
{
  return ((int32_t)(raw << 8)) >> 8;
}

static void copy_to_ring(const uint32_t *src, int count)
{
  SCB_InvalidateDCache_by_Addr((uint32_t *)src, count * 4);
  for (int i = 0; i < count; i++) {
    ring[wr] = i2s_to_pcm(src[i]);
    wr = (wr + 1) & (RING_BUF_SIZE - 1);
  }
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A) {
    copy_to_ring(dmabuf, AUDIO_BUF_SIZE);
    /* Notify AudioTask from ISR */
    if (hAudioTask != NULL) {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      vTaskNotifyGiveFromISR(hAudioTask, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A) {
    copy_to_ring(dmabuf + AUDIO_BUF_SIZE, AUDIO_BUF_SIZE);
    if (hAudioTask != NULL) {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      vTaskNotifyGiveFromISR(hAudioTask, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
}

uint32_t Audio_GetPeak(void)
{
  uint32_t peak = 0;
  uint32_t r = (wr - 256) & (RING_BUF_SIZE - 1);
  for (int i = 0; i < 256; i++) {
    int32_t v = ring[r];
    if (v < 0) v = -v;
    if ((uint32_t)v > peak) peak = (uint32_t)v;
    r = (r + 1) & (RING_BUF_SIZE - 1);
  }
  return peak;
}

int Audio_ReadSamples(int32_t *dst, int offset, int count)
{
  uint32_t start = (wr - offset) & (RING_BUF_SIZE - 1);
  for (int i = 0; i < count; i++) {
    dst[i] = ring[(start + i) & (RING_BUF_SIZE - 1)];
  }
  return 0;
}
