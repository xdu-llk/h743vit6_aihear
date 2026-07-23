/*
 * buzzer.c — TIM3 CH3 PWM on PB0: siren + lullaby melody (non-blocking)
 * TIM3 counter = 240MHz / (PSC+1) = 4MHz.  ARR = 4e6 / freq - 1.
 * CNT reset on every note change prevents ARR-shrink glitch.
 */
#include "buzzer.h"
#include "tim.h"

typedef struct { uint16_t freq; uint16_t dur_ms; } Note;

static const Note melody[] = {
  /* Two Tigers (两只老虎) — Q=quarter H=half E=eighth */
  {262,350},{294,350},{330,350},{262,350},    /* 两只老虎 QQQQ */
  {262,350},{294,350},{330,350},{262,350},    /* 两只老虎 QQQQ */
  {330,350},{349,350},{392,650},              /* 跑得快 QQH */
  {330,350},{349,350},{392,650},              /* 跑得快 QQH */
  {392,200},{440,200},{392,200},{349,200},    /* 一只没有 EEEE */
  {330,350},{262,350},                        /* 眼睛 QQ */
  {392,200},{440,200},{392,200},{349,200},    /* 一只没有 EEEE */
  {330,350},{262,350},                        /* 尾巴 QQ */
  {262,350},{196,350},{262,350},              /* 真奇怪 QQQ */
  {262,350},{196,350},{262,650},              /* 真奇怪 QQH */
};
#define N_NOTES  (sizeof(melody) / sizeof(Note))
#define GAP_MS   60  /* breath between phrases */
#define TICK_HZ  4000000UL

static int      idx   = -1;
static uint32_t t0    = 0;
static bool     on    = false;
static bool     gap   = false;

static void pwm_out(uint16_t freq)
{
  if (freq == 0) {
    TIM3->CCR3 = 0;
  } else {
    uint16_t arr = (uint16_t)(TICK_HZ / freq - 1);
    TIM3->ARR  = arr;
    TIM3->CNT  = 0;
    TIM3->CCR3 = (uint16_t)(((uint32_t)arr * 1U) / 100U);  /* 1% duty — minimal cross-talk */
  }
}

void Buzzer_Init(void)
{
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
  TIM3->CCR3 = 0;
}

void Buzzer_On(void)  { TIM3->CCR3 = 150; }
void Buzzer_Off(void) { TIM3->CCR3 = 0; }
void Buzzer_Siren(void) { Buzzer_On(); HAL_Delay(600); Buzzer_Off(); }

void Buzzer_PlayMelody(void)
{
  idx   = 0;
  t0    = HAL_GetTick();
  on    = true;
  gap   = false;
  pwm_out(melody[0].freq);
}

void Buzzer_StopMelody(void)
{
  on  = false;
  idx = -1;
  gap = false;
  TIM3->ARR  = 999;
  TIM3->CCR3 = 0;
}

bool Buzzer_IsPlaying(void) { return on; }

void Buzzer_MelodyTick(void)
{
  if (!on || idx < 0) return;

  uint32_t now = HAL_GetTick();

  uint16_t wait_ms = gap ? GAP_MS : melody[idx].dur_ms;
  if ((now - t0) < wait_ms) return;

  if (gap) {
    /* gap done → next note */
    gap = false;
    idx++;
    if (idx >= (int)N_NOTES) { Buzzer_StopMelody(); return; }
    t0 = now;
    pwm_out(melody[idx].freq);
  } else {
    /* note done → silence gap */
    gap = true;
    t0 = now;
    pwm_out(0);
  }
}
