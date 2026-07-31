#include "env_monitor.h"
#include <string.h>

#define DHT_FAULT_SAMPLES 3U

#define TEMP_HIGH_TRIGGER_C 30.0f
#define TEMP_HIGH_CLEAR_C   29.0f
#define TEMP_LOW_TRIGGER_C  16.0f
#define TEMP_LOW_CLEAR_C    17.0f
#define HUMI_HIGH_TRIGGER   80.0f
#define HUMI_HIGH_CLEAR     75.0f
#define HUMI_LOW_TRIGGER    30.0f
#define HUMI_LOW_CLEAR      35.0f

static uint8_t s_active_mask;
static uint8_t s_condition_streak[ENV_CONDITION_COUNT];
static uint8_t s_invalid_streak;
static uint8_t s_valid_streak;
static uint8_t s_dht_fault;

static uint8_t ConditionBit(EnvCondition condition)
{
  return (uint8_t)(1U << (uint8_t)condition);
}

static bool IsTriggered(EnvCondition condition, float temperature_c,
                        float humidity_pct)
{
  switch (condition) {
    case ENV_CONDITION_TEMP_HIGH: return temperature_c > TEMP_HIGH_TRIGGER_C;
    case ENV_CONDITION_TEMP_LOW:  return temperature_c < TEMP_LOW_TRIGGER_C;
    case ENV_CONDITION_HUMI_HIGH: return humidity_pct > HUMI_HIGH_TRIGGER;
    case ENV_CONDITION_HUMI_LOW:  return humidity_pct < HUMI_LOW_TRIGGER;
    default: return false;
  }
}

static bool IsCleared(EnvCondition condition, float temperature_c,
                      float humidity_pct)
{
  switch (condition) {
    case ENV_CONDITION_TEMP_HIGH: return temperature_c < TEMP_HIGH_CLEAR_C;
    case ENV_CONDITION_TEMP_LOW:  return temperature_c > TEMP_LOW_CLEAR_C;
    case ENV_CONDITION_HUMI_HIGH: return humidity_pct < HUMI_HIGH_CLEAR;
    case ENV_CONDITION_HUMI_LOW:  return humidity_pct > HUMI_LOW_CLEAR;
    default: return false;
  }
}

void EnvMonitor_Init(void)
{
  s_active_mask = 0;
  memset(s_condition_streak, 0, sizeof(s_condition_streak));
  s_invalid_streak = 0;
  s_valid_streak = 0;
  s_dht_fault = 0;
}

EnvMonitorUpdate EnvMonitor_UpdateDht(bool sample_valid,
                                      float temperature_c,
                                      float humidity_pct)
{
  EnvMonitorUpdate update = {0};

  if (!sample_valid) {
    s_valid_streak = 0;
    if (s_invalid_streak < DHT_FAULT_SAMPLES) s_invalid_streak++;
    memset(s_condition_streak, 0, sizeof(s_condition_streak));
    if (!s_dht_fault && s_invalid_streak >= DHT_FAULT_SAMPLES) {
      s_dht_fault = 1;
      update.fault_entered = 1;
    }
    update.dht_fault = s_dht_fault;
    update.invalid_streak = s_invalid_streak;
    update.active_mask = s_active_mask;
    return update;
  }

  s_invalid_streak = 0;
  if (s_dht_fault) {
    if (s_valid_streak < DHT_FAULT_SAMPLES) s_valid_streak++;
    if (s_valid_streak < DHT_FAULT_SAMPLES) {
      update.dht_fault = 1;
      update.active_mask = s_active_mask;
      return update;
    }
    s_dht_fault = 0;
    s_valid_streak = 0;
    update.fault_recovered = 1;
  } else {
    s_valid_streak = 0;
  }

  update.sample_accepted = 1;
  for (EnvCondition condition = ENV_CONDITION_TEMP_HIGH;
       condition < ENV_CONDITION_COUNT; condition++) {
    uint8_t bit = ConditionBit(condition);
    bool active = (s_active_mask & bit) != 0U;
    bool advance = active ? IsCleared(condition, temperature_c, humidity_pct)
                          : IsTriggered(condition, temperature_c, humidity_pct);
    if (advance) {
      if (s_condition_streak[condition] < ENV_CONFIRM_SAMPLES)
        s_condition_streak[condition]++;
    } else {
      s_condition_streak[condition] = 0;
    }

    if (s_condition_streak[condition] < ENV_CONFIRM_SAMPLES) continue;
    s_condition_streak[condition] = 0;
    if (active) {
      s_active_mask &= (uint8_t)~bit;
      update.newly_recovered_mask |= bit;
    } else {
      s_active_mask |= bit;
      update.newly_active_mask |= bit;
    }
  }

  update.dht_fault = s_dht_fault;
  update.active_mask = s_active_mask;
  return update;
}

uint8_t EnvMonitor_GetActiveMask(void)
{
  return s_active_mask;
}

uint8_t EnvMonitor_GetConfirmCount(EnvCondition condition)
{
  if (condition >= ENV_CONDITION_COUNT) return 0;
  return s_condition_streak[condition];
}

const char *EnvMonitor_GetConditionName(EnvCondition condition)
{
  static const char *const names[ENV_CONDITION_COUNT] = {
    "temp_high", "temp_low", "humi_high", "humi_low"
  };
  return condition < ENV_CONDITION_COUNT ? names[condition] : "env_unknown";
}

const char *EnvMonitor_GetFirstActiveName(void)
{
  for (EnvCondition condition = ENV_CONDITION_TEMP_HIGH;
       condition < ENV_CONDITION_COUNT; condition++) {
    if ((s_active_mask & ConditionBit(condition)) != 0U)
      return EnvMonitor_GetConditionName(condition);
  }
  return NULL;
}
