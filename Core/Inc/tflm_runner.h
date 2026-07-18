#ifndef TFLM_RUNNER_H
#define TFLM_RUNNER_H

#include <stdint.h>
#include <stdbool.h>

#define TFLM_INPUT_DIM  (40 * 96)     /* 40 mel x 96 frames */
#define TFLM_BC_CLASSES 2             /* baby_cry, other */
#define TFLM_KWS_CLASSES 2            /* background, help_call */

#ifdef __cplusplus
extern "C" {
#endif

void  TFLM_BabyCry_Init(void);
bool  TFLM_BabyCry_Infer(const float* input, float* output);
void  TFLM_KWS_Init(void);
bool  TFLM_KWS_Infer(const float* input, float* output);
int   TFLM_GetTopClass(const float* output, int n_classes);

#ifdef __cplusplus
}
#endif

#endif
