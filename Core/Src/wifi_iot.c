#include "wifi_iot.h"
#include "camera.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define UART_RX_DMA_SIZE 1024U
#define LINE_BUF_SIZE 256
#define STATUS_TIMEOUT_MS  15000   /* mark offline if no +STATUS for 15 s */
#define PENDING_QUEUE_SIZE 8       /* max stored alerts/status when link is down */
#define PENDING_PAYLOAD_SIZE 232
#define PUBLISH_CMD_SIZE 448
#define SNAPSHOT_CHUNK_BYTES 240U
#define SNAPSHOT_ACK_TIMEOUT_MS 1000U
#define SNAPSHOT_UPLOAD_TIMEOUT_MS 15000U
#define ESP_RESET_DEADLINE_MS 20000 /* hard-reset ESP after 20 s of no STATUS */
#define ESP_MAX_RESETS  3          /* max hard resets per power cycle */
#define ESP_RST_PORT    GPIOA
#define ESP_RST_PIN     GPIO_PIN_2

__attribute__((section(".audio_d2"), aligned(32)))
static uint8_t uart_rx_dma[UART_RX_DMA_SIZE];
static volatile uint32_t rx_dma_wraps = 0;
static uint64_t rx_dma_consumed = 0;
static volatile uint8_t rx_dma_restart_requested = 0;
static volatile uint16_t rx_overflow = 0;

static char     line_buf[LINE_BUF_SIZE];
static uint8_t  line_idx = 0;

static volatile uint8_t  wifi_state   = 0;
static volatile uint8_t  mqtt_state   = 0;
static volatile uint8_t  esp_ready    = 0;

/* ── health trackers ── */
static volatile uint32_t last_puback_seq = 0;
static volatile uint32_t last_puback_ms  = 0;
static volatile uint32_t last_status_ms  = 0;

/* ── device identity ── */
static char device_id[32];  /* captured from ESP +DEVICEID:aihear_XXXXXX */

/* ── ESP hard-reset tracking ── */
static uint32_t esp_down_since_ms = 0;  /* millis() when link first went down */
static uint8_t  esp_reset_count   = 0;  /* resets this power cycle */

/* ── remote command queue (App → MQTT → ESP → UART) ── */
#define CMD_QUEUE_SIZE  4
static char cmd_queue[CMD_QUEUE_SIZE][64];
static volatile uint8_t cmd_wr = 0;
static volatile uint8_t cmd_rd = 0;
typedef struct {
  char    topic[64];
  char    payload[PENDING_PAYLOAD_SIZE];
  uint8_t valid;   /* 0=empty, 1=pending */
} PendingMsg;

static PendingMsg pending_queue[PENDING_QUEUE_SIZE];
static uint8_t    pending_count  = 0;
static uint8_t    was_ready      = 0;   /* previous IsReady() for rising-edge detect */

/* Internal: raw publish via UART — no IsReady check, no queue. */
static void PublishRaw(const char *topic, const char *payload)
{
  char cmd[PUBLISH_CMD_SIZE];
  int len = snprintf(cmd, sizeof(cmd), "+PUB:%s:%s\r\n", topic, payload);
  if (len > 0 && len < (int)sizeof(cmd)) {
    HAL_UART_Transmit(&huart4, (uint8_t *)cmd, len, 100);
  }
}

static bool PublishRawAndWait(const char *topic, const char *payload,
                              WifiIoTServiceFn service)
{
  WifiIoT_Process();
  uint32_t ack_before = last_puback_seq;
  PublishRaw(topic, payload);
  uint32_t started = HAL_GetTick();
  while ((HAL_GetTick() - started) < SNAPSHOT_ACK_TIMEOUT_MS) {
    WifiIoT_Process();
    if (last_puback_seq != ack_before) return true;
    if (!WifiIoT_IsReady()) return false;
    if (service) service();
    IWDG1->KR = 0xAAAA;
    HAL_Delay(1);
  }
  return false;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t length)
{
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8U; ++bit)
      crc = (crc >> 1U) ^ (0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
  }
  return ~crc;
}

static size_t base64_encode(const uint8_t *input, size_t length, char *output)
{
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t in = 0, out = 0;
  while (in < length) {
    uint32_t value = (uint32_t)input[in++] << 16U;
    bool have_second = in < length;
    if (have_second) value |= (uint32_t)input[in++] << 8U;
    bool have_third = in < length;
    if (have_third) value |= input[in++];
    output[out++] = alphabet[(value >> 18U) & 0x3fU];
    output[out++] = alphabet[(value >> 12U) & 0x3fU];
    output[out++] = have_second ? alphabet[(value >> 6U) & 0x3fU] : '=';
    output[out++] = have_third ? alphabet[value & 0x3fU] : '=';
  }
  output[out] = '\0';
  return out;
}

bool WifiIoT_PublishSnapshot(const uint8_t *jpeg, size_t length,
                             const char *capture_id,
                             WifiIoTServiceFn service)
{
  if (!jpeg || !capture_id || !capture_id[0] || length < 4U ||
      length > CAMERA_JPEG_MAX_BYTES || !WifiIoT_IsReady()) return false;
  uint32_t total = (uint32_t)((length + SNAPSHOT_CHUNK_BYTES - 1U) /
                              SNAPSHOT_CHUNK_BYTES);
  uint32_t crc = crc32_bytes(jpeg, length);
  uint32_t upload_started = HAL_GetTick();
  char encoded[SNAPSHOT_CHUNK_BYTES * 4U / 3U + 1U];
  char payload[384];

  for (uint32_t seq = 0; seq < total; ++seq) {
    if ((HAL_GetTick() - upload_started) >= SNAPSHOT_UPLOAD_TIMEOUT_MS)
      return false;
    size_t offset = (size_t)seq * SNAPSHOT_CHUNK_BYTES;
    size_t chunk = length - offset;
    if (chunk > SNAPSHOT_CHUNK_BYTES) chunk = SNAPSHOT_CHUNK_BYTES;
    base64_encode(jpeg + offset, chunk, encoded);
    int written = snprintf(payload, sizeof(payload), "%s|%lu|%lu|%08lx|%s",
                           capture_id, (unsigned long)seq,
                           (unsigned long)total, (unsigned long)crc, encoded);
    if (written <= 0 || written >= (int)sizeof(payload) ||
        !PublishRawAndWait("aihear/snapshot", payload, service)) return false;
  }
  return true;
}

bool WifiIoT_PublishSnapshotStatus(const char *capture_id,
                                   const char *status,
                                   WifiIoTServiceFn service)
{
  if (!capture_id || !capture_id[0] || !status || !status[0] ||
      !WifiIoT_IsReady()) return false;
  char payload[80];
  int written = snprintf(payload, sizeof(payload), "!|%s|%s",
                         capture_id, status);
  return written > 0 && written < (int)sizeof(payload) &&
         PublishRawAndWait("aihear/snapshot", payload, service);
}

static bool uart_start_rx(void)
{
  rx_dma_wraps = 0;
  rx_dma_consumed = 0;
  rx_dma_restart_requested = 0;
  if (HAL_UART_Receive_DMA(&huart4, uart_rx_dma, UART_RX_DMA_SIZE) != HAL_OK) {
    return false;
  }
  /* Only the full-buffer interrupt is needed to extend the DMA write index. */
  __HAL_DMA_DISABLE_IT(&hdma_uart4_rx, DMA_IT_HT);
  return true;
}

static void invalidate_dma_chunk(uint32_t offset, uint32_t length)
{
  if (length == 0U) return;

  uintptr_t start = (uintptr_t)&uart_rx_dma[offset];
  uintptr_t end = start + length;
  start &= ~(uintptr_t)31U;
  end = (end + 31U) & ~(uintptr_t)31U;
  SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

static void invalidate_dma_range(uint32_t offset, uint32_t length)
{
  uint32_t first = UART_RX_DMA_SIZE - offset;
  if (first > length) first = length;
  invalidate_dma_chunk(offset, first);
  invalidate_dma_chunk(0U, length - first);
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
  else if ((p = strstr(line, "+CMD:")) != NULL) {
    /* Remote command from App.  Enqueue for freertos.c to consume. */
    const char *cmd = p + 5;
    uint8_t next = (cmd_wr + 1) & (CMD_QUEUE_SIZE - 1);
    if (next != cmd_rd) {  /* not full */
      uint8_t slot = cmd_wr;
      strncpy(cmd_queue[slot], cmd, sizeof(cmd_queue[slot]) - 1);
      cmd_queue[slot][sizeof(cmd_queue[slot]) - 1] = '\0';
      /* strip trailing \r */
      size_t cl = strlen(cmd_queue[slot]);
      if (cl > 0 && cmd_queue[slot][cl - 1] == '\r')
        cmd_queue[slot][cl - 1] = '\0';
      cmd_wr = next;
      printf("[CMD-Q] enqueued '%s'\r\n", cmd_queue[slot]);
    } else {
      printf("[CMD-Q] FULL dropped '%s'\r\n", cmd);
    }
  }
  else {
    /* Unknown line — print for debugging */
    printf("[WiFiIoT] RX: '%.80s'\r\n", line);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4) {
    rx_dma_wraps++;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART4) {
    rx_dma_restart_requested = 1;
  }
}

void WifiIoT_Init(void)
{
  /* ESP RST pin (PA2) — output high, don't reset */
  GPIO_InitTypeDef g = {0};
  g.Pin   = ESP_RST_PIN;
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ESP_RST_PORT, &g);
  HAL_GPIO_WritePin(ESP_RST_PORT, ESP_RST_PIN, GPIO_PIN_SET);

  /* Track a cold boot that never reaches +READY, not only a later link loss. */
  esp_down_since_ms = HAL_GetTick();
  if (esp_down_since_ms == 0) esp_down_since_ms = 1;

  if (!uart_start_rx()) {
    printf("[WiFiIoT] UART4 RX DMA start failed\r\n");
  }
  printf("[WiFiIoT] Init done, waiting for ESP +READY...\r\n");
}

static void process_rx_char(char c)
{
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

void WifiIoT_Process(void)
{
  if (rx_dma_restart_requested) {
    HAL_UART_AbortReceive(&huart4);
    if (!uart_start_rx()) return;
    printf("[WiFiIoT] UART4 RX DMA restarted after error\r\n");
  }

  uint32_t wraps_before, wraps_after, write_pos;
  do {
    wraps_before = rx_dma_wraps;
    write_pos = UART_RX_DMA_SIZE - __HAL_DMA_GET_COUNTER(&hdma_uart4_rx);
    wraps_after = rx_dma_wraps;
  } while (wraps_before != wraps_after);

  uint64_t produced = (uint64_t)wraps_before * UART_RX_DMA_SIZE + write_pos;
  if (produced < rx_dma_consumed) {
    return;
  }

  uint64_t available = produced - rx_dma_consumed;
  if (available > UART_RX_DMA_SIZE) {
    rx_overflow++;
    rx_dma_consumed = produced - UART_RX_DMA_SIZE;
    available = UART_RX_DMA_SIZE;
  }

  /* D2 SRAM is cacheable. Invalidate only bytes completed by DMA before the
     CPU parses them; the buffer is aligned so no unrelated data is evicted. */
  uint32_t read_pos = (uint32_t)rx_dma_consumed & (UART_RX_DMA_SIZE - 1U);
  invalidate_dma_range(read_pos, (uint32_t)available);

  while (rx_dma_consumed < produced) {
    char c = (char)uart_rx_dma[(uint32_t)rx_dma_consumed &
                               (UART_RX_DMA_SIZE - 1U)];
    rx_dma_consumed++;
    process_rx_char(c);
  }
}

/* Publish if link is up, otherwise enqueue for later retransmission.
   State/data topics coalesce to the newest value. Alerts remain ordered. */
void WifiIoT_Publish(const char *topic, const char *payload)
{
  if (WifiIoT_IsReady()) {
    PublishRaw(topic, payload);
    return;
  }

  /* ── Link down: enqueue ── */
  /* 1) overwrite same-topic state/data, but never collapse alert events. */
  if (strcmp(topic, "aihear/alert") != 0) {
    for (int i = 0; i < PENDING_QUEUE_SIZE; i++) {
      if (pending_queue[i].valid && strcmp(pending_queue[i].topic, topic) == 0) {
        strncpy(pending_queue[i].payload, payload,
                sizeof(pending_queue[i].payload) - 1);
        pending_queue[i].payload[sizeof(pending_queue[i].payload) - 1] = '\0';
        printf("[WiFiIoT] Q-update '%s'\r\n", topic);
        return;
      }
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
    esp_down_since_ms = HAL_GetTick();
  }

  /* Auto hard-reset ESP if link stays down for ESP_RESET_DEADLINE_MS */
  if (!ready && esp_down_since_ms > 0 &&
      (HAL_GetTick() - esp_down_since_ms) >= ESP_RESET_DEADLINE_MS) {
    if (esp_reset_count < ESP_MAX_RESETS) {
      printf("[WiFiIoT] ESP dead >%lums — hard reset (attempt %d/%d)\r\n",
             (unsigned long)ESP_RESET_DEADLINE_MS,
             esp_reset_count + 1, ESP_MAX_RESETS);
      WifiIoT_ResetEsp();
      esp_down_since_ms  = HAL_GetTick();  /* reset deadline after reboot */
      esp_reset_count++;
    } else {
      /* give up — don't keep resetting forever */
      if (esp_down_since_ms > 0 && (HAL_GetTick() - esp_down_since_ms) >=
          ESP_RESET_DEADLINE_MS + 10000) {
        printf("[WiFiIoT] ESP max resets reached — giving up\r\n");
        esp_down_since_ms = 0; /* stop trying */
      }
    }
  }

  if (ready && !was_ready) {
    if (pending_count > 0) {
      printf("[WiFiIoT] Link recovered — flushing %d pending\r\n", pending_count);
      for (int i = 0; i < PENDING_QUEUE_SIZE; i++) {
        if (pending_queue[i].valid) {
          PublishRaw(pending_queue[i].topic, pending_queue[i].payload);
          pending_queue[i].valid = 0;
          pending_count--;
        }
      }
    }
    esp_down_since_ms = 0;  /* link healthy again */
  }
  was_ready = ready;
}

void WifiIoT_ResetEsp(void)
{
  printf("[WiFiIoT] Hard-resetting ESP (PA2 low)...\r\n");
  HAL_GPIO_WritePin(ESP_RST_PORT, ESP_RST_PIN, GPIO_PIN_RESET);
  HAL_Delay(10);   /* RST must be low ≥ 100 µs */
  HAL_GPIO_WritePin(ESP_RST_PORT, ESP_RST_PIN, GPIO_PIN_SET);
  HAL_Delay(500);  /* give ESP time to boot */

  /* Reset state — ESP will re-announce +DEVICEID and +READY */
  wifi_state  = 0;
  mqtt_state  = 0;
  esp_ready   = 0;
  last_status_ms = 0;
  printf("[WiFiIoT] ESP reset complete, waiting for +READY...\r\n");
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

/* ── Remote command queue ── */
bool WifiIoT_HasCommand(void) { return cmd_wr != cmd_rd; }

const char* WifiIoT_PopCommand(void)
{
  if (cmd_wr == cmd_rd) return NULL;
  const char *cmd = cmd_queue[cmd_rd];
  cmd_rd = (cmd_rd + 1) & (CMD_QUEUE_SIZE - 1);
  return cmd;
}
