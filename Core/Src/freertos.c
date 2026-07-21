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

static void vAudioInferTask(void *pvParameters)
{
  (void)pvParameters;
  TickType_t last_heartbeat = xTaskGetTickCount();
  TickType_t last_status_query = 0;
  TickType_t last_env_read = xTaskGetTickCount();
  TickType_t last_control_publish = 0;
  TickType_t alert_until = 0;
  uint8_t armed = 1;
  uint8_t last_published_armed = 0xff;
  uint8_t last_published_music = 0xff;
  uint8_t control_state_dirty = 1;

  vTaskDelay(pdMS_TO_TICKS(1200));

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

    AudioPreproc_Reset();
    for (int f = PREPROC_NUM_FRAMES - 1; f >= 0; f--) {
      int32_t frame[PREPROC_FFT_N];
      int offset = PREPROC_FFT_N + f * PREPROC_HOP_LEN;
      Audio_ReadSamples(frame, offset, PREPROC_FFT_N);
      AudioPreproc_FeedFrame((const int32_t *)frame);
    }

    if (!AudioPreproc_IsReady()) continue;

    const float *feat = AudioPreproc_GetFeatures();
    uint32_t peak = Audio_GetPeak();
    float dbfs = Audio_GetDBFS();

#if RECORD_MODE
    {
      static uint32_t cooldown = 0;
      static int32_t pcm[5472];
      if (cooldown > 0) { cooldown--; }
      else if (peak > 40000) {
        cooldown = 4;
        Audio_ReadSamples(pcm, 5472, 5472);
        printf("[PCM]\n");
        for (int i = 0; i < 5472; i += 8) printf("%ld ", (long)pcm[i]);
        printf("\n[/PCM]\n");
      }
    }
#endif

    float scores[2];
    if (!armed) {
      OLED_ShowStatus("DISARMED", NULL, dbfs);
    } else if (peak < 70000) {
      OLED_ShowStatus("ARMED", NULL, dbfs);
    } else if (xTaskGetTickCount() < alert_until || Buzzer_IsPlaying()) {
      /* cooldown or melody playing — skip inference to avoid feedback loop */
    } else if (TFLM_Infer(feat, scores)) {
      /* Softmax: logits -> probabilities */
      float max_s = scores[0] > scores[1] ? scores[0] : scores[1];
      float e0 = expf(scores[0] - max_s), e1 = expf(scores[1] - max_s);
      float prob_cry = e0 / (e0 + e1);
      static const char *names[] = {"baby_cry", "other"};

      /* 3-tier dynamic inference:
       *   ≥90% → instant alert
       *   60-90% → slide 0.2s, 2 more inferences, avg ≥ 80% → alert
       *   <60% → skip */
      if (prob_cry >= 0.90f) {
        /* Tier 1: high confidence → immediate alert */
        printf("\r\n[DETECT] %s p=%.0f%% pk=%lu (instant)\r\n",
               names[0], (double)(prob_cry * 100), peak);
        OLED_ShowStatus("ALERT!", names[0], dbfs);
        Alarm_SetState(ALARM_STATE_ALERT);
        alert_until = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
        Buzzer_PlayMelody();
        { char p[32]; snprintf(p, sizeof(p), "%s:%.2f", names[0], (double)prob_cry);
          WifiIoT_Publish("aihear/alert", p); }
      } else if (prob_cry >= 0.60f) {
        /* Tier 2: medium confidence → slide 0.2s (20 frames), 2 more inferences, vote */
        OLED_ShowStatus("VOTING..", NULL, dbfs);
        float probs[3] = {prob_cry, 0.0f, 0.0f};
        for (int r = 0; r < 2; r++) {
          AudioPreproc_Shift(20);  /* keep 76 newest frames, need 20 more */
          for (int f = 19; f >= 0; f--) {
            int32_t frame[PREPROC_FFT_N];
            int offset = PREPROC_FFT_N + f * PREPROC_HOP_LEN;
            Audio_ReadSamples((int32_t *)frame, offset, PREPROC_FFT_N);
            AudioPreproc_FeedFrame((const int32_t *)frame);
          }
          if (AudioPreproc_IsReady()) {
            float s2[2];
            if (TFLM_Infer(AudioPreproc_GetFeatures(), s2)) {
              float ms = s2[0] > s2[1] ? s2[0] : s2[1];
              float er0 = expf(s2[0] - ms), er1 = expf(s2[1] - ms);
              probs[r + 1] = er0 / (er0 + er1);
            }
          }
        }
        float avg = (probs[0] + probs[1] + probs[2]) / 3.0f;
        printf("\r\n[VOTE] p1=%.0f%% p2=%.0f%% p3=%.0f%% avg=%.0f%% pk=%lu\r\n",
               (double)(probs[0]*100), (double)(probs[1]*100), (double)(probs[2]*100),
               (double)(avg*100), peak);
        if (avg >= 0.80f) {
          printf("\r\n[DETECT] %s avg=%.0f%% (voted)\r\n", names[0], (double)(avg*100));
          OLED_ShowStatus("ALERT!", names[0], dbfs);
          Alarm_SetState(ALARM_STATE_ALERT);
          alert_until = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
          Buzzer_PlayMelody();
          { char p[32]; snprintf(p, sizeof(p), "%s:%.2f", names[0], (double)avg);
            WifiIoT_Publish("aihear/alert", p); }
        }
        AudioPreproc_Reset();  /* fresh start for next round */
      } else {
        /* Tier 3: low confidence → skip */
        static int cnt = 0;
        if (++cnt % 10 == 1)
          printf("\r\n[NORMAL] p=%.0f%% pk=%lu\r\n", (double)(prob_cry * 100), peak);
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

    if ((xTaskGetTickCount() - last_env_read) >= pdMS_TO_TICKS(10000)) {
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

    if (Alarm_GetState() == ALARM_STATE_ALERT && xTaskGetTickCount() > alert_until)
      Alarm_SetState(armed ? ALARM_STATE_ARMED : ALARM_STATE_IDLE);

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
