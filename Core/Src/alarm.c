#include "alarm.h"
#include "rgb_led.h"
#include "buzzer.h"

/* Pattern: LED color + blink timing + buzzer */
typedef struct {
  RGB_Color color_on;
  uint32_t  led_on_ms;
  uint32_t  led_off_ms;
  uint32_t  buz_on_ms;   /* 0 = buzzer disabled */
  uint32_t  buz_off_ms;
  int       buz_repeat;  /* -1 = infinite */
} Pattern;

static const Pattern PATTERNS[] = {
  [ALARM_STATE_IDLE]      = { { 255, 200,   0 }, 10000,    0,    0,    0,  0 }, /* yellow solid — disarmed */
  [ALARM_STATE_ARMED]     = { {   0, 255,   0 },  500,  500,    0,    0,  0 }, /* green 1Hz — armed */
  [ALARM_STATE_DETECTING] = { {   0, 255, 255 },  125,  125,    0,    0,  0 }, /* cyan 4Hz */
  [ALARM_STATE_ALERT]     = { { 255,   0,   0 },  200,  100,  200,  200,  3 }, /* red + 3 beeps — baby cry */
  [ALARM_STATE_RECOVERY]  = { {   0, 255,   0 },  200, 2000,    0,    0,  0 }, /* green slow */
  [ALARM_STATE_ENV_ALERT] = { { 255,   0,   0 },  200,  100,    0,    0,  0 }, /* red — env alert, no buzzer */
};

static AlarmState state       = ALARM_STATE_IDLE;
static uint32_t   last_toggle;
static uint8_t    led_on;
static uint8_t    buz_on;
static uint8_t    buz_count;

void Alarm_Init(void)
{
  state = ALARM_STATE_IDLE;
  RGB_SetColor(RGB_BLACK);
  Buzzer_Off();
  led_on = 0;
  buz_on = 0;
  buz_count = 0;
  last_toggle = HAL_GetTick();
}

void Alarm_SetState(AlarmState s)
{
  if (s != state) {
    state = s;
    RGB_SetColor(RGB_BLACK);
    Buzzer_Off();
    led_on = 0;
    buz_on = 0;
    buz_count = 0;
    last_toggle = HAL_GetTick();
  }
}

AlarmState Alarm_GetState(void) { return state; }

void Alarm_Process(void)
{
  const Pattern *p = &PATTERNS[state];
  uint32_t now = HAL_GetTick();
  uint32_t elapsed = now - last_toggle;

  /* RGB LED blink */
  if (p->led_on_ms == 0 && p->led_off_ms == 0) {
    if (led_on) { RGB_SetColor(RGB_BLACK); led_on = 0; }
  } else {
    uint32_t interval = led_on ? p->led_on_ms : p->led_off_ms;
    if (interval == 0 || elapsed >= interval) {
      if (led_on) { RGB_SetColor(RGB_BLACK);   led_on = 0; }
      else        { RGB_SetColor(p->color_on); led_on = 1; }
      last_toggle = now;
    }
  }

  /* Buzzer */
  if (p->buz_on_ms == 0) {
    if (buz_on) { Buzzer_Off(); buz_on = 0; }
    return;
  }

  if (p->buz_repeat >= 0 && buz_count >= (uint8_t)(p->buz_repeat * 2))
    return;

  if (elapsed >= (buz_on ? p->buz_on_ms : p->buz_off_ms)) {
    if (buz_on) { Buzzer_Off(); buz_on = 0; buz_count++; }
    else        { Buzzer_On();  buz_on = 1; buz_count++; }
    last_toggle = now;
  }
}
