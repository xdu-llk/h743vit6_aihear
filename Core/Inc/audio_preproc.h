#ifndef __AUDIO_PREPROC_H__
#define __AUDIO_PREPROC_H__

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PREPROC_FFT_N       512   /* matches training n_fft */
#define PREPROC_HOP_LEN     160   /* matches training hop_length */
#define PREPROC_NUM_MELS    40
#define PREPROC_NUM_FRAMES  96   /* ~1s window @ hop=160 */
#define PREPROC_NUM_CHANS   1     /* mel only */
#define MEL_FFT_BINS        (PREPROC_FFT_N / 2 + 1)  /* 257 */

/* Global z-score normalization — MUST match training feature_cache/norm.pkl */
#define PREPROC_GLOBAL_MEAN  -31.191f
#define PREPROC_GLOBAL_STD   16.576f

void         AudioPreproc_Init(void);
void         AudioPreproc_FeedFrame(const int32_t *pcm_512);
bool         AudioPreproc_IsReady(void);
const float* AudioPreproc_GetFeatures(void);
void         AudioPreproc_Reset(void);
void         AudioPreproc_Shift(int n_frames);  /* slide window: discard oldest n frames, keep rest */

#ifdef __cplusplus
}
#endif

#endif
