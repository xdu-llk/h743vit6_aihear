/*
 * freertos.c — AI Hear single-task FreeRTOS
 *
 * DMA → preproc → TFLM 2-class (baby_cry/other) → alarm → MQTT
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

#define RECORD_MODE 0  /* Set to 1 to dump PCM via serial for data collection */

TaskHandle_t hAudioTask = NULL;

static void vAudioInferTask(void *pvParameters)
{
  (void)pvParameters;
  TickType_t last_heartbeat = xTaskGetTickCount();
  TickType_t alert_until = 0;
  uint32_t quiet_cnt = 0;
  uint8_t armed = 1;  /* 1=ARMED, 0=DISARMED */

  vTaskDelay(pdMS_TO_TICKS(1200));   /* 1.2s to fill ring with 15712+ samples */

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    IWDG1->KR = 0xAAAA;  /* feed watchdog */

#if !RECORD_MODE
    Button_Poll();
#endif

    /* Feed 32 frames OLDEST-FIRST so spectrogram time axis matches training.
       ring[wr-offset] reads backwards in time; oldest has largest offset. */
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

#if RECORD_MODE
    /* Fast PCM dump: send 2000 samples (~1s serial), skip quiet, 2s cooldown. */
    {
      static uint32_t cooldown = 0;
      static int32_t pcm[5472];  /* exactly 1 inference window */

      if (cooldown > 0) {
        cooldown--;
      } else if (peak > 40000) {
        cooldown = 4;  /* ~2s cooldown */
        Audio_ReadSamples(pcm, 5472, 5472);
        printf("[PCM]\n");
        for (int i = 0; i < 5472; i += 8) printf("%ld ", (long)pcm[i]);
        printf("\n[/PCM]\n");
      }
    }
#endif

    float scores[2];
    if (!armed) {
      OLED_ShowStatus("DISARMED", NULL, peak);
    } else if (peak < 70000) {
      OLED_ShowStatus("ARMED", NULL, peak);
    } else if (xTaskGetTickCount() < alert_until) {
    } else if (TFLM_Infer(feat, scores)) {
      int top = (scores[0] >= scores[1]) ? 0 : 1;
      float margin = (top == 0) ? (scores[0] - scores[1]) : (scores[1] - scores[0]);
      static const char *names[] = {"baby_cry", "other"};

      if (top == 0 && margin >= 0.8f) {
        printf("\r\n[DETECT] %s m=%.2f (%+.2f %+.2f) pk=%lu\r\n",
               names[0], (double)margin, (double)scores[0], (double)scores[1], peak);
        OLED_ShowStatus("ALERT!", names[0], peak);
        Alarm_SetState(ALARM_STATE_ALERT);
        alert_until = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
        Buzzer_Siren();
        {
          char payload[32];
          snprintf(payload, sizeof(payload), "%s:%.2f", names[0], (double)margin);
          if (WifiIoT_IsReady()) WifiIoT_Publish("aihear/alert", payload);
        }
      } else if (top == 0) {
        /* baby_cry sub-threshold: MAYBE or WEAK */
        const char *lvl = (margin >= 0.5f) ? "MAYBE" : "WEAK";
        static int cnt = 0;
        if (++cnt % 10 == 1)
          printf("\r\n[%s] %s m=%.2f pk=%lu\r\n", lvl, names[0], (double)margin, peak);
        OLED_ShowStatus(lvl, NULL, peak);
      } else {
        /* other: always NORMAL */
        static int cnt2 = 0;
        if (++cnt2 % 10 == 1)
          printf("\r\n[NORMAL] %s m=%.2f pk=%lu\r\n", names[1], (double)margin, peak);
        OLED_ShowStatus("NORMAL", NULL, peak);
      }
    }

    WifiIoT_Process();
    Alarm_Process();

#if !RECORD_MODE
    /* Button: long press >= 3s = soft reset */
    if (Button_HeldMs() >= 3000) {
      printf("\r\n[BTN] Long press -> soft reset\r\n");
      OLED_ShowStatus("RESET...", NULL, 0);
      HAL_Delay(500);
      NVIC_SystemReset();
    }
    /* Button: short press = toggle armed/disarmed */
    if (Button_WasPressed()) {
      armed = !armed;
      printf("\r\n[BTN] %s\r\n", armed ? "ARMED" : "DISARMED");
      if (armed) { Alarm_SetState(ALARM_STATE_ARMED); OLED_ShowStatus("ARMED", NULL, 0); }
      else       { Alarm_SetState(ALARM_STATE_IDLE);  OLED_ShowStatus("DISARMED", NULL, 0); }
    }
#endif

    /* Auto-recover from ALERT to ARMED (only if currently armed) */
    if (Alarm_GetState() == ALARM_STATE_ALERT &&
        xTaskGetTickCount() > alert_until) {
      Alarm_SetState(armed ? ALARM_STATE_ARMED : ALARM_STATE_IDLE);
    }

    if ((xTaskGetTickCount() - last_heartbeat) >= pdMS_TO_TICKS(30000)) {
      last_heartbeat = xTaskGetTickCount();
      if (WifiIoT_IsReady()) {
        char status[48];
        snprintf(status, sizeof(status), "uptime=%lus",
                 (unsigned long)(xTaskGetTickCount() / 1000));
        WifiIoT_Publish("aihear/status", status);
      }
    }
  }
}

void vApplicationTickHook(void) {}
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  printf("\r\n[PANIC] %s\r\n", pcTaskName);
  __disable_irq();
  for (;;) {}
}

void FreeRTOS_Init(void)
{
  OLED_Init();
  Button_Init();
  OLED_ShowStatus("BOOTING..", NULL, 0);

  BaseType_t ret;
  ret = xTaskCreate(vAudioInferTask, "AudioInfer", 2048, NULL, 3, &hAudioTask);
  configASSERT(ret == pdPASS);
  printf("[FreeRTOS] 1 task. Starting scheduler...\r\n");
  vTaskStartScheduler();
  for (;;) {}
}
