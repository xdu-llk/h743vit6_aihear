#include "particle_monitor.h"

#include <string.h>

#define PARTICLE_BASELINE_SAMPLES 10U
#define PARTICLE_MEDIAN_SAMPLES    5U
#define PARTICLE_CONFIRM_SAMPLES   7U
#define PARTICLE_FAULT_SAMPLES     3U
#define PARTICLE_RAW_VALID_MIN    16U
#define PARTICLE_RAW_VALID_MAX 65519U
#define PARTICLE_ALERT_INDEX     180.0f
#define PARTICLE_CLEAR_INDEX     130.0f
#define PARTICLE_FILTER_ALPHA      0.35f
#define PARTICLE_BASELINE_ALPHA    0.001f

static ParticleMonitorUpdate s_state;
static uint64_t s_baseline_sum;
static uint16_t s_baseline_count;
static uint8_t s_invalid_streak;
static uint8_t s_valid_streak;
static float s_filtered_raw;
static float s_baseline_raw;
static uint16_t s_raw_history[PARTICLE_MEDIAN_SAMPLES];
static uint8_t s_raw_history_count;
static uint8_t s_raw_history_next;

static void ResetRawHistory(void)
{
  memset(s_raw_history, 0, sizeof(s_raw_history));
  s_raw_history_count = 0;
  s_raw_history_next = 0;
}

static bool PushRawMedian(uint16_t raw, uint16_t *median)
{
  s_raw_history[s_raw_history_next] = raw;
  s_raw_history_next =
      (uint8_t)((s_raw_history_next + 1U) % PARTICLE_MEDIAN_SAMPLES);
  if (s_raw_history_count < PARTICLE_MEDIAN_SAMPLES) s_raw_history_count++;
  if (s_raw_history_count < PARTICLE_MEDIAN_SAMPLES) return false;

  uint16_t sorted[PARTICLE_MEDIAN_SAMPLES];
  memcpy(sorted, s_raw_history, sizeof(sorted));
  for (uint8_t i = 1; i < PARTICLE_MEDIAN_SAMPLES; i++) {
    uint16_t value = sorted[i];
    uint8_t j = i;
    while (j > 0U && sorted[j - 1U] > value) {
      sorted[j] = sorted[j - 1U];
      j--;
    }
    sorted[j] = value;
  }
  *median = sorted[PARTICLE_MEDIAN_SAMPLES / 2U];
  return true;
}

static void StartBaseline(void)
{
  memset(&s_state, 0, sizeof(s_state));
  s_state.phase = PARTICLE_PHASE_BASELINE;
  s_baseline_sum = 0;
  s_baseline_count = 0;
  s_filtered_raw = 0.0f;
  s_baseline_raw = 0.0f;
  ResetRawHistory();
}

void ParticleMonitor_Init(void)
{
  s_invalid_streak = 0;
  s_valid_streak = 0;
  StartBaseline();
}

ParticleMonitorUpdate ParticleMonitor_Update(bool sample_valid, uint16_t raw)
{
  ParticleMonitorUpdate events = s_state;
  events.newly_alert = 0;
  events.newly_recovered = 0;
  events.fault_entered = 0;
  events.fault_recovered = 0;
  s_state.raw = raw;
  events.raw = raw;

  sample_valid = sample_valid && raw > PARTICLE_RAW_VALID_MIN &&
                 raw < PARTICLE_RAW_VALID_MAX;
  if (!sample_valid) {
    s_valid_streak = 0;
    if (s_invalid_streak < PARTICLE_FAULT_SAMPLES) s_invalid_streak++;
    if (s_invalid_streak >= PARTICLE_FAULT_SAMPLES &&
        s_state.phase != PARTICLE_PHASE_FAULT) {
      if (s_state.alert) events.newly_recovered = 1;
      s_state.alert = 0;
      s_state.ready = 0;
      s_state.phase = PARTICLE_PHASE_FAULT;
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
  if (s_state.phase == PARTICLE_PHASE_FAULT) {
    if (s_valid_streak < PARTICLE_FAULT_SAMPLES) s_valid_streak++;
    if (s_valid_streak < PARTICLE_FAULT_SAMPLES) return events;
    s_valid_streak = 0;
    StartBaseline();
    s_state.raw = raw;
    events = s_state;
    events.raw = raw;
    events.fault_recovered = 1;
    return events;
  }
  s_valid_streak = 0;

  uint16_t median_raw;
  if (!PushRawMedian(raw, &median_raw)) {
    events = s_state;
    return events;
  }

  if (s_state.phase == PARTICLE_PHASE_BASELINE) {
    s_baseline_sum += median_raw;
    s_baseline_count++;
    if (s_baseline_count < PARTICLE_BASELINE_SAMPLES) {
      events = s_state;
      return events;
    }
    s_baseline_raw = (float)s_baseline_sum / (float)s_baseline_count;
    s_filtered_raw = s_baseline_raw;
    s_state.baseline = (uint16_t)(s_baseline_raw + 0.5f);
    s_state.index = 100.0f;
    s_state.ready = 1;
    s_state.phase = PARTICLE_PHASE_READY;
    events = s_state;
    return events;
  }

  s_filtered_raw += PARTICLE_FILTER_ALPHA *
                    ((float)median_raw - s_filtered_raw);
  s_state.index = 100.0f * s_filtered_raw / s_baseline_raw;
  s_state.baseline = (uint16_t)(s_baseline_raw + 0.5f);

  bool advancing = s_state.alert ? s_state.index <= PARTICLE_CLEAR_INDEX
                                 : s_state.index >= PARTICLE_ALERT_INDEX;
  if (advancing) {
    if (s_state.confirm_count < PARTICLE_CONFIRM_SAMPLES)
      s_state.confirm_count++;
  } else {
    s_state.confirm_count = 0;
  }

  if (s_state.confirm_count >= PARTICLE_CONFIRM_SAMPLES) {
    s_state.confirm_count = 0;
    s_state.alert = !s_state.alert;
    if (s_state.alert) events.newly_alert = 1;
    else events.newly_recovered = 1;
  }

  if (!s_state.alert && s_state.index >= 90.0f && s_state.index <= 110.0f) {
    s_baseline_raw += PARTICLE_BASELINE_ALPHA *
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
  return events;
}

ParticleMonitorUpdate ParticleMonitor_GetState(void)
{
  return s_state;
}

const char *ParticleMonitor_GetStateName(void)
{
  switch (s_state.phase) {
    case PARTICLE_PHASE_BASELINE: return "baseline";
    case PARTICLE_PHASE_FAULT: return "fault";
    case PARTICLE_PHASE_READY: return s_state.alert ? "high" : "normal";
    default: return "fault";
  }
}
