#include "particle_monitor.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
  ParticleMonitorUpdate update = {0};
  ParticleMonitor_Init();

  for (int i = 0; i < 13; i++) {
    update = ParticleMonitor_Update(true, 2000);
    assert(update.phase == PARTICLE_PHASE_BASELINE);
    assert(update.ready == 0U);
  }
  update = ParticleMonitor_Update(true, 2000);
  assert(update.phase == PARTICLE_PHASE_READY);
  assert(update.ready == 1U);
  assert(update.baseline == 2000U);
  assert(update.index > 99.9f && update.index < 100.1f);

  /* Isolated ADC spikes must not advance the confirmed alert state. */
  for (int i = 0; i < 15; i++) {
    uint16_t raw = (i % 3 == 0) ? 6000U : 2000U;
    update = ParticleMonitor_Update(true, raw);
    assert(update.alert == 0U);
    assert(update.confirm_count == 0U);
  }

  uint8_t alerted = 0;
  for (int i = 0; i < 10; i++) {
    update = ParticleMonitor_Update(true, 3000);
    alerted |= update.newly_alert;
  }
  assert(alerted == 1U && update.alert == 1U);

  uint8_t recovered = 0;
  for (int i = 0; i < 12; i++) {
    update = ParticleMonitor_Update(true, 2000);
    recovered |= update.newly_recovered;
  }
  assert(recovered == 1U && update.alert == 0U);

  ParticleMonitor_Update(false, 0);
  ParticleMonitor_Update(false, 0);
  update = ParticleMonitor_Update(false, 0);
  assert(update.phase == PARTICLE_PHASE_FAULT);
  assert(update.fault_entered == 1U);

  ParticleMonitor_Update(true, 2000);
  ParticleMonitor_Update(true, 2000);
  update = ParticleMonitor_Update(true, 2000);
  assert(update.fault_recovered == 1U);
  assert(update.phase == PARTICLE_PHASE_BASELINE);

  puts("particle_monitor_test: PASS");
  return 0;
}
