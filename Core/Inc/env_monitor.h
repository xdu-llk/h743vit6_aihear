#ifndef ENV_MONITOR_H
#define ENV_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#define ENV_CONFIRM_SAMPLES 3U

typedef enum {
  ENV_CONDITION_TEMP_HIGH = 0,
  ENV_CONDITION_TEMP_LOW,
  ENV_CONDITION_HUMI_HIGH,
  ENV_CONDITION_HUMI_LOW,
  ENV_CONDITION_COUNT
} EnvCondition;

typedef struct {
  uint8_t sample_accepted;
  uint8_t dht_fault;
  uint8_t fault_entered;
  uint8_t fault_recovered;
  uint8_t invalid_streak;
  uint8_t active_mask;
  uint8_t newly_active_mask;
  uint8_t newly_recovered_mask;
} EnvMonitorUpdate;

void EnvMonitor_Init(void);
EnvMonitorUpdate EnvMonitor_UpdateDht(bool sample_valid,
                                      float temperature_c,
                                      float humidity_pct);
uint8_t EnvMonitor_GetActiveMask(void);
uint8_t EnvMonitor_GetConfirmCount(EnvCondition condition);
const char *EnvMonitor_GetConditionName(EnvCondition condition);
const char *EnvMonitor_GetFirstActiveName(void);

#endif
