#include "tflm_runner.h"

extern const unsigned char g_model[];
extern const int g_model_len;
extern const unsigned char g_kws_model[];
extern const int g_kws_model_len;

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "main.h"
#include <cmath>
#include <cstring>

namespace {

using SharedResolver = tflite::MicroMutableOpResolver<8>;

TfLiteStatus RegisterOps(SharedResolver& r) {
  TF_LITE_ENSURE_STATUS(r.AddConv2D());
  TF_LITE_ENSURE_STATUS(r.AddDepthwiseConv2D());
  TF_LITE_ENSURE_STATUS(r.AddMean());
  TF_LITE_ENSURE_STATUS(r.AddFullyConnected());
  TF_LITE_ENSURE_STATUS(r.AddSoftmax());
  TF_LITE_ENSURE_STATUS(r.AddReshape());
  TF_LITE_ENSURE_STATUS(r.AddPad());
  return kTfLiteOk;
}

// Baby-cry: AXI SRAM 512KB
constexpr int kBCArenaSize = 512 * 1024;
__attribute__((section(".tensor_arena"), aligned(16)))
uint8_t bc_arena[kBCArenaSize];
tflite::MicroInterpreter* bc_interp = nullptr;

// KWS: D2 SRAM 80KB (after ring + DMA buffers)
constexpr int kKwsArenaSize = 80 * 1024;
__attribute__((section(".kws_arena"), aligned(16)))
uint8_t kws_arena[kKwsArenaSize];
tflite::MicroInterpreter* kws_interp = nullptr;

}  // namespace

// ── Baby-Cry ──
void TFLM_BabyCry_Init(void) {
  memset(bc_arena, 0, kBCArenaSize);
  const tflite::Model* m = ::tflite::GetModel(g_model);
  static SharedResolver r;
  RegisterOps(r);
  __DSB(); __ISB();
  static tflite::MicroInterpreter si(m, r, bc_arena, kBCArenaSize);
  bc_interp = &si;
  bc_interp->AllocateTensors();
}

bool TFLM_BabyCry_Infer(const float* input, float* output) {
  if (!bc_interp) return false;
  TfLiteTensor* in = bc_interp->input(0);
  TfLiteTensor* out = bc_interp->output(0);
  float is = in->params.scale;
  int32_t iz = in->params.zero_point;
  for (int i = 0; i < TFLM_INPUT_DIM; i++) {
    int32_t q = (int32_t)roundf(input[i] / is) + iz;
    in->data.int8[i] = (int8_t)(q < -128 ? -128 : (q > 127 ? 127 : q));
  }
  if (bc_interp->Invoke() != kTfLiteOk) return false;
  float os = out->params.scale;
  int32_t oz = out->params.zero_point;
  for (int i = 0; i < TFLM_BC_CLASSES; i++)
    output[i] = ((int32_t)out->data.int8[i] - oz) * os;
  return true;
}

// ── KWS ──
void TFLM_KWS_Init(void) {
  memset(kws_arena, 0, kKwsArenaSize);
  const tflite::Model* m = ::tflite::GetModel(g_kws_model);
  static SharedResolver r;
  RegisterOps(r);
  __DSB(); __ISB();
  static tflite::MicroInterpreter si(m, r, kws_arena, kKwsArenaSize);
  kws_interp = &si;
  kws_interp->AllocateTensors();
}

bool TFLM_KWS_Infer(const float* input, float* output) {
  if (!kws_interp) return false;
  TfLiteTensor* in = kws_interp->input(0);
  TfLiteTensor* out = kws_interp->output(0);
  float is = in->params.scale;
  int32_t iz = in->params.zero_point;
  for (int i = 0; i < TFLM_INPUT_DIM; i++) {
    int32_t q = (int32_t)roundf(input[i] / is) + iz;
    in->data.int8[i] = (int8_t)(q < -128 ? -128 : (q > 127 ? 127 : q));
  }
  if (kws_interp->Invoke() != kTfLiteOk) return false;
  float os = out->params.scale;
  int32_t oz = out->params.zero_point;
  for (int i = 0; i < TFLM_KWS_CLASSES; i++)
    output[i] = ((int32_t)out->data.int8[i] - oz) * os;
  return true;
}

int TFLM_GetTopClass(const float* output, int n_classes) {
  int top = 0;
  for (int i = 1; i < n_classes; i++)
    if (output[i] > output[top]) top = i;
  return top;
}
