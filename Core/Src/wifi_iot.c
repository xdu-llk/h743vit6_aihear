#include "wifi_iot.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/* Task handle — set by freertos.c (if using separate MQTT task) */
/* extern TaskHandle_t hMqttAlarmTask; — removed, single-task arch */

#define RX_RING_SIZE  512
#define LINE_BUF_SIZE 256

static volatile char  rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_wr = 0;
static volatile uint16_t rx_rd = 0;

static char     line_buf[LINE_BUF_SIZE];
static uint8_t  line_idx = 0;

static volatile uint8_t  wifi_state = 0;
static volatile uint8_t  mqtt_state = 0;
static volatile uint8_t  esp_ready  = 0;

/* Start single-byte UART4 RX interrupt */
static void uart_start_rx(void)
{
  static uint8_t rx_byte;
  HAL_UART_Receive_IT(&huart4, &rx_byte, 1);
}

/* Parse one complete line from ESP32 */
static void process_line(const char *line)
{
  if (strncmp(line, "+READY", 6) == 0) {
    if (!esp_ready) printf("[WiFiIoT] ESP32 ready\r\n");  /* only on first +READY */
    esp_ready = 1;
  }
  else if (strncmp(line, "+STATUS:", 8) == 0) {
    int w = 0, m = 0;
    sscanf(line, "+STATUS:%d:%d", &w, &m);
    wifi_state = (uint8_t)w;
    mqtt_state = (uint8_t)m;
    esp_ready = 1;  /* valid STATUS reply means ESP32 is alive */
  }
  else if (strncmp(line, "+ERR:", 5) == 0) {
    printf("[WiFiIoT] ESP32 error: %s\r\n", line + 5);
  }
  else if (strstr(line, "aihear_")) {
    static uint8_t printed = 0;
    if (!printed) { printf("[WiFiIoT] Device: %s\r\n", strstr(line, "aihear_")); printed = 1; }
  }
  else if (strstr(line, "+CONFIG:")) {}
  else {
    printf("[WiFiIoT] RX: '%s'\r\n", line);
  }
}

/* Override __weak HAL callback: every RX byte from UART4 goes into ring buffer */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4) {
    static uint8_t rx_byte;
    rx_ring[rx_wr] = rx_byte;
    rx_wr = (rx_wr + 1) & (RX_RING_SIZE - 1);
    /* UART4 data processed in AudioInferTask main loop via WifiIoT_Process() */
    HAL_UART_Receive_IT(&huart4, &rx_byte, 1);
  }
}

void WifiIoT_Init(void)
{
  /* UART4 NVIC already enabled by CubeMX MspInit */
  uart_start_rx();
  printf("[WiFiIoT] Init done, waiting for ESP32 +READY...\r\n");
}

void WifiIoT_Process(void)
{
  while (rx_rd != rx_wr) {
    char c = rx_ring[rx_rd];
    rx_rd = (rx_rd + 1) & (RX_RING_SIZE - 1);

    if (c == '\n') {
      if (line_idx > 0) {
        line_buf[line_idx] = '\0';
        process_line(line_buf);
        line_idx = 0;
      }
    } else if (c != '\r' && line_idx < LINE_BUF_SIZE - 1) {
      line_buf[line_idx++] = c;
    }
  }
}

void WifiIoT_Publish(const char *topic, const char *payload)
{
  if (wifi_state != WIFI_STATE_CONNECTED || mqtt_state != MQTT_STATE_CONNECTED) return;
  char cmd[256];
  int len = snprintf(cmd, sizeof(cmd), "+PUB:%s:%s\r\n", topic, payload);
  if (len > 0 && len < (int)sizeof(cmd)) {
    HAL_UART_Transmit(&huart4, (uint8_t *)cmd, len, 100);
  }
}

void WifiIoT_QueryStatus(void)
{
  /* Always send query — don't wait for esp_ready (chicken-and-egg) */
  const char *cmd = "+STATUS\r\n";
  HAL_UART_Transmit(&huart4, (uint8_t *)cmd, strlen(cmd), 100);
}

uint8_t WifiIoT_GetWifiState(void)  { return wifi_state; }
uint8_t WifiIoT_GetMqttState(void)  { return mqtt_state; }
uint8_t WifiIoT_IsReady(void)       { return wifi_state == WIFI_STATE_CONNECTED && mqtt_state == MQTT_STATE_CONNECTED; }
