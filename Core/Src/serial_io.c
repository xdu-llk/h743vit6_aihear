#include "serial_io.h"
#include "usart.h"

void Serial_Init(void)
{
  /* USART1 已在 MX_USART1_UART_Init() 中初始化，
     这里仅做状态确认 */
  setvbuf(stdout, NULL, _IONBF, 0);  /* unbuffered —实时输出到串口 */
  printf("\r\n=== STM32H743 AI Hear ===\r\n");
  printf("SystemClock: 480MHz\r\n");
  printf("LED: PC13  Buzzer: PB0 (TIM3_CH3)\r\n\r\n");
}

/* printf 重定向 — Picolibc 回调 */
int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 10);
  return ch;
}
