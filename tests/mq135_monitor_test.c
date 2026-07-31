#include "mq135_monitor.h"

#include <assert.h>
#include <stdio.h>

static Mq135MonitorUpdate Feed(uint32_t second, uint16_t raw)
{
  return Mq135Monitor_Update(true, raw, second * 1000U);
}

int main(void)
{
  Mq135MonitorUpdate update = {0};
  Mq135Monitor_Init(0);

  for (uint32_t second = 1; second <= 30; second++) {
    update = Feed(second, 10000);
  }
  assert(update.phase == MQ135_PHASE_BASELINE);
  assert(update.ready == 0U);

  for (uint32_t second = 31; second <= 59; second++) {
    update = Feed(second, 10000);
  }
  assert(update.ready == 1U);
  assert(update.baseline == 10000U);
  assert(update.index > 99.9f && update.index < 100.1f);

  uint8_t alerted = 0;
  for (uint32_t second = 60; second < 72; second++) {
    update = Feed(second, 15000);
    alerted |= update.newly_alert;
  }
  assert(alerted == 1U && update.alert == 1U);

  uint8_t recovered = 0;
  for (uint32_t second = 72; second < 84; second++) {
    update = Feed(second, 10000);
    recovered |= update.newly_recovered;
  }
  assert(recovered == 1U && update.alert == 0U);

  Mq135Monitor_Update(false, 0, 85000U);
  Mq135Monitor_Update(false, 0, 86000U);
  update = Mq135Monitor_Update(false, 0, 87000U);
  assert(update.phase == MQ135_PHASE_FAULT);
  assert(update.fault_entered == 1U);

  Mq135Monitor_Update(true, 10000, 88000U);
  Mq135Monitor_Update(true, 10000, 89000U);
  update = Mq135Monitor_Update(true, 10000, 90000U);
  assert(update.fault_recovered == 1U);
  assert(update.phase == MQ135_PHASE_WARMUP);

  puts("mq135_monitor_test: PASS");
  return 0;
}
