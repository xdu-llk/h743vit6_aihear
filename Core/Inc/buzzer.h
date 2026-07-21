#ifndef __BUZZER_H__
#define __BUZZER_H__

#include "main.h"
#include <stdbool.h>

void Buzzer_Init(void);
void Buzzer_On(void);
void Buzzer_Off(void);
void Buzzer_Siren(void);

/* Melody engine (non-blocking, call Buzzer_MelodyTick from main loop) */
void Buzzer_PlayMelody(void);
void Buzzer_StopMelody(void);
bool Buzzer_IsPlaying(void);
void Buzzer_MelodyTick(void);

#endif
