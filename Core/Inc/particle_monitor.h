#ifndef PARTICLE_MONITOR_H
#define PARTICLE_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  PARTICLE_PHASE_BASELINE = 0,
  PARTICLE_PHASE_READY,
  PARTICLE_PHASE_FAULT
} ParticlePhase;

typedef struct {
  ParticlePhase phase;
  uint8_t ready;
  uint8_t alert;
  uint8_t newly_alert;
  uint8_t newly_recovered;
  uint8_t fault_entered;
  uint8_t fault_recovered;
  uint8_t confirm_count;
  uint16_t raw;
  uint16_t baseline;
  float index;
} ParticleMonitorUpdate;

void ParticleMonitor_Init(void);
ParticleMonitorUpdate ParticleMonitor_Update(bool sample_valid, uint16_t raw);
ParticleMonitorUpdate ParticleMonitor_GetState(void);
const char *ParticleMonitor_GetStateName(void);

#endif
