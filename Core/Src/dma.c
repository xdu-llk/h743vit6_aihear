/* Includes ------------------------------------------------------------------*/
#include "dma.h"

/* Enable DMA controller clock */
void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  /* Priority 6 = FreeRTOS-safe for vTaskNotifyGiveFromISR (must be >= 5) */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);

}
