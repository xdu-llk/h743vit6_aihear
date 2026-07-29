/*
 * freertos.c — AI Hear single-task FreeRTOS
 * DMA -> preproc -> TFLM 2-class (baby_cry/other) -> alarm -> MQTT
 */
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "audio.h"
#include "audio_preproc.h"
#include "tflm_runner.h"
#include "alarm.h"
#include "buzzer.h"
#include "wifi_iot.h"
#include "oled.h"
#include "button.h"
#include "dht11.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define RECORD_MODE 0
#define AUDIO_GATE_PEAK       70000U
#define VOTE_STEP_SAMPLES     (20U * PREPROC_HOP_LEN)
#define VOTE_WAIT_TIMEOUT_MS  500U
#define ENV_READ_PERIOD_MS    2000U
#define ENV_ALERT_COOLDOWN_MS 300000U

TaskHandle_t hAudioTask = NULL;

static uint8_t PublishControlState(uint8_t armed)
{
  const char *devid = WifiIoT_GetDeviceId();
  if (!devid || !devid[0]) return 0;

  char topic[64];
  char payload[160];
  snprintf(topic, sizeof(topic), "aihear/v1/demo/%s/state", devid);
  snprintf(payload, sizeof(payload),
           "{\"deviceId\":\"%s\",\"armed\":%s,\"music\":%s,\"uptimeMs\":%lu}",
           devid, armed ? "true" : "false",
           Buzzer_IsPlaying() ? "true" : "false",
           (unsigned long)HAL_GetTick());
  WifiIoT_Publish(topic, payload);
  return 1;
}

static uint32_t WindowPeak(const int32_t *pcm)
{
  uint32_t peak = 0;
  for (uint32_t i = 0; i < AUDIO_WINDOW_SAMPLES; i++) {
    int32_t value = pcm[i];
    uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
    if (magnitude > peak) peak = magnitude;
  }
  return peak;
}

static float WindowDBFS(const int32_t *pcm)
{
  float sum_sq = 0.0f;
  for (uint32_t i = AUDIO_WINDOW_SAMPLES - AUDIO_BUF_SIZE;
       i < AUDIO_WINDOW_SAMPLES; i++) {
    float value = (float)pcm[i];
    sum_sq += value * value;
  }
  float rms = sqrtf(sum_sq / AUDIO_BUF_SIZE);
  return rms < 1.0f ? -60.0f : 20.0f * log10f(rms / 8388608.0f);
}

static bool InferCryProbability(const int32_t *pcm, float *prob_cry)
{
  AudioPreproc_Reset();
  for (int frame = 0; frame < PREPROC_NUM_FRAMES; frame++) {
    AudioPreproc_FeedFrame(pcm + frame * PREPROC_HOP_LEN);
  }
  if (!AudioPreproc_IsReady()) return false;

  float scores[2];
  if (!TFLM_Infer(AudioPreproc_GetFeatures(), scores)) return false;
  float max_score = scores[0] > scores[1] ? scores[0] : scores[1];
  float cry_exp = expf(scores[0] - max_score);
  float other_exp = expf(scores[1] - max_score);
  *prob_cry = cry_exp / (cry_exp + other_exp);
  return true;
}

static bool WaitForNewAudio(uint32_t baseline, uint32_t required_samples)
{
  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(VOTE_WAIT_TIMEOUT_MS);
  while ((uint32_t)(Audio_GetSampleCount() - baseline) < required_samples) {
    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(deadline - now) <= 0) return false;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25));
    WifiIoT_Process();
    IWDG1->KR = 0xAAAA;
  }
  return true;
}

static bool EnvAlertReady(TickType_t now, TickType_t last_alert)
{
  return last_alert == 0 ||
         (now - last_alert) >= pdMS_TO_TICKS(ENV_ALERT_COOLDOWN_MS);
}

static void vAudioInferTask(void *pvParameters)
{
  (void)pvParameters;
  TickType_t last_heartbeat = xTaskGetTickCount();
  TickType_t last_status_query = 0;
  TickType_t last_env_read = xTaskGetTickCount();
  TickType_t last_control_publish = 0;
  TickType_t alert_until = 0;
  TickType_t env_alert_until = 0;
  /* 4 independent env alert cooldowns — 5 min each */
  TickType_t env_cooldown_temp_high = 0;
  TickType_t env_cooldown_temp_low  = 0;
  TickType_t env_cooldown_humi_high = 0;
  TickType_t env_cooldown_humi_low  = 0;
  uint8_t armed = 1;
  uint8_t last_published_armed = 0xff;
  uint8_t last_published_music = 0xff;
  uint8_t control_state_dirty = 1;

  vTaskDelay(pdMS_TO_TICKS(1200));

  Alarm_SetState(ALARM_STATE_ARMED);  /* boot into armed state — green blink */

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    /* Feed IWDG at most every 3s — not every loop (~16ms) */
    {
      static TickType_t last_iwdg = 0;
      TickType_t now = xTaskGetTickCount();
      if ((now - last_iwdg) >= pdMS_TO_TICKS(3000)) {
        IWDG1->KR = 0xAAAA;
        last_iwdg = now;
      }
    }

    if ((xTaskGetTickCount() - last_status_query) >= pdMS_TO_TICKS(10000)) {
      last_status_query = xTaskGetTickCount();
      WifiIoT_QueryStatus();
    }

#if !RECORD_MODE
    Button_Poll();
#endif

    float dbfs = Audio_GetDBFS();
    uint32_t peak = Audio_GetPeak();
    const int32_t *window = NULL;

#if RECORD_MODE
    {
      static uint32_t cooldown = 0;
      if (cooldown > 0) { cooldown--; }
      else if (peak > 40000) {
        cooldown = 4;
        window = Audio_CaptureWindow();
        if (!window) continue;
        printf("[PCM]\n");
        for (int i = AUDIO_WINDOW_SAMPLES - 5472; i < AUDIO_WINDOW_SAMPLES; i += 8)
          printf("%ld ", (long)window[i]);
        printf("\n[/PCM]\n");
      }
    }
#endif

    if (!armed) {
      OLED_ShowStatus("DISARMED", NULL, dbfs);
    } else if (xTaskGetTickCount() < alert_until || Buzzer_IsPlaying()) {
      /* cooldown or melody playing — skip inference to avoid feedback loop */
    } else if ((window = Audio_CaptureWindow()) == NULL) {
      OLED_ShowStatus("LISTENING", NULL, dbfs);
    } else if ((peak = WindowPeak(window)) < AUDIO_GATE_PEAK) {
      dbfs = WindowDBFS(window);
      OLED_ShowStatus("ARMED", NULL, dbfs);
    } else {
      float prob_cry = 0.0f;
      static const char *names[] = {"baby_cry", "other"};
      dbfs = WindowDBFS(window);

      if (!InferCryProbability(window, &prob_cry)) {
        printf("[TFLM] Inference failed\r\n");
      } else if (prob_cry >= 0.90f) {
        /* Tier 1: high confidence → immediate alert */
        printf("\r\n[DETECT] %s p=%.0f%% pk=%lu (instant)\r\n",
               names[0], (double)(prob_cry * 100), peak);
        OLED_ShowStatus("ALERT!", names[0], dbfs);
        Alarm_SetState(ALARM_STATE_ALERT);
        alert_until = xTaskGetTickCount() + pdMS_TO_TICKS(20000);
        Buzzer_PlayMelody();
        { char p[32]; snprintf(p, sizeof(p), "%s:%.2f", names[0], (double)prob_cry);
          WifiIoT_Publish("aihear/alert", p); }
      } else if (prob_cry >= 0.75f) {
        /* Tier 2: collect two genuinely newer windows, 0.2 s apart. */
        OLED_ShowStatus("VOTING..", NULL, dbfs);
        float probs[3] = {prob_cry, 0.0f, 0.0f};
        int valid_count = 1;
        for (int r = 0; r < 2; r++) {
          uint32_t baseline = Audio_GetSampleCount();
          if (!WaitForNewAudio(baseline, VOTE_STEP_SAMPLES)) break;
          const int32_t *next_window = Audio_CaptureWindow();
          if (!next_window || !InferCryProbability(next_window, &probs[r + 1])) break;
          valid_count++;
        }

        if (valid_count == 3) {
          float avg = (probs[0] + probs[1] + probs[2]) / 3.0f;
          float max_prob = probs[0], second_prob = 0.0f;
          for (int i = 1; i < 3; i++) {
            if (probs[i] >= max_prob) { second_prob = max_prob; max_prob = probs[i]; }
            else if (probs[i] > second_prob) { second_prob = probs[i]; }
          }
          bool detected = max_prob >= 0.90f || avg >= 0.85f ||
                          (max_prob >= 0.85f && second_prob >= 0.60f);
          float decision_score = avg >= 0.85f ? avg : max_prob;
          printf("\r\n[VOTE] p1=%.0f%% p2=%.0f%% p3=%.0f%% avg=%.1f%% pk=%lu\r\n",
                 (double)(probs[0]*100), (double)(probs[1]*100), (double)(probs[2]*100),
                 (double)(avg*100), peak);
          if (detected) {
            printf("\r\n[DETECT] %s score=%.0f%% (temporal)\r\n",
                   names[0], (double)(decision_score*100));
            OLED_ShowStatus("ALERT!", names[0], dbfs);
            Alarm_SetState(ALARM_STATE_ALERT);
            alert_until = xTaskGetTickCount() + pdMS_TO_TICKS(20000);
            Buzzer_PlayMelody();
            { char p[32]; snprintf(p, sizeof(p), "%s:%.2f", names[0], (double)decision_score);
              WifiIoT_Publish("aihear/alert", p); }
          }
        } else {
          printf("[VOTE] Timed out waiting for fresh audio (%d/3 valid)\r\n", valid_count);
        }
        AudioPreproc_Reset();  /* fresh start for next round */
      } else {
        /* Tier 3: low confidence → skip */
        static int cnt = 0;
        if (++cnt % 10 == 1)
          printf("\r\n[NORMAL] pk=%lu\r\n", peak);
        OLED_ShowStatus("NORMAL", NULL, dbfs);
      }
    }

    WifiIoT_Process(); WifiIoT_FlushPending();

    /* Handle remote commands from App (via MQTT → ESP → UART) */
    while (WifiIoT_HasCommand()) {
      const char *cmd = WifiIoT_PopCommand();
      if (!cmd) continue;
      if (strstr(cmd, "STOP_MUSIC") || strstr(cmd, "stop_music")) {
        Buzzer_StopMelody();
        control_state_dirty = 1;
        printf("\r\n[CMD] App: stop music\r\n");
      } else if (strstr(cmd, "PLAY_MUSIC") || strstr(cmd, "play_music")) {
        if (!Buzzer_IsPlaying()) {
          Buzzer_PlayMelody();
        }
        control_state_dirty = 1;
        printf("\r\n[CMD] App: play music\r\n");
      } else if (strstr(cmd, "DISARM") || strstr(cmd, "disarm")) {
        if (armed) {
          armed = 0;
          Buzzer_StopMelody();
          Alarm_SetState(ALARM_STATE_IDLE);
          OLED_ShowStatus("DISARMED", NULL, 0);
        }
        control_state_dirty = 1;
        printf("\r\n[CMD] App: disarmed\r\n");
      } else if (strstr(cmd, "ARM") || strstr(cmd, "arm")) {
        if (!armed) {
          armed = 1;
          Buzzer_StopMelody();
          Alarm_SetState(ALARM_STATE_ARMED);
          OLED_ShowStatus("ARMED", NULL, 0);
        }
        control_state_dirty = 1;
        printf("\r\n[CMD] App: armed\r\n");
      } else {
        printf("\r\n[CMD] Unknown: '%s'\r\n", cmd);
      }
    }

    Buzzer_MelodyTick();
    Alarm_Process();

    if ((xTaskGetTickCount() - last_env_read) >= pdMS_TO_TICKS(ENV_READ_PERIOD_MS)) {
      last_env_read = xTaskGetTickCount();
      float temp_c = 0.0f, humi_pct = 0.0f;
      if (DHT11_Read(&temp_c, &humi_pct)) {
        OLED_SetEnvironment(temp_c, humi_pct, 1);
        const char *devid = WifiIoT_GetDeviceId();
        char payload[128];
        snprintf(payload, sizeof(payload),
                 "{\"deviceId\":\"%s\",\"temp\":%.1f,\"humi\":%.1f,\"uptimeMs\":%lu}",
                 devid ? devid : "", (double)temp_c, (double)humi_pct,
                 (unsigned long)HAL_GetTick());
        WifiIoT_Publish("aihear/env", payload);

        /* ── Environment threshold alerts (armed only) ── */
        if (armed && WifiIoT_IsReady()) {
          TickType_t now = xTaskGetTickCount();
          const char *env_alert = NULL;

          if (temp_c > 30.0f && EnvAlertReady(now, env_cooldown_temp_high))
            { env_alert = "temp_high"; env_cooldown_temp_high = now; }
          else if (temp_c < 16.0f && EnvAlertReady(now, env_cooldown_temp_low))
            { env_alert = "temp_low"; env_cooldown_temp_low = now; }
          else if (humi_pct > 80.0f && EnvAlertReady(now, env_cooldown_humi_high))
            { env_alert = "humi_high"; env_cooldown_humi_high = now; }
          else if (humi_pct < 30.0f && EnvAlertReady(now, env_cooldown_humi_low))
            { env_alert = "humi_low"; env_cooldown_humi_low = now; }

          if (env_alert) {
            printf("\r\n[ENV] %s t=%.1f h=%.1f\r\n", env_alert, (double)temp_c, (double)humi_pct);
            OLED_ShowStatus("ALERT!", env_alert, 0);
            /* cry alert has priority — don't overwrite */
            if (Alarm_GetState() != ALARM_STATE_ALERT) {
              Alarm_SetState(ALARM_STATE_ENV_ALERT);
              env_alert_until = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
            }
            WifiIoT_Publish("aihear/alert", env_alert);
          }
        }
      } else {
        OLED_SetEnvironment(0.0f, 0.0f, 0);
        printf("[DHT11] failed: %s bit=%d\r\n",
               DHT11_GetLastError(), DHT11_GetLastErrorBit());
      }
    }

#if !RECORD_MODE
    if (Button_HeldMs() >= 3000) {
      printf("\r\n[BTN] Long press -> soft reset\r\n");
      OLED_ShowStatus("RESET...", NULL, 0);
      HAL_Delay(500); NVIC_SystemReset();
    }
    if (Button_WasPressed()) {
      armed = !armed;
      printf("\r\n[BTN] %s\r\n", armed ? "ARMED" : "DISARMED");
      if (armed) { Alarm_SetState(ALARM_STATE_ARMED); OLED_ShowStatus("ARMED", NULL, 0); }
      else       { Buzzer_StopMelody(); Alarm_SetState(ALARM_STATE_IDLE); OLED_ShowStatus("DISARMED", NULL, 0); }
      control_state_dirty = 1;
    }
#endif

    {
      AlarmState st = Alarm_GetState();
      if (st == ALARM_STATE_ALERT && xTaskGetTickCount() > alert_until)
        Alarm_SetState(armed ? ALARM_STATE_ARMED : ALARM_STATE_IDLE);
      if (st == ALARM_STATE_ENV_ALERT && xTaskGetTickCount() > env_alert_until)
        Alarm_SetState(armed ? ALARM_STATE_ARMED : ALARM_STATE_IDLE);
    }

    {
      TickType_t now = xTaskGetTickCount();
      uint8_t music = Buzzer_IsPlaying() ? 1 : 0;
      uint8_t changed = (armed != last_published_armed) ||
                        (music != last_published_music);
      uint8_t periodic = WifiIoT_IsReady() &&
                         ((now - last_control_publish) >= pdMS_TO_TICKS(30000));
      if (control_state_dirty || changed || periodic) {
        if (PublishControlState(armed)) {
          last_published_armed = armed;
          last_published_music = music;
          last_control_publish = now;
          control_state_dirty = 0;
        }
      }
    }

    if ((xTaskGetTickCount() - last_heartbeat) >= pdMS_TO_TICKS(30000)) {
      last_heartbeat = xTaskGetTickCount();
      const char *devid = WifiIoT_GetDeviceId();
      if (devid && WifiIoT_IsReady()) {
        char topic[64];
        snprintf(topic, sizeof(topic), "aihear/v1/demo/%s/status", devid);
        WifiIoT_Publish(topic, devid);
      }
    }
  }
}

void vApplicationTickHook(void) {}
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask; printf("\r\n[PANIC] %s\r\n", pcTaskName);
  __disable_irq(); for (;;) {}
}

void FreeRTOS_Init(void) {
  OLED_Init(); Button_Init(); DHT11_Init(); OLED_ShowStatus("BOOTING..", NULL, 0);
  BaseType_t ret;
  ret = xTaskCreate(vAudioInferTask, "AudioInfer", 4096, NULL, 3, &hAudioTask);
  configASSERT(ret == pdPASS);
  printf("[FreeRTOS] 1 task. Starting scheduler...\r\n");
  vTaskStartScheduler(); for (;;) {}
}
