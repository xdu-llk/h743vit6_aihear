#include "tflm_runner.h"

extern const unsigned char g_model[];
extern const int g_model_len;

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "main.h"
#include <cmath>
#include <cstring>

namespace {

using Resolver = tflite::MicroMutableOpResolver<8>;

TfLiteStatus RegisterOps(Resolver& r) {
  TF_LITE_ENSURE_STATUS(r.AddConv2D());
  TF_LITE_ENSURE_STATUS(r.AddDepthwiseConv2D());
  TF_LITE_ENSURE_STATUS(r.AddMean());
  TF_LITE_ENSURE_STATUS(r.AddFullyConnected());
  TF_LITE_ENSURE_STATUS(r.AddSoftmax());
  TF_LITE_ENSURE_STATUS(r.AddReshape());
  TF_LITE_ENSURE_STATUS(r.AddPad());
  return kTfLiteOk;
}

constexpr int kArenaSize = 512 * 1024;
__attribute__((section(".tensor_arena"), aligned(16)))
uint8_t arena[kArenaSize];
tflite::MicroInterpreter* interp = nullptr;

}  // namespace

void TFLM_Init(void) {
  memset(arena, 0, kArenaSize);
  const tflite::Model* m = ::tflite::GetModel(g_model);
  static Resolver r; RegisterOps(r);
  __DSB(); __ISB();
  static tflite::MicroInterpreter si(m, r, arena, kArenaSize);
  interp = &si;
  interp->AllocateTensors();
}

bool TFLM_Infer(const float* input, float* output) {
  if (!interp) return false;
  TfLiteTensor* in = interp->input(0);
  TfLiteTensor* out = interp->output(0);
  float is = in->params.scale; int32_t iz = in->params.zero_point;
  for (int i = 0; i < TFLM_INPUT_DIM; i++) {
    int32_t q = (int32_t)roundf(input[i] / is) + iz;
    in->data.int8[i] = (int8_t)(q < -128 ? -128 : (q > 127 ? 127 : q));
  }
  if (interp->Invoke() != kTfLiteOk) return false;
  float os = out->params.scale; int32_t oz = out->params.zero_point;
  for (int i = 0; i < TFLM_NUM_CLASSES; i++)
    output[i] = ((int32_t)out->data.int8[i] - oz) * os;
  return true;
}

int TFLM_GetTopClass(const float* output) {
  int top = 0;
  for (int i = 1; i < TFLM_NUM_CLASSES; i++)
    if (output[i] > output[top]) top = i;
  return top;
}
