#include "tflm_runner.h"

/* Model data from training/dscnn_int8.tflite → model_data.cc */
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

/* DS-CNN 2-class: float32 I/O, INT8 internal. Conv2D + DWConv2D + FC + Softmax + Pad + Quantize ops */
using DscnnOpResolver = tflite::MicroMutableOpResolver<8>;

constexpr int kTensorArenaSize = 512 * 1024;

__attribute__((section(".tensor_arena"), aligned(16)))
uint8_t tensor_arena[kTensorArenaSize];

tflite::MicroInterpreter* interpreter = nullptr;

TfLiteStatus RegisterOps(DscnnOpResolver& op_resolver) {
  TF_LITE_ENSURE_STATUS(op_resolver.AddConv2D());
  TF_LITE_ENSURE_STATUS(op_resolver.AddDepthwiseConv2D());
  TF_LITE_ENSURE_STATUS(op_resolver.AddMean());
  TF_LITE_ENSURE_STATUS(op_resolver.AddFullyConnected());
  TF_LITE_ENSURE_STATUS(op_resolver.AddSoftmax());
  TF_LITE_ENSURE_STATUS(op_resolver.AddReshape());
  TF_LITE_ENSURE_STATUS(op_resolver.AddPad());
  return kTfLiteOk;
}

}  /* namespace */

void TFLM_Init(void)
{
  /* Zero tensor arena to prevent SRAM ECC errors on cold boot.
     .tensor_arena is (NOLOAD) in linker — not zeroed by startup. */
  memset(tensor_arena, 0, kTensorArenaSize);

  /* tflite::InitializeTarget() skipped — H7 HAL already handles cache/MPU init */
  MicroPrintf("[TFLM] 1/3 Target ok\n");

  const tflite::Model* model = ::tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("[TFLM] Model version mismatch!\n");
    return;
  }
  MicroPrintf("[TFLM] 2/3 Model ok (%d bytes)\n", g_model_len);

  static DscnnOpResolver op_resolver;
  if (RegisterOps(op_resolver) != kTfLiteOk) {
    MicroPrintf("[TFLM] Failed to register ops!\n");
    return;
  }
  MicroPrintf("[TFLM] 3/3 Ops ok\n");

  static tflite::MicroInterpreter static_interpreter(
      model, op_resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    MicroPrintf("[TFLM] Failed to allocate tensors!\n");
    return;
  }
  MicroPrintf("[TFLM] Arena: %d bytes\n", kTensorArenaSize);
}

bool TFLM_Infer(const float* input, float* output)
{
  if (!interpreter) return false;

  TfLiteTensor* in_tensor = interpreter->input(0);
  TfLiteTensor* out_tensor = interpreter->output(0);

  /* Quantize float features → int8 */
  float input_scale = in_tensor->params.scale;
  int32_t input_zp = in_tensor->params.zero_point;
  for (int i = 0; i < TFLM_INPUT_DIM; i++) {
    int32_t q = static_cast<int32_t>(roundf(input[i] / input_scale)) + input_zp;
    in_tensor->data.int8[i] = static_cast<int8_t>(
        q < -128 ? -128 : (q > 127 ? 127 : q));
  }

  uint32_t t0 = HAL_GetTick();
  if (interpreter->Invoke() != kTfLiteOk) return false;
  uint32_t elapsed = HAL_GetTick() - t0;

  /* Dequantize int8 output → float */
  float output_scale = out_tensor->params.scale;
  int32_t output_zp = out_tensor->params.zero_point;
  for (int i = 0; i < TFLM_NUM_CLASSES; i++) {
    output[i] = (static_cast<int32_t>(out_tensor->data.int8[i]) - output_zp) * output_scale;
  }

  static int infer_cnt = 0;
  if (++infer_cnt % 10 == 1) {
    MicroPrintf("[TFLM] infer %lums", elapsed);
  }

  return true;
}

int TFLM_GetTopClass(const float* output)
{
  int top = 0;
  for (int i = 1; i < TFLM_NUM_CLASSES; i++) {
    if (output[i] > output[top]) top = i;
  }
  return top;
}
