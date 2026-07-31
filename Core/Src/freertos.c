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
#include "dht11.h"
#include "tft_ui.h"
#include "env_monitor.h"
#include "mq135.h"
#include "mq135_monitor.h"
#include "gp2y1010.h"
#include "particle_monitor.h"
#include "camera.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define RECORD_MODE 0
#define AUDIO_GATE_PEAK       70000U
#define VOTE_STEP_SAMPLES     (20U * PREPROC_HOP_LEN)
#define VOTE_WAIT_TIMEOUT_MS  500U
#define ENV_READ_PERIOD_MS    2000U
#define MQ135_READ_PERIOD_MS  1000U
#define PARTICLE_READ_PERIOD_MS 1000U
#define PARTICLE_PUBLISH_PERIOD_MS 2000U
#define ENV_ALERT_COOLDOWN_MS 300000U

TaskHandle_t hAudioTask = NULL;

static void Ui_ShowStatus(const char *status, const char *detail, float dbfs)
{
  TftUi_SetStatus(status, detail, dbfs);
}

static void Ui_SetEnvironment(float temp_c, float humi_pct, uint8_t valid)
{
  TftUi_SetEnvironment(temp_c, humi_pct, valid);
}

static void ServiceLongOperation(void)
{
  static uint32_t last_status_query_ms;
  uint32_t now = HAL_GetTick();

  WifiIoT_Process();
  if ((now - last_status_query_ms) >= 5000U) {
    WifiIoT_QueryStatus();
    last_status_query_ms = now;
  }
  Buzzer_MelodyTick();
  Alarm_Process();
  IWDG1->KR = 0xAAAA;
}

static void CaptureAndPublish(const char *source, float dbfs)
{
  uint32_t started = HAL_GetTick();
  char capture_id[16];
  snprintf(capture_id, sizeof(capture_id), "h%08lx",
           (unsigned long)started);
  if (!Camera_IsAvailable()) {
    printf("[CAM] %s capture skipped: camera unavailable\r\n", source);
    WifiIoT_PublishSnapshotStatus(capture_id, "unavailable",
                                  ServiceLongOperation);
    if (strcmp(source, "manual") == 0)
      Ui_ShowStatus("CAMERA OFF", "CHECK MODULE", dbfs);
    return;
  }

  Ui_ShowStatus("CAPTURING", source, dbfs);
  CameraCaptureResult result = Camera_CaptureEvent(1200U, ServiceLongOperation);
  if (result != CAMERA_CAPTURE_OK) {
    printf("[CAM] %s capture failed: %s (%lums)\r\n", source,
           Camera_ResultName(result), (unsigned long)(HAL_GetTick() - started));
    WifiIoT_PublishSnapshotStatus(capture_id, Camera_ResultName(result),
                                  ServiceLongOperation);
    Ui_ShowStatus("CAM ERROR", Camera_ResultName(result), dbfs);
    return;
  }

  size_t rgb_length = 0, jpeg_length = 0;
  const uint8_t *rgb = Camera_GetRgb565(&rgb_length);
  const uint8_t *jpeg = Camera_GetJpeg(&jpeg_length);
  bool displayed = TftUi_ShowSnapshot(rgb, (uint32_t)rgb_length);
  bool uploaded = WifiIoT_PublishSnapshot(jpeg, jpeg_length, capture_id,
                                          ServiceLongOperation);
  if (!uploaded)
    WifiIoT_PublishSnapshotStatus(capture_id, "upload_failed",
                                  ServiceLongOperation);
  printf("[CAM] %s capture id=%s jpeg=%uB tft=%s upload=%s total=%lums\r\n",
         source, capture_id, (unsigned)jpeg_length,
         displayed ? "ok" : "off", uploaded ? "ok" : "failed",
         (unsigned long)(HAL_GetTick() - started));
  TftUi_RestoreDashboard();
}

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

static const char *EnvValueState(uint8_t active_mask,
                                 EnvCondition high_condition,
                                 EnvCondition low_condition)
{
  if ((active_mask & (uint8_t)(1U << (uint8_t)high_condition)) != 0U)
    return "high";
  if ((active_mask & (uint8_t)(1U << (uint8_t)low_condition)) != 0U)
    return "low";
  return "normal";
}

static void PublishEnvironmentData(uint8_t dht_valid, uint8_t dht_fault,
                                   float temp_c, float humi_pct,
                                   uint8_t dht_active_mask,
                                   const Mq135MonitorUpdate *mq)
{
  const char *devid = WifiIoT_GetDeviceId();
  const char *gas_state = Mq135Monitor_GetStateName();
  char payload[232];
  int length;

  if (dht_valid && mq->ready) {
    length = snprintf(payload, sizeof(payload),
        "{\"deviceId\":\"%s\",\"temp\":%.1f,\"humi\":%.1f,"
        "\"tempState\":\"%s\",\"humiState\":\"%s\",\"dhtValid\":true,"
        "\"activeMask\":%u,\"gasIndex\":%.0f,\"gasState\":\"%s\","
        "\"mqReady\":true,\"mqFault\":false,\"uptimeMs\":%lu}",
        devid ? devid : "", (double)temp_c, (double)humi_pct,
        EnvValueState(dht_active_mask, ENV_CONDITION_TEMP_HIGH,
                      ENV_CONDITION_TEMP_LOW),
        EnvValueState(dht_active_mask, ENV_CONDITION_HUMI_HIGH,
                      ENV_CONDITION_HUMI_LOW),
        (unsigned)dht_active_mask, (double)mq->index, gas_state,
        (unsigned long)HAL_GetTick());
  } else if (dht_valid) {
    length = snprintf(payload, sizeof(payload),
        "{\"deviceId\":\"%s\",\"temp\":%.1f,\"humi\":%.1f,"
        "\"tempState\":\"%s\",\"humiState\":\"%s\",\"dhtValid\":true,"
        "\"activeMask\":%u,\"gasIndex\":null,\"gasState\":\"%s\","
        "\"mqReady\":false,"
        "\"mqFault\":%s,\"uptimeMs\":%lu}",
        devid ? devid : "", (double)temp_c, (double)humi_pct,
        EnvValueState(dht_active_mask, ENV_CONDITION_TEMP_HIGH,
                      ENV_CONDITION_TEMP_LOW),
        EnvValueState(dht_active_mask, ENV_CONDITION_HUMI_HIGH,
                      ENV_CONDITION_HUMI_LOW),
        (unsigned)dht_active_mask, gas_state,
        mq->phase == MQ135_PHASE_FAULT ? "true" : "false",
        (unsigned long)HAL_GetTick());
  } else if (mq->ready) {
    length = snprintf(payload, sizeof(payload),
        "{\"deviceId\":\"%s\",\"temp\":null,\"humi\":null,"
        "\"dhtValid\":false,\"dhtFault\":%s,"
        "\"gasIndex\":%.0f,\"gasState\":\"%s\",\"mqReady\":true,"
        "\"mqFault\":false,\"uptimeMs\":%lu}",
        devid ? devid : "", dht_fault ? "true" : "false",
        (double)mq->index, gas_state, (unsigned long)HAL_GetTick());
  } else {
    length = snprintf(payload, sizeof(payload),
        "{\"deviceId\":\"%s\",\"temp\":null,\"humi\":null,"
        "\"dhtValid\":false,\"dhtFault\":%s,\"gasIndex\":null,"
        "\"gasState\":\"%s\",\"mqReady\":false,\"mqFault\":%s,"
        "\"uptimeMs\":%lu}",
        devid ? devid : "", dht_fault ? "true" : "false", gas_state,
        mq->phase == MQ135_PHASE_FAULT ? "true" : "false",
        (unsigned long)HAL_GetTick());
  }

  if (length > 0 && length < (int)sizeof(payload)) {
    WifiIoT_Publish("aihear/env", payload);
  } else {
    printf("[ENV] payload overflow (%d bytes)\r\n", length);
  }
}

static void PublishParticleData(const ParticleMonitorUpdate *particle)
{
  const char *devid = WifiIoT_GetDeviceId();
  char payload[160];
  int length;

  if (particle->ready) {
    length = snprintf(payload, sizeof(payload),
        "{\"deviceId\":\"%s\",\"pmIndex\":%.0f,\"pmState\":\"%s\","
        "\"pmReady\":true,\"pmFault\":false,\"uptimeMs\":%lu}",
        devid ? devid : "", (double)particle->index,
        ParticleMonitor_GetStateName(), (unsigned long)HAL_GetTick());
  } else {
    length = snprintf(payload, sizeof(payload),
        "{\"deviceId\":\"%s\",\"pmIndex\":null,\"pmState\":\"%s\","
        "\"pmReady\":false,\"pmFault\":%s,\"uptimeMs\":%lu}",
        devid ? devid : "", ParticleMonitor_GetStateName(),
        particle->phase == PARTICLE_PHASE_FAULT ? "true" : "false",
        (unsigned long)HAL_GetTick());
  }

  if (length > 0 && length < (int)sizeof(payload)) {
    WifiIoT_Publish("aihear/env", payload);
  } else {
    printf("[PM] payload overflow (%d bytes)\r\n", length);
  }
}

static void vAudioInferTask(void *pvParameters)
{
  (void)pvParameters;
  TickType_t last_heartbeat = xTaskGetTickCount();
  TickType_t last_status_query = 0;
  TickType_t last_env_read = xTaskGetTickCount();
  TickType_t last_mq135_read = xTaskGetTickCount();
  TickType_t last_particle_read = xTaskGetTickCount();
  TickType_t last_particle_publish =
      xTaskGetTickCount() - pdMS_TO_TICKS(PARTICLE_READ_PERIOD_MS);
  TickType_t last_control_publish = 0;
  TickType_t alert_until = 0;
  /* 4 independent env alert cooldowns — 5 min each */
  TickType_t env_cooldown[ENV_CONDITION_COUNT] = {0};
  TickType_t mq135_cooldown = 0;
  TickType_t particle_cooldown = 0;
  uint8_t armed = 1;
  uint8_t last_published_armed = 0xff;
  uint8_t last_published_music = 0xff;
  uint8_t control_state_dirty = 1;
  const char *last_env_ui_name = NULL;

  vTaskDelay(pdMS_TO_TICKS(1200));

  Alarm_SetState(ALARM_STATE_ARMED);  /* boot into armed state — green blink */

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint8_t automatic_capture_requested = 0;

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
      Ui_ShowStatus("DISARMED", NULL, dbfs);
    } else if (xTaskGetTickCount() < alert_until || Buzzer_IsPlaying()) {
      /* cooldown or melody playing — skip inference to avoid feedback loop */
    } else if ((window = Audio_CaptureWindow()) == NULL) {
      Ui_ShowStatus("LISTENING", NULL, dbfs);
    } else if ((peak = WindowPeak(window)) < AUDIO_GATE_PEAK) {
      dbfs = WindowDBFS(window);
      Ui_ShowStatus("ARMED", NULL, dbfs);
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
        char ui_detail[24];
        snprintf(ui_detail, sizeof(ui_detail), "CRY %.0f%%", (double)(prob_cry * 100));
        Ui_ShowStatus("ALERT!", ui_detail, dbfs);
        Alarm_SetState(ALARM_STATE_ALERT);
        alert_until = xTaskGetTickCount() + pdMS_TO_TICKS(20000);
        Buzzer_PlayMelody();
        { char p[32]; snprintf(p, sizeof(p), "%s:%.2f", names[0], (double)prob_cry);
          WifiIoT_Publish("aihear/alert", p); }
        automatic_capture_requested = 1;
      } else if (prob_cry >= 0.75f) {
        /* Tier 2: collect two genuinely newer windows, 0.2 s apart. */
        Ui_ShowStatus("VOTING", "CHECK 1/3", dbfs);
        float probs[3] = {prob_cry, 0.0f, 0.0f};
        int valid_count = 1;
        for (int r = 0; r < 2; r++) {
          uint32_t baseline = Audio_GetSampleCount();
          if (!WaitForNewAudio(baseline, VOTE_STEP_SAMPLES)) break;
          const int32_t *next_window = Audio_CaptureWindow();
          if (!next_window || !InferCryProbability(next_window, &probs[r + 1])) break;
          valid_count++;
          char vote_detail[16];
          snprintf(vote_detail, sizeof(vote_detail), "CHECK %d/3", valid_count);
          Ui_ShowStatus("VOTING", vote_detail, dbfs);
        }

        if (valid_count == 3) {
          float avg = (probs[0] + probs[1] + probs[2]) / 3.0f;
          float max_prob = probs[0];
          for (int i = 1; i < 3; i++) {
            if (probs[i] > max_prob) max_prob = probs[i];
          }
          bool detected = max_prob >= 0.90f || avg >= 0.85f;
          float decision_score = avg >= 0.85f ? avg : max_prob;
          printf("\r\n[VOTE] p1=%.0f%% p2=%.0f%% p3=%.0f%% avg=%.1f%% pk=%lu\r\n",
                 (double)(probs[0]*100), (double)(probs[1]*100), (double)(probs[2]*100),
                 (double)(avg*100), peak);
          if (detected) {
            printf("\r\n[DETECT] %s score=%.0f%% (temporal)\r\n",
                   names[0], (double)(decision_score*100));
            char ui_detail[24];
            snprintf(ui_detail, sizeof(ui_detail), "CRY %.0f%%",
                     (double)(decision_score * 100));
            Ui_ShowStatus("ALERT!", ui_detail, dbfs);
            Alarm_SetState(ALARM_STATE_ALERT);
            alert_until = xTaskGetTickCount() + pdMS_TO_TICKS(20000);
            Buzzer_PlayMelody();
            { char p[32]; snprintf(p, sizeof(p), "%s:%.2f", names[0], (double)decision_score);
              WifiIoT_Publish("aihear/alert", p); }
            automatic_capture_requested = 1;
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
        Ui_ShowStatus("NORMAL", NULL, dbfs);
      }
    }

    if (automatic_capture_requested) CaptureAndPublish("cry", dbfs);

    WifiIoT_Process(); WifiIoT_FlushPending();

    /* Handle remote commands from App (via MQTT → ESP → UART) */
    while (WifiIoT_HasCommand()) {
      const char *cmd = WifiIoT_PopCommand();
      if (!cmd) continue;
      if (strstr(cmd, "CAPTURE") || strstr(cmd, "capture")) {
        printf("\r\n[CMD] App: capture\r\n");
        CaptureAndPublish("manual", dbfs);
      } else if (strstr(cmd, "STOP_MUSIC") || strstr(cmd, "stop_music")) {
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
          Ui_ShowStatus("DISARMED", NULL, dbfs);
        }
        control_state_dirty = 1;
        printf("\r\n[CMD] App: disarmed\r\n");
      } else if (strstr(cmd, "ARM") || strstr(cmd, "arm")) {
        if (!armed) {
          armed = 1;
          Buzzer_StopMelody();
          Alarm_SetState(ALARM_STATE_ARMED);
          Ui_ShowStatus("ARMED", NULL, dbfs);
        }
        control_state_dirty = 1;
        printf("\r\n[CMD] App: armed\r\n");
      } else {
        printf("\r\n[CMD] Unknown: '%s'\r\n", cmd);
      }
    }

    Buzzer_MelodyTick();
    Alarm_Process();

    if ((xTaskGetTickCount() - last_mq135_read) >=
        pdMS_TO_TICKS(MQ135_READ_PERIOD_MS)) {
      last_mq135_read = xTaskGetTickCount();
      uint16_t raw = 0;
      bool read_ok = Mq135_ReadRaw(&raw);
      Mq135MonitorUpdate mq =
          Mq135Monitor_Update(read_ok, raw, HAL_GetTick());
      TftUi_SetGasQuality(mq.index, mq.ready, mq.alert,
                          mq.phase == MQ135_PHASE_FAULT);

      static uint8_t mq_log_divider = 0;
      if (++mq_log_divider >= 5U || mq.newly_alert || mq.newly_recovered ||
          mq.fault_entered || mq.fault_recovered) {
        mq_log_divider = 0;
        printf("[MQ135] phase=%s raw=%u base=%u index=%.0f%s\r\n",
               Mq135Monitor_GetStateName(), (unsigned)mq.raw,
               (unsigned)mq.baseline, (double)mq.index,
               mq.alert ? " ALERT" : "");
      }

      TickType_t now = xTaskGetTickCount();
      if (mq.newly_alert ||
          (mq.alert && EnvAlertReady(now, mq135_cooldown))) {
        mq135_cooldown = now;
        printf("[ENV] confirmed mq135_high raw=%u base=%u index=%.0f\r\n",
               (unsigned)mq.raw, (unsigned)mq.baseline, (double)mq.index);
        WifiIoT_Publish("aihear/alert", "mq135_high");
      }
      if (mq.newly_recovered) {
        printf("[ENV] recovered mq135_high index=%.0f\r\n",
               (double)mq.index);
        WifiIoT_Publish("aihear/alert", "mq135_high_recovered");
      }
      if (mq.fault_entered) printf("[MQ135] sensor fault confirmed\r\n");
      if (mq.fault_recovered)
        printf("[MQ135] sensor recovered; restarting warmup\r\n");
    }

    if ((xTaskGetTickCount() - last_particle_read) >=
        pdMS_TO_TICKS(PARTICLE_READ_PERIOD_MS)) {
      last_particle_read = xTaskGetTickCount();
      uint16_t raw = 0;
      bool read_ok = Gp2y1010_ReadRaw(&raw);
      ParticleMonitorUpdate particle =
          ParticleMonitor_Update(read_ok, raw);
      TftUi_SetParticleQuality(particle.index, particle.ready,
                               particle.alert,
                               particle.phase == PARTICLE_PHASE_FAULT);

      static uint8_t particle_log_divider = 0;
      if (++particle_log_divider >= 5U || particle.newly_alert ||
          particle.newly_recovered || particle.fault_entered ||
          particle.fault_recovered) {
        particle_log_divider = 0;
        printf("[PM] phase=%s raw=%u base=%u index=%.0f%s\r\n",
               ParticleMonitor_GetStateName(), (unsigned)particle.raw,
               (unsigned)particle.baseline, (double)particle.index,
               particle.alert ? " ALERT" : "");
      }

      TickType_t now = xTaskGetTickCount();
      if (particle.newly_alert ||
          (particle.alert && EnvAlertReady(now, particle_cooldown))) {
        particle_cooldown = now;
        printf("[ENV] confirmed particle_high raw=%u base=%u index=%.0f\r\n",
               (unsigned)particle.raw, (unsigned)particle.baseline,
               (double)particle.index);
        WifiIoT_Publish("aihear/alert", "particle_high");
      }
      if (particle.newly_recovered) {
        printf("[ENV] recovered particle_high index=%.0f\r\n",
               (double)particle.index);
        WifiIoT_Publish("aihear/alert", "particle_high_recovered");
      }
      if (particle.fault_entered) printf("[PM] sensor fault confirmed\r\n");
      if (particle.fault_recovered)
        printf("[PM] sensor recovered; rebuilding baseline\r\n");

      if ((now - last_particle_publish) >=
          pdMS_TO_TICKS(PARTICLE_PUBLISH_PERIOD_MS)) {
        last_particle_publish = now;
        PublishParticleData(&particle);
      }
    }

    if ((xTaskGetTickCount() - last_env_read) >= pdMS_TO_TICKS(ENV_READ_PERIOD_MS)) {
      last_env_read = xTaskGetTickCount();
      float temp_c = 0.0f, humi_pct = 0.0f;
      bool dht_read_ok = DHT11_Read(&temp_c, &humi_pct);
      EnvMonitorUpdate env_update =
          EnvMonitor_UpdateDht(dht_read_ok, temp_c, humi_pct);

      if (env_update.sample_accepted) {
        Ui_SetEnvironment(temp_c, humi_pct, 1);
        Mq135MonitorUpdate mq = Mq135Monitor_GetState();
        PublishEnvironmentData(1, 0, temp_c, humi_pct,
                               env_update.active_mask, &mq);

        TickType_t now = xTaskGetTickCount();
        for (EnvCondition condition = ENV_CONDITION_TEMP_HIGH;
             condition < ENV_CONDITION_COUNT; condition++) {
          uint8_t bit = (uint8_t)(1U << (uint8_t)condition);
          if ((env_update.newly_active_mask & bit) != 0U ||
              ((env_update.active_mask & bit) != 0U &&
               EnvAlertReady(now, env_cooldown[condition]))) {
            const char *name = EnvMonitor_GetConditionName(condition);
            env_cooldown[condition] = now;
            printf("\r\n[ENV] confirmed %s t=%.1f h=%.1f\r\n",
                   name, (double)temp_c, (double)humi_pct);
            WifiIoT_Publish("aihear/alert", name);
          }
          if ((env_update.newly_recovered_mask & bit) != 0U) {
            const char *name = EnvMonitor_GetConditionName(condition);
            char recovery_name[32];
            snprintf(recovery_name, sizeof(recovery_name), "%s_recovered", name);
            printf("\r\n[ENV] recovered %s t=%.1f h=%.1f\r\n", name,
                   (double)temp_c, (double)humi_pct);
            WifiIoT_Publish("aihear/alert", recovery_name);
          }
        }
      } else {
        if (env_update.dht_fault) {
          Ui_SetEnvironment(0.0f, 0.0f, 0);
          Mq135MonitorUpdate mq = Mq135Monitor_GetState();
          PublishEnvironmentData(0, 1, 0.0f, 0.0f,
                                 env_update.active_mask, &mq);
        }
        if (!dht_read_ok) {
          printf("[DHT11] failed %u/3: %s bit=%d\r\n",
                 (unsigned)env_update.invalid_streak,
                 DHT11_GetLastError(), DHT11_GetLastErrorBit());
        }
      }

      if (env_update.fault_entered) {
        printf("[DHT11] sensor fault confirmed\r\n");
      } else if (env_update.fault_recovered) {
        printf("[DHT11] sensor recovered after 3 valid samples\r\n");
      }
    }

    {
      AlarmState st = Alarm_GetState();
      Mq135MonitorUpdate mq = Mq135Monitor_GetState();
      ParticleMonitorUpdate particle = ParticleMonitor_GetState();
      uint8_t env_active = EnvMonitor_GetActiveMask() != 0U || mq.alert ||
                           particle.alert;
      const char *env_name = EnvMonitor_GetFirstActiveName();
      if (env_name == NULL && mq.alert) env_name = "mq135_high";
      if (env_name == NULL && particle.alert) env_name = "particle_high";
      if (st == ALARM_STATE_ALERT && xTaskGetTickCount() > alert_until) {
        if (env_active) {
          Alarm_SetState(ALARM_STATE_ENV_ALERT);
          Ui_ShowStatus("ENV ALERT", env_name, dbfs);
          last_env_ui_name = env_name;
        } else {
          Alarm_SetState(armed ? ALARM_STATE_ARMED : ALARM_STATE_IDLE);
          Ui_ShowStatus(armed ? "ARMED" : "DISARMED", NULL, dbfs);
          last_env_ui_name = NULL;
        }
      } else if (st != ALARM_STATE_ALERT && env_active) {
        if (st != ALARM_STATE_ENV_ALERT) Alarm_SetState(ALARM_STATE_ENV_ALERT);
        if (st != ALARM_STATE_ENV_ALERT || last_env_ui_name != env_name) {
          Ui_ShowStatus("ENV ALERT", env_name, dbfs);
          last_env_ui_name = env_name;
        }
      } else if (st == ALARM_STATE_ENV_ALERT && !env_active) {
        Alarm_SetState(armed ? ALARM_STATE_ARMED : ALARM_STATE_IDLE);
        Ui_ShowStatus(armed ? "ARMED" : "DISARMED", NULL, dbfs);
        last_env_ui_name = NULL;
      }
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
      printf("[HEALTH] stack_free=%luB heap_free=%luB heap_min=%luB "
             "uart_ovf=%u pending=%u\r\n",
             (unsigned long)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t),
             (unsigned long)xPortGetFreeHeapSize(),
             (unsigned long)xPortGetMinimumEverFreeHeapSize(),
             (unsigned)WifiIoT_GetRxOverflow(),
             (unsigned)WifiIoT_GetPendingCount());
    }
  }
}

void vApplicationTickHook(void) {}
void vApplicationMallocFailedHook(void) {
  printf("\r\n[PANIC] FreeRTOS heap exhausted\r\n");
  __disable_irq(); for (;;) {}
}
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask; printf("\r\n[PANIC] %s\r\n", pcTaskName);
  __disable_irq(); for (;;) {}
}

void FreeRTOS_Init(void) {
  DHT11_Init();
  EnvMonitor_Init();
  Mq135Monitor_Init(HAL_GetTick());
  ParticleMonitor_Init();
  Ui_ShowStatus("BOOTING..", NULL, NAN);
  BaseType_t ret;
  ret = xTaskCreate(vAudioInferTask, "AudioInfer", 4096, NULL, 3, &hAudioTask);
  configASSERT(ret == pdPASS);
  printf("[FreeRTOS] 1 task. Starting scheduler...\r\n");
  vTaskStartScheduler(); for (;;) {}
}
