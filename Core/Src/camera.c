#include "camera.h"
#include "main.h"
#include "stm32h7xx_hal_dcmi.h"
#include <string.h>

#define OV2640_ADDR_WRITE 0x60U
#define OV2640_BANK       0xffU
#define OV2640_BANK_DSP   0x00U
#define OV2640_BANK_SENSOR 0x01U

#define CAM_SCL_PORT GPIOB
#define CAM_SCL_PIN  GPIO_PIN_8
#define CAM_SDA_PORT GPIOB
#define CAM_SDA_PIN  GPIO_PIN_9
#define CAM_RESET_PORT GPIOC
#define CAM_RESET_PIN  GPIO_PIN_4
#define CAM_FLASH_PORT GPIOA
#define CAM_FLASH_PIN  GPIO_PIN_5
#define CAM_PWDN_PORT GPIOD
#define CAM_PWDN_PIN  GPIO_PIN_14

#define RGB_BYTES (CAMERA_RGB_WIDTH * CAMERA_RGB_HEIGHT * 2U)
#define SENSOR_WIDTH  320U
#define SENSOR_HEIGHT 240U

DCMI_HandleTypeDef hdcmi;
DMA_HandleTypeDef hdma_dcmi;

__attribute__((section(".camera_rgb"), aligned(32)))
static uint8_t rgb_buffer[RGB_BYTES];

__attribute__((section(".camera_jpeg"), aligned(32)))
static uint8_t jpeg_buffer[CAMERA_JPEG_MAX_BYTES];

static volatile bool frame_done;
static volatile bool frame_error;
static volatile uint32_t frame_bytes;
static bool camera_available;
static uint16_t sensor_id;

typedef struct {
  uint8_t reg;
  uint8_t value;
} CameraReg;

/* Manufacturer H743/OV2640 SVGA configuration, derived from its board example. */
static const CameraReg svga_config[] = {
  {0xff,0x01},{0x12,0x80},{0xff,0x00},{0x2c,0xff},{0x2e,0xdf},{0xff,0x01},
  {0x3c,0x32},{0x2c,0x0c},{0x33,0x78},{0x3a,0x33},{0x3b,0xfb},{0x3e,0x00},{0x43,0x11},{0x16,0x10},
  {0x39,0x92},{0x35,0xda},{0x22,0x1a},{0x37,0xc3},{0x36,0x1a},{0x4c,0x00},{0x5b,0x00},{0x42,0x03},
  {0x4a,0x81},{0x21,0x99},{0x5c,0x00},{0x63,0x00},{0x7c,0x05},{0x6c,0x00},{0x6d,0x80},{0x6e,0x00},
  {0x70,0x02},{0x71,0x94},{0x73,0xc1},{0x20,0x80},{0x28,0x30},{0x37,0xc0},{0x3d,0x38},{0x6d,0x00},
  {0x23,0x00},{0x06,0x88},{0x07,0xc0},{0x0d,0x87},{0x0e,0x41},{0x11,0x00},{0x09,0x02},{0x04,0x28},
  {0x12,0x40},{0x14,0x48},{0x15,0x00},{0x32,0x09},{0x03,0x8a},{0x46,0x00},{0x24,0x40},{0x25,0x38},
  {0x26,0x82},{0x34,0xc0},{0x61,0x70},{0x62,0x80},{0x17,0x11},{0x18,0x43},{0x19,0x01},{0x1a,0x97},
  {0x13,0xe5},{0x0c,0x3a},{0x4f,0xbb},{0x50,0x9c},{0x5a,0x23},{0xff,0x00},
  {0xe5,0x7f},{0x41,0x24},{0x76,0xff},{0x33,0xa0},{0x42,0x20},{0x43,0x18},{0x4c,0x00},{0xd7,0x03},
  {0xd9,0x10},{0x88,0x3f},{0xc8,0x08},{0xc9,0x80},{0x7c,0x00},{0x7d,0x00},{0x7c,0x03},{0x7d,0x48},
  {0x7d,0x48},{0x7c,0x08},{0x7d,0x20},{0x7d,0x10},{0x7d,0x0e},{0x90,0x00},{0x91,0x0e},{0x91,0x1a},
  {0x91,0x31},{0x91,0x5a},{0x91,0x69},{0x91,0x75},{0x91,0x7e},{0x91,0x88},{0x91,0x8f},{0x91,0x96},
  {0x91,0xa3},{0x91,0xaf},{0x91,0xc4},{0x91,0xd7},{0x91,0xe8},{0x91,0x20},{0x92,0x00},{0x93,0x06},
  {0x93,0xe3},{0x93,0x05},{0x93,0x05},{0x93,0x00},{0x93,0x04},{0x93,0x00},{0x93,0x00},{0x93,0x00},
  {0x93,0x00},{0x93,0x00},{0x93,0x00},{0x93,0x00},{0x96,0x00},{0x97,0x08},{0x97,0x19},{0x97,0x02},
  {0x97,0x0c},{0x97,0x24},{0x97,0x30},{0x97,0x28},{0x97,0x26},{0x97,0x02},{0x97,0x98},{0x97,0x80},
  {0x97,0x00},{0x97,0x00},{0xa4,0x00},{0xa8,0x00},{0xc5,0x11},{0xc6,0x51},{0xbf,0x80},{0xc7,0x10},
  {0xb6,0x66},{0xb8,0xa5},{0xb7,0x64},{0xb9,0x7c},{0xb3,0xaf},{0xb4,0x97},{0xb5,0xff},{0xb0,0xc5},
  {0xb1,0x94},{0xb2,0x0f},{0xc4,0x5c},{0x7f,0x00},{0xf9,0xc0},{0xe0,0x14},{0x87,0xd0},{0xc3,0xed},
  {0xc2,0x0e},{0x86,0x3d},{0xda,0x09},{0xc0,0x64},{0xc1,0x4b},{0x8c,0x00},{0x50,0x00},{0x51,0xc8},
  {0x52,0x96},{0x53,0x00},{0x54,0x00},{0x55,0x00},{0x57,0x00},{0xd3,0x04},{0xe5,0x1f},{0xe1,0x67},
  {0xdd,0x7f},{0xe0,0x00},{0x05,0x00}
};

static const CameraReg rgb565_config[] = {
  {0xff,0x00},{0xda,0x09},{0xd7,0x03},{0xe0,0x00},{0x05,0x00}
};

static const CameraReg jpeg_config[] = {
  {0xff,0x00},{0xda,0x10},{0xe1,0x77},{0xd7,0x03},{0xe0,0x00},{0x05,0x00}
};

static void sccb_delay(void)
{
  for (volatile uint32_t i = 0; i < 100U; ++i) __NOP();
}

static void sccb_start(void)
{
  HAL_GPIO_WritePin(CAM_SDA_PORT, CAM_SDA_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_SET);
  sccb_delay();
  HAL_GPIO_WritePin(CAM_SDA_PORT, CAM_SDA_PIN, GPIO_PIN_RESET);
  sccb_delay();
  HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_RESET);
}

static void sccb_stop(void)
{
  HAL_GPIO_WritePin(CAM_SDA_PORT, CAM_SDA_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_SET);
  sccb_delay();
  HAL_GPIO_WritePin(CAM_SDA_PORT, CAM_SDA_PIN, GPIO_PIN_SET);
  sccb_delay();
}

static bool sccb_write_byte(uint8_t value)
{
  for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1U) {
    HAL_GPIO_WritePin(CAM_SDA_PORT, CAM_SDA_PIN,
                      (value & mask) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    sccb_delay();
    HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_SET);
    sccb_delay();
    HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_RESET);
  }
  HAL_GPIO_WritePin(CAM_SDA_PORT, CAM_SDA_PIN, GPIO_PIN_SET);
  sccb_delay();
  HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_SET);
  sccb_delay();
  bool ack = HAL_GPIO_ReadPin(CAM_SDA_PORT, CAM_SDA_PIN) == GPIO_PIN_RESET;
  HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_RESET);
  return ack;
}

static uint8_t sccb_read_byte(void)
{
  uint8_t value = 0;
  HAL_GPIO_WritePin(CAM_SDA_PORT, CAM_SDA_PIN, GPIO_PIN_SET);
  for (uint8_t i = 0; i < 8U; ++i) {
    value <<= 1U;
    HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_SET);
    sccb_delay();
    if (HAL_GPIO_ReadPin(CAM_SDA_PORT, CAM_SDA_PIN) == GPIO_PIN_SET) value |= 1U;
    HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_RESET);
    sccb_delay();
  }
  /* NACK the final byte. */
  HAL_GPIO_WritePin(CAM_SDA_PORT, CAM_SDA_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_SET);
  sccb_delay();
  HAL_GPIO_WritePin(CAM_SCL_PORT, CAM_SCL_PIN, GPIO_PIN_RESET);
  return value;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
  bool ok;
  sccb_start();
  ok = sccb_write_byte(OV2640_ADDR_WRITE) && sccb_write_byte(reg) &&
       sccb_write_byte(value);
  sccb_stop();
  return ok;
}

static bool read_reg(uint8_t reg, uint8_t *value)
{
  sccb_start();
  bool ok = sccb_write_byte(OV2640_ADDR_WRITE) && sccb_write_byte(reg);
  sccb_stop();
  if (!ok) return false;
  sccb_start();
  ok = sccb_write_byte(OV2640_ADDR_WRITE | 1U);
  if (ok) *value = sccb_read_byte();
  sccb_stop();
  return ok;
}

static bool write_table(const CameraReg *table, size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    if (!write_reg(table[i].reg, table[i].value)) return false;
    if (table[i].reg == 0x12U && table[i].value == 0x80U) HAL_Delay(10);
  }
  return true;
}

static bool set_frame_size(uint16_t width, uint16_t height)
{
  if ((width & 3U) != 0U || (height & 3U) != 0U) return false;
  return write_reg(OV2640_BANK, OV2640_BANK_DSP) &&
         write_reg(0x5a, (uint8_t)(width / 4U)) &&
         write_reg(0x5b, (uint8_t)(height / 4U)) &&
         write_reg(0x5c, (uint8_t)(((width / 4U) >> 8U) |
                                   (((height / 4U) >> 6U) & 0x04U))) &&
         write_reg(0xe0, 0x00);
}

static void init_control_and_sccb(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Pin = CAM_RESET_PIN;
  HAL_GPIO_Init(CAM_RESET_PORT, &gpio);
  gpio.Pin = CAM_FLASH_PIN;
  HAL_GPIO_Init(CAM_FLASH_PORT, &gpio);
  gpio.Pin = CAM_PWDN_PIN;
  HAL_GPIO_Init(CAM_PWDN_PORT, &gpio);
  HAL_GPIO_WritePin(CAM_FLASH_PORT, CAM_FLASH_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CAM_PWDN_PORT, CAM_PWDN_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(CAM_RESET_PORT, CAM_RESET_PIN, GPIO_PIN_RESET);

  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Pin = CAM_SCL_PIN | CAM_SDA_PIN;
  HAL_GPIO_Init(GPIOB, &gpio);
  HAL_GPIO_WritePin(GPIOB, CAM_SCL_PIN | CAM_SDA_PIN, GPIO_PIN_SET);
}

void HAL_DCMI_MspInit(DCMI_HandleTypeDef *handle)
{
  if (handle->Instance != DCMI) return;
  __HAL_RCC_DCMI_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {0};
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF13_DCMI;
  gpio.Pin = GPIO_PIN_4 | GPIO_PIN_6;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_7;
  HAL_GPIO_Init(GPIOB, &gpio);
  gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOC, &gpio);
  gpio.Pin = GPIO_PIN_3;
  HAL_GPIO_Init(GPIOD, &gpio);
  gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
  HAL_GPIO_Init(GPIOE, &gpio);

  hdma_dcmi.Instance = DMA2_Stream7;
  hdma_dcmi.Init.Request = DMA_REQUEST_DCMI;
  hdma_dcmi.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_dcmi.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_dcmi.Init.MemInc = DMA_MINC_ENABLE;
  hdma_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  hdma_dcmi.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  hdma_dcmi.Init.Mode = DMA_NORMAL;
  hdma_dcmi.Init.Priority = DMA_PRIORITY_LOW;
  hdma_dcmi.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
  hdma_dcmi.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  hdma_dcmi.Init.MemBurst = DMA_MBURST_SINGLE;
  hdma_dcmi.Init.PeriphBurst = DMA_PBURST_SINGLE;
  if (HAL_DMA_Init(&hdma_dcmi) != HAL_OK) return;
  __HAL_LINKDMA(handle, DMA_Handle, hdma_dcmi);

  HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);
  HAL_NVIC_SetPriority(DCMI_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(DCMI_IRQn);
}

static bool dcmi_init(void)
{
  hdcmi.Instance = DCMI;
  hdcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
  hdcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_RISING;
  hdcmi.Init.VSPolarity = DCMI_VSPOLARITY_LOW;
  hdcmi.Init.HSPolarity = DCMI_HSPOLARITY_LOW;
  hdcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
  hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
  hdcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
  hdcmi.Init.ByteSelectMode = DCMI_BSM_ALL;
  hdcmi.Init.ByteSelectStart = DCMI_OEBS_ODD;
  hdcmi.Init.LineSelectMode = DCMI_LSM_ALL;
  hdcmi.Init.LineSelectStart = DCMI_OELS_ODD;
  return HAL_DCMI_Init(&hdcmi) == HAL_OK && hdcmi.DMA_Handle != NULL;
}

static bool set_rgb_mode(void)
{
  CLEAR_BIT(hdcmi.Instance->CR, DCMI_CR_JPEG);
  if (!write_table(rgb565_config, sizeof(rgb565_config) / sizeof(rgb565_config[0])) ||
      !set_frame_size(SENSOR_WIDTH, SENSOR_HEIGHT)) return false;
  HAL_DCMI_DisableCrop(&hdcmi);
  uint32_t x_offset = SENSOR_WIDTH - CAMERA_RGB_WIDTH;
  if (HAL_DCMI_ConfigCrop(&hdcmi, x_offset, 0U,
                          CAMERA_RGB_WIDTH * 2U - 1U,
                          CAMERA_RGB_HEIGHT - 1U) != HAL_OK) return false;
  return HAL_DCMI_EnableCrop(&hdcmi) == HAL_OK;
}

static bool set_jpeg_mode(void)
{
  HAL_DCMI_DisableCrop(&hdcmi);
  SET_BIT(hdcmi.Instance->CR, DCMI_CR_JPEG);
  return write_table(jpeg_config, sizeof(jpeg_config) / sizeof(jpeg_config[0])) &&
         set_frame_size(SENSOR_WIDTH, SENSOR_HEIGHT);
}

static bool capture_dma(uint8_t *buffer, uint32_t capacity,
                        uint32_t timeout_ms, bool variable_length,
                        CameraServiceFn service)
{
  frame_done = false;
  frame_error = false;
  frame_bytes = 0;
  memset(buffer, 0, capacity);
  SCB_CleanInvalidateDCache_by_Addr((uint32_t *)buffer, (int32_t)capacity);
  if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_SNAPSHOT,
                         (uint32_t)buffer, capacity / 4U) != HAL_OK) return false;
  if (variable_length) __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_FRAME);

  uint32_t started = HAL_GetTick();
  while (!frame_done && !frame_error && (HAL_GetTick() - started) < timeout_ms) {
    IWDG1->KR = 0xAAAA;
    if (service) service();
    HAL_Delay(1);
  }
  if (!frame_done || frame_error) {
    HAL_DCMI_Stop(&hdcmi);
    return false;
  }
  HAL_DCMI_Stop(&hdcmi);
  if (!variable_length) frame_bytes = capacity;
  SCB_InvalidateDCache_by_Addr((uint32_t *)buffer, (int32_t)capacity);
  return true;
}

bool Camera_Init(void)
{
  init_control_and_sccb();
  HAL_Delay(5);
  HAL_GPIO_WritePin(CAM_PWDN_PORT, CAM_PWDN_PIN, GPIO_PIN_RESET);
  HAL_Delay(5);
  HAL_GPIO_WritePin(CAM_RESET_PORT, CAM_RESET_PIN, GPIO_PIN_SET);
  HAL_Delay(10);

  uint8_t id_high = 0, id_low = 0;
  bool ok = write_reg(OV2640_BANK, OV2640_BANK_SENSOR) &&
            read_reg(0x0a, &id_high) && read_reg(0x0b, &id_low);
  sensor_id = (uint16_t)(((uint16_t)id_high << 8U) | id_low);
  if (!ok || (sensor_id != 0x2640U && sensor_id != 0x2642U)) {
    HAL_GPIO_WritePin(CAM_PWDN_PORT, CAM_PWDN_PIN, GPIO_PIN_SET);
    camera_available = false;
    return false;
  }

  if (!write_reg(OV2640_BANK, OV2640_BANK_SENSOR) || !write_reg(0x12, 0x80)) {
    camera_available = false;
    return false;
  }
  HAL_Delay(10);
  if (!write_table(svga_config, sizeof(svga_config) / sizeof(svga_config[0])) ||
      !dcmi_init() || !set_rgb_mode()) {
    camera_available = false;
    return false;
  }
  camera_available = true;
  return true;
}

bool Camera_IsAvailable(void) { return camera_available; }
uint16_t Camera_GetSensorId(void) { return sensor_id; }

CameraCaptureResult Camera_CaptureEvent(uint32_t frame_timeout_ms,
                                        CameraServiceFn service)
{
  if (!camera_available) return CAMERA_CAPTURE_UNAVAILABLE;
  HAL_GPIO_WritePin(CAM_FLASH_PORT, CAM_FLASH_PIN, GPIO_PIN_SET);
  HAL_Delay(120);

  if (!set_rgb_mode()) goto io_error;
  if (!capture_dma(rgb_buffer, sizeof(rgb_buffer), frame_timeout_ms, false,
                   service)) {
    HAL_GPIO_WritePin(CAM_FLASH_PORT, CAM_FLASH_PIN, GPIO_PIN_RESET);
    return CAMERA_CAPTURE_RGB_TIMEOUT;
  }
  if (!set_jpeg_mode()) goto io_error;
  HAL_Delay(40);
  if (!capture_dma(jpeg_buffer, sizeof(jpeg_buffer), frame_timeout_ms, true,
                   service)) {
    HAL_GPIO_WritePin(CAM_FLASH_PORT, CAM_FLASH_PIN, GPIO_PIN_RESET);
    return CAMERA_CAPTURE_JPEG_TIMEOUT;
  }
  HAL_GPIO_WritePin(CAM_FLASH_PORT, CAM_FLASH_PIN, GPIO_PIN_RESET);

  size_t start = 0;
  while (start + 1U < frame_bytes &&
         !(jpeg_buffer[start] == 0xffU && jpeg_buffer[start + 1U] == 0xd8U)) start++;
  size_t end = frame_bytes;
  while (end > start + 1U &&
         !(jpeg_buffer[end - 2U] == 0xffU && jpeg_buffer[end - 1U] == 0xd9U)) end--;
  if (start + 1U >= frame_bytes || end <= start + 1U) {
    frame_bytes = 0;
    return CAMERA_CAPTURE_JPEG_INVALID;
  }
  if (start != 0U) memmove(jpeg_buffer, jpeg_buffer + start, end - start);
  frame_bytes = (uint32_t)(end - start);
  return CAMERA_CAPTURE_OK;

io_error:
  HAL_GPIO_WritePin(CAM_FLASH_PORT, CAM_FLASH_PIN, GPIO_PIN_RESET);
  return CAMERA_CAPTURE_IO_ERROR;
}

const uint8_t *Camera_GetRgb565(size_t *length)
{
  if (length) *length = sizeof(rgb_buffer);
  return rgb_buffer;
}

const uint8_t *Camera_GetJpeg(size_t *length)
{
  if (length) *length = frame_bytes;
  return jpeg_buffer;
}

const char *Camera_ResultName(CameraCaptureResult result)
{
  static const char *const names[] = {
    "ok", "unavailable", "rgb_timeout", "jpeg_timeout", "jpeg_invalid", "io_error"
  };
  return (unsigned)result < (sizeof(names) / sizeof(names[0])) ? names[result] : "unknown";
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *handle)
{
  if (handle->Instance != DCMI) return;
  frame_bytes = (CAMERA_JPEG_MAX_BYTES / 4U -
                 __HAL_DMA_GET_COUNTER(&hdma_dcmi)) * 4U;
  frame_done = true;
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *handle)
{
  if (handle->Instance == DCMI) frame_error = true;
}
