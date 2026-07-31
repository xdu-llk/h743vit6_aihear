#include "audio.h"
#include "sai.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <math.h>

/* Task handle — set by freertos.c, used by ISR callbacks */
extern TaskHandle_t hAudioTask;

/* DMA、环形缓冲和推理快照统一由链接脚本放入 D2 SRAM。 */
__attribute__((section(".audio_d2"), aligned(32)))
static uint32_t dmabuf[AUDIO_BUF_SIZE * 2];
__attribute__((section(".audio_d2"), aligned(32)))
static int32_t ring[RING_BUF_SIZE];
__attribute__((section(".audio_d2"), aligned(32)))
static int32_t inference_snapshot[AUDIO_WINDOW_SAMPLES];
static volatile uint32_t wr = 0;
static volatile uint32_t sample_count = 0;

/* Debug: DMA ISR fire counter */
volatile uint32_t dma_isr_count = 0;

void Audio_Init(void)
{
  HAL_SAI_Receive_DMA(&hsai_BlockA3, (uint8_t *)dmabuf,
                      AUDIO_BUF_SIZE * 2);
}

bool Audio_Restart(void)
{
  (void)HAL_SAI_DMAStop(&hsai_BlockA3);
  taskENTER_CRITICAL();
  wr = 0;
  sample_count = 0;
  memset(ring, 0, sizeof(ring));
  taskEXIT_CRITICAL();
  return HAL_SAI_Receive_DMA(&hsai_BlockA3, (uint8_t *)dmabuf,
                             AUDIO_BUF_SIZE * 2) == HAL_OK;
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
  sample_count += (uint32_t)count;
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI3_Block_A) {
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
  if (hsai->Instance == SAI3_Block_A) {
    copy_to_ring(dmabuf + AUDIO_BUF_SIZE, AUDIO_BUF_SIZE);
    if (hAudioTask != NULL) {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      vTaskNotifyGiveFromISR(hAudioTask, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
}

float Audio_GetDBFS(void)
{
  uint32_t r = (wr - 256) & (RING_BUF_SIZE - 1);
  float sum_sq = 0.0f;
  for (int i = 0; i < 256; i++) {
    float v = (float)ring[r];
    sum_sq += v * v;
    r = (r + 1) & (RING_BUF_SIZE - 1);
  }
  float rms = sqrtf(sum_sq / 256.0f);
  /* 24-bit signed PCM full scale = 2^23 */
  if (rms < 1.0f) return -60.0f;  /* silence floor */
  return 20.0f * log10f(rms / 8388608.0f);
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

const int32_t *Audio_CaptureWindow(void)
{
  if (sample_count < AUDIO_WINDOW_SAMPLES) return NULL;

  /* The ISR writes the ring in 256-sample blocks. Masking interrupts for this
     short copy guarantees that all 96 FFT frames share one write position. */
  taskENTER_CRITICAL();
  uint32_t start = (wr - AUDIO_WINDOW_SAMPLES) & (RING_BUF_SIZE - 1);
  for (uint32_t i = 0; i < AUDIO_WINDOW_SAMPLES; i++) {
    inference_snapshot[i] = ring[(start + i) & (RING_BUF_SIZE - 1)];
  }
  taskEXIT_CRITICAL();
  return inference_snapshot;
}

uint32_t Audio_GetSampleCount(void)
{
  return sample_count;
}
