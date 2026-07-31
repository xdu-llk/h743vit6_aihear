#include "mq135_monitor.h"

#include <string.h>

#define MQ135_WARMUP_MS          30000U
#define MQ135_BASELINE_SAMPLES   30U
#define MQ135_CONFIRM_SAMPLES     5U
#define MQ135_FAULT_SAMPLES       3U
#define MQ135_RAW_VALID_MIN      64U
#define MQ135_RAW_VALID_MAX   65471U
#define MQ135_ALERT_INDEX       135.0f
#define MQ135_CLEAR_INDEX       120.0f
#define MQ135_FILTER_ALPHA        0.4f
#define MQ135_BASELINE_ALPHA      0.001f

static Mq135MonitorUpdate s_state;
static uint32_t s_phase_started_ms;
static uint64_t s_baseline_sum;
static uint16_t s_baseline_count;
static uint8_t s_invalid_streak;
static uint8_t s_valid_streak;
static float s_filtered_raw;
static float s_baseline_raw;

static void StartWarmup(uint32_t now_ms)
{
  memset(&s_state, 0, sizeof(s_state));
  s_state.phase = MQ135_PHASE_WARMUP;
  s_state.warmup_remaining_ms = MQ135_WARMUP_MS;
  s_phase_started_ms = now_ms;
  s_baseline_sum = 0;
  s_baseline_count = 0;
  s_filtered_raw = 0.0f;
  s_baseline_raw = 0.0f;
}

void Mq135Monitor_Init(uint32_t now_ms)
{
  s_invalid_streak = 0;
  s_valid_streak = 0;
  StartWarmup(now_ms);
}

Mq135MonitorUpdate Mq135Monitor_Update(bool sample_valid, uint16_t raw,
                                       uint32_t now_ms)
{
  Mq135MonitorUpdate events = s_state;
  events.newly_alert = 0;
  events.newly_recovered = 0;
  events.fault_entered = 0;
  events.fault_recovered = 0;

  sample_valid = sample_valid && raw > MQ135_RAW_VALID_MIN &&
                 raw < MQ135_RAW_VALID_MAX;
  if (!sample_valid) {
    s_valid_streak = 0;
    if (s_invalid_streak < MQ135_FAULT_SAMPLES) s_invalid_streak++;
    if (s_invalid_streak >= MQ135_FAULT_SAMPLES &&
        s_state.phase != MQ135_PHASE_FAULT) {
      if (s_state.alert) events.newly_recovered = 1;
      s_state.alert = 0;
      s_state.ready = 0;
      s_state.phase = MQ135_PHASE_FAULT;
      s_state.confirm_count = 0;
      events.fault_entered = 1;
    }
    events.phase = s_state.phase;
    events.ready = s_state.ready;
    events.alert = s_state.alert;
    events.confirm_count = s_state.confirm_count;
    return events;
  }

  s_invalid_streak = 0;
  s_state.raw = raw;
  events.raw = raw;

  if (s_state.phase == MQ135_PHASE_FAULT) {
    if (s_valid_streak < MQ135_FAULT_SAMPLES) s_valid_streak++;
    if (s_valid_streak < MQ135_FAULT_SAMPLES) return events;
    s_valid_streak = 0;
    StartWarmup(now_ms);
    s_state.raw = raw;
    events = s_state;
    events.raw = raw;
    events.fault_recovered = 1;
    return events;
  }
  s_valid_streak = 0;

  if (s_state.phase == MQ135_PHASE_WARMUP) {
    uint32_t elapsed = now_ms - s_phase_started_ms;
    if (elapsed < MQ135_WARMUP_MS) {
      s_state.warmup_remaining_ms = MQ135_WARMUP_MS - elapsed;
      events = s_state;
      return events;
    }
    s_state.phase = MQ135_PHASE_BASELINE;
    s_state.warmup_remaining_ms = 0;
    s_baseline_sum = 0;
    s_baseline_count = 0;
  }

  if (s_state.phase == MQ135_PHASE_BASELINE) {
    s_baseline_sum += raw;
    s_baseline_count++;
    if (s_baseline_count < MQ135_BASELINE_SAMPLES) {
      events = s_state;
      return events;
    }
    s_baseline_raw = (float)s_baseline_sum / (float)s_baseline_count;
    s_filtered_raw = s_baseline_raw;
    s_state.baseline = (uint16_t)(s_baseline_raw + 0.5f);
    s_state.index = 100.0f;
    s_state.ready = 1;
    s_state.phase = MQ135_PHASE_READY;
    events = s_state;
    return events;
  }

  s_filtered_raw += MQ135_FILTER_ALPHA * ((float)raw - s_filtered_raw);
  s_state.index = 100.0f * s_filtered_raw / s_baseline_raw;
  s_state.baseline = (uint16_t)(s_baseline_raw + 0.5f);

  bool advancing = s_state.alert ? s_state.index <= MQ135_CLEAR_INDEX
                                 : s_state.index >= MQ135_ALERT_INDEX;
  if (advancing) {
    if (s_state.confirm_count < MQ135_CONFIRM_SAMPLES)
      s_state.confirm_count++;
  } else {
    s_state.confirm_count = 0;
  }

  if (s_state.confirm_count >= MQ135_CONFIRM_SAMPLES) {
    s_state.confirm_count = 0;
    s_state.alert = !s_state.alert;
    if (s_state.alert) events.newly_alert = 1;
    else events.newly_recovered = 1;
  }

  if (!s_state.alert && s_state.index >= 90.0f && s_state.index <= 110.0f) {
    s_baseline_raw += MQ135_BASELINE_ALPHA *
                      (s_filtered_raw - s_baseline_raw);
    s_state.baseline = (uint16_t)(s_baseline_raw + 0.5f);
  }

  events.phase = s_state.phase;
  events.ready = s_state.ready;
  events.alert = s_state.alert;
  events.confirm_count = s_state.confirm_count;
  events.raw = s_state.raw;
  events.baseline = s_state.baseline;
  events.index = s_state.index;
  events.warmup_remaining_ms = s_state.warmup_remaining_ms;
  return events;
}

Mq135MonitorUpdate Mq135Monitor_GetState(void)
{
  return s_state;
}

const char *Mq135Monitor_GetStateName(void)
{
  switch (s_state.phase) {
    case MQ135_PHASE_WARMUP: return "warmup";
    case MQ135_PHASE_BASELINE: return "baseline";
    case MQ135_PHASE_FAULT: return "fault";
    case MQ135_PHASE_READY: return s_state.alert ? "high" : "normal";
    default: return "fault";
  }
}
