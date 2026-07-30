/* Includes ------------------------------------------------------------------*/
#include "sai.h"

SAI_HandleTypeDef hsai_BlockA3;
DMA_HandleTypeDef hdma_sai3_a;

/* SAI3 init function */
void MX_SAI3_Init(void)
{

hsai_BlockA3.Instance = SAI3_Block_A;
  hsai_BlockA3.Init.AudioMode = SAI_MODEMASTER_RX;
  hsai_BlockA3.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockA3.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockA3.Init.NoDivider = SAI_MCK_OVERSAMPLING_ENABLE;
  hsai_BlockA3.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai_BlockA3.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockA3.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_16K;
  hsai_BlockA3.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA3.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA3.Init.CompandingMode = SAI_NOCOMPANDING;
  if (HAL_SAI_InitProtocol(&hsai_BlockA3, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_24BIT, 2) != HAL_OK)
  {
    Error_Handler();
  }

}
static uint32_t SAI3_client =0;

void HAL_SAI_MspInit(SAI_HandleTypeDef* saiHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
/* SAI3 */
    if(saiHandle->Instance==SAI3_Block_A)
    {
    /* SAI3 clock enable */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SAI23;
    PeriphClkInitStruct.PLL2.PLL2M = 5;
    PeriphClkInitStruct.PLL2.PLL2N = 30;
    PeriphClkInitStruct.PLL2.PLL2P = 36;
    PeriphClkInitStruct.PLL2.PLL2Q = 2;
    PeriphClkInitStruct.PLL2.PLL2R = 2;
    PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_2;
    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOMEDIUM;
    PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
    PeriphClkInitStruct.Sai23ClockSelection = RCC_SAI23CLKSOURCE_PLL2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    if (SAI3_client == 0)
    {
       __HAL_RCC_SAI3_CLK_ENABLE();
    }
    SAI3_client ++;

    /**SAI3_A_Block_A GPIO Configuration
    PD0     ------> SAI3_SCK_A
    PD1     ------> SAI3_SD_A
    PD4     ------> SAI3_FS_A
    */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF6_SAI3;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* Peripheral DMA init*/

    hdma_sai3_a.Instance = DMA1_Stream0;
    hdma_sai3_a.Init.Request = DMA_REQUEST_SAI3_A;
    hdma_sai3_a.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_sai3_a.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sai3_a.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sai3_a.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sai3_a.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_sai3_a.Init.Mode = DMA_CIRCULAR;
    hdma_sai3_a.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_sai3_a.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_sai3_a) != HAL_OK)
    {
      Error_Handler();
    }

    /* Several peripheral DMA handle pointers point to the same DMA handle.
     Be aware that there is only one channel to perform all the requested DMAs. */
    __HAL_LINKDMA(saiHandle,hdmarx,hdma_sai3_a);
    __HAL_LINKDMA(saiHandle,hdmatx,hdma_sai3_a);
    }
}

void HAL_SAI_MspDeInit(SAI_HandleTypeDef* saiHandle)
{

/* SAI3 */
    if(saiHandle->Instance==SAI3_Block_A)
    {
    SAI3_client --;
    if (SAI3_client == 0)
      {
      /* Peripheral clock disable */
       __HAL_RCC_SAI3_CLK_DISABLE();
      }

    /**SAI3_A_Block_A GPIO Configuration
    PD0     ------> SAI3_SCK_A
    PD1     ------> SAI3_SD_A
    PD4     ------> SAI3_FS_A
    */
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4);

    HAL_DMA_DeInit(saiHandle->hdmarx);
    HAL_DMA_DeInit(saiHandle->hdmatx);
    }
}

