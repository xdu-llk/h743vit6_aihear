#ifndef MQ135_MONITOR_H
#define MQ135_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  MQ135_PHASE_WARMUP = 0,
  MQ135_PHASE_BASELINE,
  MQ135_PHASE_READY,
  MQ135_PHASE_FAULT
} Mq135Phase;

typedef struct {
  Mq135Phase phase;
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
  uint32_t warmup_remaining_ms;
} Mq135MonitorUpdate;

void Mq135Monitor_Init(uint32_t now_ms);
Mq135MonitorUpdate Mq135Monitor_Update(bool sample_valid, uint16_t raw,
                                       uint32_t now_ms);
Mq135MonitorUpdate Mq135Monitor_GetState(void);
const char *Mq135Monitor_GetStateName(void);

#endif
