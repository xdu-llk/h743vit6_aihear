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

  for (uint32_t second = 1; second <= 60; second++) {
    update = Feed(second, 10000);
  }
  assert(update.phase == MQ135_PHASE_WARMUP);
  assert(update.warmup_remaining_ms == 60000U);
  assert(update.ready == 0U);

  for (uint32_t second = 61; second <= 119; second++) {
    update = Feed(second, 10000);
  }
  assert(update.phase == MQ135_PHASE_WARMUP);
  assert(update.ready == 0U);

  /* The 60-second window completes before three minutes. Ten outliers at
     each edge cannot move either the trimmed mean or upper quartile. */
  for (uint32_t second = 120; second <= 179; second++) {
    uint32_t offset = second - 120U;
    uint16_t raw = offset < 10U ? 1000U :
                   (offset >= 50U ? 20000U : 10000U);
    update = Feed(second, raw);
  }
  assert(update.ready == 1U);
  assert(update.baseline == 10000U);
  assert(update.index > 99.9f && update.index < 100.1f);

  uint8_t alerted = 0;
  for (uint32_t second = 180; second < 192; second++) {
    update = Feed(second, 15000);
    alerted |= update.newly_alert;
  }
  assert(alerted == 1U && update.alert == 1U);

  uint8_t recovered = 0;
  for (uint32_t second = 192; second < 204; second++) {
    update = Feed(second, 10000);
    recovered |= update.newly_recovered;
  }
  assert(recovered == 1U && update.alert == 0U);

  Mq135Monitor_Update(false, 0, 205000U);
  Mq135Monitor_Update(false, 0, 206000U);
  update = Mq135Monitor_Update(false, 0, 207000U);
  assert(update.phase == MQ135_PHASE_FAULT);
  assert(update.fault_entered == 1U);

  Mq135Monitor_Update(true, 10000, 208000U);
  Mq135Monitor_Update(true, 10000, 209000U);
  update = Mq135Monitor_Update(true, 10000, 210000U);
  assert(update.fault_recovered == 1U);
  assert(update.phase == MQ135_PHASE_WARMUP);

  /* A rising warm-up curve must not become an immediate false alert. */
  Mq135Monitor_Init(0);
  for (uint32_t second = 1; second < 120; second++) {
    update = Feed(second, 9000);
  }
  for (uint32_t second = 120; second <= 179; second++) {
    update = Feed(second, (uint16_t)(10000U + (second - 120U) * 100U));
  }
  assert(update.ready == 1U);
  assert(update.baseline >= 14500U);
  for (uint32_t second = 180; second < 200; second++) {
    update = Feed(second, 15900U);
    assert(update.alert == 0U);
    assert(update.index < 135.0f);
  }

  alerted = 0;
  for (uint32_t second = 200; second < 215; second++) {
    update = Feed(second, 23000U);
    alerted |= update.newly_alert;
  }
  assert(alerted == 1U && update.alert == 1U);

  puts("mq135_monitor_test: PASS");
  return 0;
}
