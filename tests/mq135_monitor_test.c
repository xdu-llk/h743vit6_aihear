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
  assert(update.phase == MQ135_PHASE_WARMUP);
  assert(update.warmup_remaining_ms == 30000U);
  assert(update.ready == 0U);

  for (uint32_t second = 31; second <= 89; second++) {
    update = Feed(second, 10000);
  }
  assert(update.phase == MQ135_PHASE_BASELINE);
  assert(update.ready == 0U);

  /* An unstable 30-second block must reset dynamic baseline progress. */
  for (uint32_t second = 90; second <= 119; second++) {
    update = Feed(second, (second & 1U) ? 8000U : 12000U);
  }
  assert(update.phase == MQ135_PHASE_BASELINE);
  assert(update.ready == 0U);

  /* Stable blocks with a large mean shift are not consecutive stability. */
  for (uint32_t second = 120; second <= 149; second++) {
    update = Feed(second, 12000);
  }
  for (uint32_t second = 150; second <= 179; second++) {
    update = Feed(second, 10000);
  }
  assert(update.phase == MQ135_PHASE_BASELINE);
  assert(update.ready == 0U);

  for (uint32_t second = 180; second <= 209; second++) {
    update = Feed(second, 10000);
  }
  assert(update.ready == 1U);
  assert(update.baseline == 10000U);
  assert(update.index > 99.9f && update.index < 100.1f);

  uint8_t alerted = 0;
  for (uint32_t second = 210; second < 222; second++) {
    update = Feed(second, 15000);
    alerted |= update.newly_alert;
  }
  assert(alerted == 1U && update.alert == 1U);

  uint8_t recovered = 0;
  for (uint32_t second = 222; second < 234; second++) {
    update = Feed(second, 10000);
    recovered |= update.newly_recovered;
  }
  assert(recovered == 1U && update.alert == 0U);

  Mq135Monitor_Update(false, 0, 235000U);
  Mq135Monitor_Update(false, 0, 236000U);
  update = Mq135Monitor_Update(false, 0, 237000U);
  assert(update.phase == MQ135_PHASE_FAULT);
  assert(update.fault_entered == 1U);

  Mq135Monitor_Update(true, 10000, 238000U);
  Mq135Monitor_Update(true, 10000, 239000U);
  update = Mq135Monitor_Update(true, 10000, 240000U);
  assert(update.fault_recovered == 1U);
  assert(update.phase == MQ135_PHASE_WARMUP);

  puts("mq135_monitor_test: PASS");
  return 0;
}
