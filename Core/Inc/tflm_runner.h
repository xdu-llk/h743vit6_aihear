#ifndef TFLM_RUNNER_H
#define TFLM_RUNNER_H

#include <stdint.h>
#include <stdbool.h>

#define TFLM_INPUT_DIM  (40 * 96)     /* 40 mel x 96 frames */
#define TFLM_NUM_CLASSES 2            /* baby_cry, other */

#ifdef __cplusplus
extern "C" {
#endif

void  TFLM_Init(void);
bool  TFLM_Infer(const float* input, float* output);
int   TFLM_GetTopClass(const float* output);

#ifdef __cplusplus
}
#endif

#endif
