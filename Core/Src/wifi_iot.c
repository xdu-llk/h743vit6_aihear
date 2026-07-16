#include "wifi_iot.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define RX_RING_SIZE  512
#define LINE_BUF_SIZE 256
#define STATUS_TIMEOUT_MS  15000   /* mark offline if no +STATUS for 15 s */
#define PENDING_QUEUE_SIZE 4       /* max stored alerts/status when link is down */

static volatile char  rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_wr = 0;
static volatile uint16_t rx_rd = 0;
static volatile uint16_t rx_overflow = 0;

static char     line_buf[LINE_BUF_SIZE];
static uint8_t  line_idx = 0;

static volatile uint8_t  wifi_state   = 0;
static volatile uint8_t  mqtt_state   = 0;
static volatile uint8_t  esp_ready    = 0;

/* ── file‑scope rx_byte — SHARED by uart_start_rx() and the ISR callback ── */
static volatile uint8_t rx_byte;

/* ── health trackers ── */
static volatile uint32_t last_puback_seq = 0;
static volatile uint32_t last_puback_ms  = 0;
static volatile uint32_t last_status_ms  = 0;

/* ── device identity ── */
static char device_id[32];  /* captured from ESP +DEVICEID:aihear_XXXXXX */

/* ── pending message queue (link-down buffer) ── */
typedef struct {
  char    topic[64];
  char    payload[128];
  uint8_t valid;   /* 0=empty, 1=pending */
} PendingMsg;

static PendingMsg pending_queue[PENDING_QUEUE_SIZE];
static uint8_t    pending_count  = 0;
static uint8_t    was_ready      = 0;   /* previous IsReady() for rising-edge detect */

/* Internal: raw publish via UART — no IsReady check, no queue. */
static void PublishRaw(const char *topic, const char *payload)
{
  char cmd[256];
  int len = snprintf(cmd, sizeof(cmd), "+PUB:%s:%s\r\n", topic, payload);
  if (len > 0 && len < (int)sizeof(cmd)) {
    HAL_UART_Transmit(&huart4, (uint8_t *)cmd, len, 100);
  }
}

/* Start single-byte UART4 RX interrupt */
static void uart_start_rx(void)
{
  HAL_UART_Receive_IT(&huart4, (uint8_t *)&rx_byte, 1);
}

/* Parse one complete line from ESP8266.
   Prefixes use strstr() instead of strncmp() to tolerate leading garbage
   (e.g. "++STATUS:2:2" from UART framing noise). */
static void process_line(const char *line)
{
  const char *p;

  if (strstr(line, "+READY")) {
    if (!esp_ready) printf("[WiFiIoT] ESP ready\r\n");
    esp_ready = 1;
  }
  else if ((p = strstr(line, "+STATUS:")) != NULL) {
    int w = 0, m = 0;
    sscanf(p, "+STATUS:%d:%d", &w, &m);
    wifi_state = (uint8_t)w;
    mqtt_state = (uint8_t)m;
    last_status_ms = HAL_GetTick();
    esp_ready = 1;
  }
  else if ((p = strstr(line, "+PUBACK:")) != NULL) {
    sscanf(p, "+PUBACK:%lu", (unsigned long *)&last_puback_seq);
    last_puback_ms = HAL_GetTick();
  }
  else if (strstr(line, "+ERR:")) {
    printf("[WiFiIoT] ESP error: %s\r\n", line);
  }
  else if ((p = strstr(line, "+DEVICEID:")) != NULL) {
    /* Capture device ID — used for status payload */
    const char *q = strstr(p, "aihear_");
    if (q) {
      strncpy(device_id, q, sizeof(device_id) - 1);
      device_id[sizeof(device_id) - 1] = '\0';
      /* strip trailing \r if present */
      size_t dl = strlen(device_id);
      if (dl > 0 && device_id[dl - 1] == '\r') device_id[dl - 1] = '\0';
    }
    static uint8_t printed_devid = 0;
    if (!printed_devid && device_id[0]) {
      printf("[WiFiIoT] Device: %s\r\n", device_id);
      printed_devid = 1;
    }
  }
  else if (strstr(line, "+CONFIG:")) {
    /* silently ignore config lines */
  }
  else {
    /* Unknown line — print for debugging */
    printf("[WiFiIoT] RX: '%.80s'\r\n", line);
  }
}

/* Override __weak HAL callback: every RX byte from UART4 goes into ring buffer */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4) {
    uint16_t next = (rx_wr + 1) & (RX_RING_SIZE - 1);
    if (next == rx_rd) {
      rx_overflow++;               /* ring full — drop oldest */
      rx_rd = (rx_rd + 1) & (RX_RING_SIZE - 1);
    }
    rx_ring[rx_wr] = rx_byte;
    rx_wr = next;
    HAL_UART_Receive_IT(&huart4, (uint8_t *)&rx_byte, 1);
  }
}

void WifiIoT_Init(void)
{
  uart_start_rx();
  printf("[WiFiIoT] Init done, waiting for ESP +READY...\r\n");
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
    /* silently drop if line too long — next '\n' resets */
  }
}

/* Publish if link is up, otherwise enqueue for later retransmission.
   Same topic overwrites the existing slot (dedup). */
void WifiIoT_Publish(const char *topic, const char *payload)
{
  if (WifiIoT_IsReady()) {
    PublishRaw(topic, payload);
    return;
  }

  /* ── Link down: enqueue ── */
  /* 1) overwrite slot with same topic */
  for (int i = 0; i < PENDING_QUEUE_SIZE; i++) {
    if (pending_queue[i].valid && strcmp(pending_queue[i].topic, topic) == 0) {
      strncpy(pending_queue[i].payload, payload, sizeof(pending_queue[i].payload) - 1);
      pending_queue[i].payload[sizeof(pending_queue[i].payload) - 1] = '\0';
      printf("[WiFiIoT] Q-update '%s'\r\n", topic);
      return;
    }
  }
  /* 2) find empty slot */
  for (int i = 0; i < PENDING_QUEUE_SIZE; i++) {
    if (!pending_queue[i].valid) {
      strncpy(pending_queue[i].topic, topic, sizeof(pending_queue[i].topic) - 1);
      strncpy(pending_queue[i].payload, payload, sizeof(pending_queue[i].payload) - 1);
      pending_queue[i].topic[sizeof(pending_queue[i].topic) - 1] = '\0';
      pending_queue[i].payload[sizeof(pending_queue[i].payload) - 1] = '\0';
      pending_queue[i].valid = 1;
      pending_count++;
      printf("[WiFiIoT] Q-store '%s' (%d/%d)\r\n", topic, pending_count, PENDING_QUEUE_SIZE);
      return;
    }
  }
  /* 3) queue full — drop oldest, reuse last slot */
  printf("[WiFiIoT] Q-full! drop oldest for '%s'\r\n", topic);
  memmove(&pending_queue[0], &pending_queue[1],
          sizeof(PendingMsg) * (PENDING_QUEUE_SIZE - 1));
  strncpy(pending_queue[PENDING_QUEUE_SIZE - 1].topic, topic,
          sizeof(pending_queue[PENDING_QUEUE_SIZE - 1].topic) - 1);
  strncpy(pending_queue[PENDING_QUEUE_SIZE - 1].payload, payload,
          sizeof(pending_queue[PENDING_QUEUE_SIZE - 1].payload) - 1);
  pending_queue[PENDING_QUEUE_SIZE - 1]
      .topic[sizeof(pending_queue[PENDING_QUEUE_SIZE - 1].topic) - 1] = '\0';
  pending_queue[PENDING_QUEUE_SIZE - 1]
      .payload[sizeof(pending_queue[PENDING_QUEUE_SIZE - 1].payload) - 1] = '\0';
  pending_queue[PENDING_QUEUE_SIZE - 1].valid = 1;
}

/* Call every main loop iteration.  Detects link recovery (rising edge) and
   flushes all pending messages. */
void WifiIoT_FlushPending(void)
{
  uint8_t ready = WifiIoT_IsReady();

  if (!ready && was_ready) {
    printf("[WiFiIoT] Link lost — queueing enabled\r\n");
  }

  if (ready && !was_ready && pending_count > 0) {
    printf("[WiFiIoT] Link recovered — flushing %d pending\r\n", pending_count);
    for (int i = 0; i < PENDING_QUEUE_SIZE; i++) {
      if (pending_queue[i].valid) {
        PublishRaw(pending_queue[i].topic, pending_queue[i].payload);
        pending_queue[i].valid = 0;
        pending_count--;
      }
    }
  }
  was_ready = ready;
}

void WifiIoT_QueryStatus(void)
{
  const char *cmd = "+STATUS\r\n";
  HAL_UART_Transmit(&huart4, (uint8_t *)cmd, strlen(cmd), 100);
}

/* ── public accessors ── */
uint8_t WifiIoT_GetWifiState(void)  { return wifi_state; }
uint8_t WifiIoT_GetMqttState(void)  { return mqtt_state; }

uint8_t WifiIoT_IsReady(void)
{
  if (wifi_state != WIFI_STATE_CONNECTED || mqtt_state != MQTT_STATE_CONNECTED)
    return 0;
  /* Timestamp check: if we've never received a STATUS reply, or the last one
     is older than STATUS_TIMEOUT_MS, treat the link as unhealthy. */
  uint32_t now = HAL_GetTick();
  if (last_status_ms == 0 || (now - last_status_ms) > STATUS_TIMEOUT_MS)
    return 0;
  return 1;
}

uint8_t WifiIoT_StatusHealthy(void)
{
  if (last_status_ms == 0) return 0;
  return (HAL_GetTick() - last_status_ms) < STATUS_TIMEOUT_MS;
}

uint32_t WifiIoT_GetLastPubackMs(void)  { return last_puback_ms; }
uint32_t WifiIoT_GetLastPubackSeq(void) { return last_puback_seq; }
uint16_t WifiIoT_GetRxOverflow(void)    { return rx_overflow; }
uint8_t  WifiIoT_GetPendingCount(void)  { return pending_count; }
const char* WifiIoT_GetDeviceId(void)   { return device_id[0] ? device_id : NULL; }
