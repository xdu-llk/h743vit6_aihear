/*
 * freertos.c — AI Hear dual-model FreeRTOS
 * KWS (130ms) → help detection, Baby-cry (2000ms) → only if KWS negative
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
#include <stdio.h>
#include <string.h>

#define RECORD_MODE 0

TaskHandle_t hAudioTask = NULL;

static void vAudioInferTask(void *pvParameters)
{
  (void)pvParameters;
  TickType_t last_heartbeat = xTaskGetTickCount();
  TickType_t last_status_query = 0;
  TickType_t alert_until = 0;
  uint8_t armed = 1;

  vTaskDelay(pdMS_TO_TICKS(1200));
  IWDG1->KR = 0xAAAA;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    IWDG1->KR = 0xAAAA;

    if ((xTaskGetTickCount() - last_status_query) >= pdMS_TO_TICKS(5000)) {
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

    float scores_bc[2], scores_kws[2];
    if (!armed) {
      OLED_ShowStatus("DISARMED", NULL, dbfs);
    } else if (peak < 70000) {
      OLED_ShowStatus("ARMED", NULL, dbfs);
    } else if (xTaskGetTickCount() < alert_until) {
    } else {
      bool alarmed = false;

      /* KWS first (fast ~130ms) */
      if (TFLM_KWS_Infer(feat, scores_kws)) {
        int kws_top = (scores_kws[0] >= scores_kws[1]) ? 0 : 1;
        float kws_margin = kws_top == 1 ? (scores_kws[1] - scores_kws[0])
                                        : (scores_kws[0] - scores_kws[1]);

        static int kws_cnt = 0;
        if (++kws_cnt % 10 == 1 || kws_top == 1) {
          printf("\r\n[KWS] %s m=%.2f (%.2f/%.2f) pk=%lu\r\n",
                 kws_top == 1 ? "help" : "bg",
                 (double)kws_margin,
                 (double)scores_kws[0], (double)scores_kws[1], peak);
        }

        if (kws_top == 1 && kws_margin >= 0.2f) {
          printf("\r\n[DETECT] help m=%.2f pk=%lu\r\n", (double)kws_margin, peak);
          OLED_ShowStatus("ALERT!", "help", dbfs);
          Alarm_SetState(ALARM_STATE_ALERT);
          alert_until = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
          Buzzer_Siren();
          IWDG1->KR = 0xAAAA;
          { char p[32]; snprintf(p, sizeof(p), "help:%.2f", (double)kws_margin);
            WifiIoT_Publish("aihear/alert", p); }
          alarmed = true;
        }
      }

      /* Baby-cry second (heavy ~2000ms), only if KWS didn't fire */
      if (!alarmed && TFLM_BabyCry_Infer(feat, scores_bc)) {
        int top = (scores_bc[0] >= scores_bc[1]) ? 0 : 1;
        float margin = top == 0 ? (scores_bc[0] - scores_bc[1])
                                : (scores_bc[1] - scores_bc[0]);
        static const char *names[] = {"baby_cry", "other"};

        if (top == 0 && margin >= 0.8f) {
          printf("\r\n[DETECT] %s m=%.2f pk=%lu\r\n", names[0], (double)margin, peak);
          OLED_ShowStatus("ALERT!", names[0], dbfs);
          Alarm_SetState(ALARM_STATE_ALERT);
          alert_until = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
          Buzzer_Siren();
          IWDG1->KR = 0xAAAA;
          { char p[32]; snprintf(p, sizeof(p), "%s:%.2f", names[0], (double)margin);
            WifiIoT_Publish("aihear/alert", p); }
        } else if (top == 0) {
          const char *lvl = (margin >= 0.5f) ? "MAYBE" : "WEAK";
          static int cnt = 0;
          if (++cnt % 10 == 1)
            printf("\r\n[%s] %s m=%.2f pk=%lu\r\n", lvl, names[0], (double)margin, peak);
          OLED_ShowStatus(lvl, NULL, dbfs);
        } else {
          static int cnt2 = 0;
          if (++cnt2 % 10 == 1)
            printf("\r\n[NORMAL] %s m=%.2f pk=%lu\r\n", names[1], (double)margin, peak);
          OLED_ShowStatus("NORMAL", NULL, dbfs);
        }
      }
    }

    WifiIoT_Process();
    WifiIoT_FlushPending();
    Alarm_Process();

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
      else       { Alarm_SetState(ALARM_STATE_IDLE);  OLED_ShowStatus("DISARMED", NULL, 0); }
    }
#endif

    if (Alarm_GetState() == ALARM_STATE_ALERT && xTaskGetTickCount() > alert_until)
      Alarm_SetState(armed ? ALARM_STATE_ARMED : ALARM_STATE_IDLE);

    if ((xTaskGetTickCount() - last_heartbeat) >= pdMS_TO_TICKS(30000)) {
      last_heartbeat = xTaskGetTickCount();
      const char *devid = WifiIoT_GetDeviceId();
      if (devid && WifiIoT_IsReady())
        WifiIoT_Publish("aihear/status", devid);
    }
  }
}

void vApplicationTickHook(void) {}
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  printf("\r\n[PANIC] %s\r\n", pcTaskName);
  __disable_irq();
  for (;;) {}
}

void FreeRTOS_Init(void) {
  OLED_Init(); Button_Init();
  OLED_ShowStatus("BOOTING..", NULL, 0);
  BaseType_t ret;
  ret = xTaskCreate(vAudioInferTask, "AudioInfer", 4096, NULL, 3, &hAudioTask);
  configASSERT(ret == pdPASS);
  printf("[FreeRTOS] 1 task. Starting scheduler...\r\n");
  vTaskStartScheduler();
  for (;;) {}
}
