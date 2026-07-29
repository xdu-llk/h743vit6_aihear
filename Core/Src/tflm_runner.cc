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
#include <cstdio>
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

bool HasShape(const TfLiteTensor* tensor, const int* expected, int rank) {
  if (!tensor || !tensor->dims || tensor->dims->size != rank) return false;
  for (int i = 0; i < rank; ++i) {
    if (tensor->dims->data[i] != expected[i]) return false;
  }
  return true;
}

}  // namespace

bool TFLM_Init(void) {
  interp = nullptr;
  memset(arena, 0, kArenaSize);
  const tflite::Model* m = ::tflite::GetModel(g_model);
  if (!m || m->version() != TFLITE_SCHEMA_VERSION) {
    printf("[TFLM] Schema mismatch: model=%ld runtime=%d\r\n",
           m ? (long)m->version() : -1L, TFLITE_SCHEMA_VERSION);
    return false;
  }

  static Resolver r;
  if (RegisterOps(r) != kTfLiteOk) {
    printf("[TFLM] Operator registration failed\r\n");
    return false;
  }
  __DSB(); __ISB();
  static tflite::MicroInterpreter si(m, r, arena, kArenaSize);
  if (si.AllocateTensors() != kTfLiteOk) {
    printf("[TFLM] AllocateTensors failed (arena=%d bytes)\r\n", kArenaSize);
    return false;
  }

  TfLiteTensor* in = si.input(0);
  TfLiteTensor* out = si.output(0);
  const int input_shape[] = {1, 40, 96, 1};
  const int output_shape[] = {1, 2};
  if (!in || in->type != kTfLiteInt8 || !HasShape(in, input_shape, 4) ||
      !in->data.int8 || in->params.scale <= 0.0f) {
    printf("[TFLM] Invalid input tensor; expected INT8 [1,40,96,1]\r\n");
    return false;
  }
  if (!out || out->type != kTfLiteInt8 || !HasShape(out, output_shape, 2) ||
      !out->data.int8 || out->params.scale <= 0.0f) {
    printf("[TFLM] Invalid output tensor; expected INT8 [1,2]\r\n");
    return false;
  }

  interp = &si;
  printf("[TFLM] arena used=%lu/%d bytes\r\n",
         (unsigned long)si.arena_used_bytes(), kArenaSize);
  printf("[TFLM] INT8 input scale=%.8f zp=%ld, output scale=%.8f zp=%ld\r\n",
         (double)in->params.scale, (long)in->params.zero_point,
         (double)out->params.scale, (long)out->params.zero_point);
  return true;
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
