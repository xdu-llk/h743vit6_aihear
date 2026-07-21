#include "dht11.h"
#include "main.h"

#define DHT11_PORT GPIOD
#define DHT11_PIN  GPIO_PIN_0

static uint32_t cycles_per_us;
static const char *last_error = "not-read";
static int last_error_bit = -1;

static void delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * cycles_per_us;
  while ((uint32_t)(DWT->CYCCNT - start) < ticks) {}
}

static void pin_output(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = DHT11_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DHT11_PORT, &gpio);
}

static void pin_input(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = DHT11_PIN;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(DHT11_PORT, &gpio);
}

static bool wait_while(GPIO_PinState level, uint32_t timeout_us,
                       uint32_t *duration_us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t timeout_ticks = timeout_us * cycles_per_us;

  while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == level) {
    if ((uint32_t)(DWT->CYCCNT - start) >= timeout_ticks) return false;
  }

  if (duration_us) {
    *duration_us = (uint32_t)(DWT->CYCCNT - start) / cycles_per_us;
  }
  return true;
}

void DHT11_Init(void)
{
  __HAL_RCC_GPIOD_CLK_ENABLE();
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  cycles_per_us = SystemCoreClock / 1000000U;
  if (cycles_per_us == 0) cycles_per_us = 1;

  pin_input();
}

const char *DHT11_GetLastError(void)
{
  return last_error;
}

int DHT11_GetLastErrorBit(void)
{
  return last_error_bit;
}

bool DHT11_Read(float *temperature_c, float *humidity_pct)
{
  if (!temperature_c || !humidity_pct) {
    last_error = "invalid-argument";
    last_error_bit = -1;
    return false;
  }

  uint8_t data[5] = {0};
  bool ok = false;
  last_error = "none";
  last_error_bit = -1;

  /* Use DWT so the start pulse does not depend on the RTOS/HAL tick. */
  pin_output();
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
  delay_us(20000);

  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
  delay_us(30);
  pin_input();

  /* Sensor response: 80 us low, 80 us high. */
  if (!wait_while(GPIO_PIN_SET, 120, NULL)) {
    last_error = "response-start-timeout";
    goto done;
  }
  if (!wait_while(GPIO_PIN_RESET, 120, NULL)) {
    last_error = "response-low-timeout";
    goto done;
  }
  if (!wait_while(GPIO_PIN_SET, 120, NULL)) {
    last_error = "response-high-timeout";
    goto done;
  }

  for (int bit = 0; bit < 40; bit++) {
    uint32_t high_us = 0;
    if (!wait_while(GPIO_PIN_RESET, 100, NULL)) {
      last_error = "bit-low-timeout";
      last_error_bit = bit;
      goto done;
    }
    if (!wait_while(GPIO_PIN_SET, 120, &high_us)) {
      last_error = "bit-high-timeout";
      last_error_bit = bit;
      goto done;
    }

    data[bit / 8] <<= 1;
    if (high_us > 45) data[bit / 8] |= 1;
  }

  if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
    last_error = "checksum";
    goto done;
  }

  *humidity_pct = (float)data[0] + (float)data[1] * 0.1f;
  *temperature_c = (float)(data[2] & 0x7f) + (float)data[3] * 0.1f;
  if (data[2] & 0x80) *temperature_c = -*temperature_c;
  ok = true;

done:
  if (!primask) __enable_irq();
  return ok;
}
