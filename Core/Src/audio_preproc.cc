/*
 * audio_preproc.cc — 40×32 log-mel spectrogram for DS-CNN
 *
 * Matches Python training pipeline EXACTLY:
 *   sr=16000, n_fft=512, hop_len=160, n_mels=40, fmin=20, fmax=8000
 *   Hann window, power_to_db (10*log10), z-score normalization
 */

#include "audio_preproc.h"
#include "mel_bank.h"
#include "dsp_windows.h"

#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include "arm_math.h"
}

/* CMSIS-DSP FFT instance */
static arm_rfft_fast_instance_f32 rfft;

/* FFT working buffers — 512-point */
static float fft_in[PREPROC_FFT_N];       /* 512 real input */
static float fft_out[PREPROC_FFT_N];       /* packed complex */
static float power[MEL_FFT_BINS];          /* 257 power bins */

static float spec_acc[PREPROC_NUM_MELS][PREPROC_NUM_FRAMES];
static float features[PREPROC_NUM_MELS][PREPROC_NUM_FRAMES];
static int   frame_idx = 0;
static bool  ready     = false;

void AudioPreproc_Init(void)
{
  arm_rfft_fast_init_f32(&rfft, PREPROC_FFT_N);
  AudioPreproc_Reset();
}

void AudioPreproc_Reset(void)
{
  frame_idx = 0;
  ready     = false;
  memset(spec_acc, 0, sizeof(spec_acc));
  memset(features, 0, sizeof(features));
}

void AudioPreproc_FeedFrame(const int32_t *pcm_512)
{
  if (ready) return;

  /* 1. Convert PCM to float and apply Hann window.
     * PCM scaling: MCU I2S max ≈ 150000 → divide by 150000 to match training [-1,1] */
  for (int i = 0; i < PREPROC_FFT_N; i++) {
    fft_in[i] = ((float)pcm_512[i] / 150000.0f) * hann_512[i];
  }

  /* 2. Real FFT 512-point (forward) */
  arm_rfft_fast_f32(&rfft, fft_in, fft_out, 0);

  /* 3. Power spectrum (257 bins) — CMSIS-DSP packed format:
        [R0, R1, I1, R2, I2, ..., R{N/2-1}, I{N/2-1}, R{N/2}] */
  /* Suppress very low freq bins (0-94Hz) — INMP441 self-noise dominates these,
     causing Mel band 0 energy to be 4-5σ above training data distribution */
  power[0] = 0.0f;  /* DC */
  for (int k = 1; k <= 2; k++) {
    power[k] = 0.0f;  /* bins 1-2: 31-94 Hz — suppressed (INMP441 self-noise) */
  }
  for (int k = 3; k < PREPROC_FFT_N / 2; k++) {
    float re = fft_out[2 * k - 1];
    float im = fft_out[2 * k];
    power[k] = re * re + im * im;
  }
  power[MEL_FFT_BINS - 1] = fft_out[PREPROC_FFT_N - 1] *
                             fft_out[PREPROC_FFT_N - 1];  /* Nyquist */

  /* 4. Mel filterbank (40×257) × power → 40 energies → 10*log10 */
  for (int m = 0; m < PREPROC_NUM_MELS; m++) {
    float energy = 0.0f;
    for (int k = 0; k < MEL_FFT_BINS; k++) {
      energy += mel_bank[m][k] * power[k];
    }
    if (energy < 1e-6f) energy = 1e-6f;
    spec_acc[m][frame_idx] = 10.0f * log10f(energy);  /* power_to_db */
  }

  frame_idx++;
  if (frame_idx >= PREPROC_NUM_FRAMES) {
    /* 5. Per-sample z-score normalization (matches Python training exactly) */
    float sum = 0.0f, sq = 0.0f;
    const int N = PREPROC_NUM_MELS * PREPROC_NUM_FRAMES;
    for (int m = 0; m < PREPROC_NUM_MELS; m++)
      for (int f = 0; f < PREPROC_NUM_FRAMES; f++) {
        sum += spec_acc[m][f];
        sq += spec_acc[m][f] * spec_acc[m][f];
      }
    float mean = sum / N;
    float std = sqrtf(sq / N - mean * mean) + 1e-6f;
    for (int m = 0; m < PREPROC_NUM_MELS; m++)
      for (int f = 0; f < PREPROC_NUM_FRAMES; f++)
        features[m][f] = (spec_acc[m][f] - mean) / std;

    ready = true;
  }
}

/* Slide window by discarding oldest n_frames from spec_acc.
 * Call after Inference #1 to shift, then feed n_frames new frames for next inference.
 * Example: Shift(20) keeps frames [20..95], sets frame_idx=76, needs 20 more to reach 96. */
void AudioPreproc_Shift(int n_frames)
{
  if (!ready) return;
  if (n_frames <= 0 || n_frames >= PREPROC_NUM_FRAMES) {
    AudioPreproc_Reset();
    return;
  }
  for (int m = 0; m < PREPROC_NUM_MELS; m++) {
    memmove(&spec_acc[m][0], &spec_acc[m][n_frames],
            (PREPROC_NUM_FRAMES - n_frames) * sizeof(float));
  }
  frame_idx = PREPROC_NUM_FRAMES - n_frames;
  ready = false;
}

bool AudioPreproc_IsReady(void)
{
  return ready;
}

const float *AudioPreproc_GetFeatures(void)
{
  return (const float *)features;
}
