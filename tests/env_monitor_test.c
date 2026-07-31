#include "env_monitor.h"

#include <assert.h>
#include <stdio.h>

static EnvMonitorUpdate Feed(uint8_t valid, float temp_c, float humidity_pct)
{
  return EnvMonitor_UpdateDht(valid != 0U, temp_c, humidity_pct);
}

int main(void)
{
  EnvMonitorUpdate update;

  EnvMonitor_Init();
  update = Feed(1, 31.0f, 50.0f);
  assert(update.active_mask == 0U);
  update = Feed(1, 31.0f, 50.0f);
  assert(update.active_mask == 0U);
  update = Feed(1, 31.0f, 50.0f);
  assert(update.newly_active_mask == (1U << ENV_CONDITION_TEMP_HIGH));

  /* Values inside the hysteresis band must not clear the alert. */
  update = Feed(1, 29.5f, 50.0f);
  assert(update.active_mask == (1U << ENV_CONDITION_TEMP_HIGH));
  Feed(1, 28.0f, 50.0f);
  Feed(1, 28.0f, 50.0f);
  update = Feed(1, 28.0f, 50.0f);
  assert(update.newly_recovered_mask == (1U << ENV_CONDITION_TEMP_HIGH));
  assert(update.active_mask == 0U);

  /* Independent conditions can become active in the same sample. */
  Feed(1, 31.0f, 85.0f);
  Feed(1, 31.0f, 85.0f);
  update = Feed(1, 31.0f, 85.0f);
  assert(update.active_mask == ((1U << ENV_CONDITION_TEMP_HIGH) |
                                (1U << ENV_CONDITION_HUMI_HIGH)));

  EnvMonitor_Init();
  Feed(0, 0.0f, 0.0f);
  Feed(0, 0.0f, 0.0f);
  update = Feed(0, 0.0f, 0.0f);
  assert(update.fault_entered == 1U && update.dht_fault == 1U);
  update = Feed(1, 25.0f, 50.0f);
  assert(update.sample_accepted == 0U && update.dht_fault == 1U);
  update = Feed(1, 25.0f, 50.0f);
  assert(update.sample_accepted == 0U && update.dht_fault == 1U);
  update = Feed(1, 25.0f, 50.0f);
  assert(update.fault_recovered == 1U && update.sample_accepted == 1U);

  puts("env_monitor_test: PASS");
  return 0;
}
