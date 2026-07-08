TARGET = h743vit6_aihear
DEBUG = 1
OPT = -O3
BUILD_DIR = build

# CMSIS-NN source files
CMSIS_NN_SRC := $(shell find third_party/cmsis-nn/Source -name '*.c' | sort)

C_SOURCES = $(CMSIS_NN_SRC) \
Core/Src/main.c Core/Src/gpio.c Core/Src/usart.c Core/Src/stm32h7xx_it.c Core/Src/stm32h7xx_hal_msp.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c \
Core/Src/system_stm32h7xx.c Core/Src/sysmem.c Core/Src/syscalls.c Core/Src/led.c Core/Src/buzzer.c Core/Src/alarm.c \
Core/Src/rgb_led.c Core/Src/serial_io.c Core/Src/tim.c Core/Src/dma.c Core/Src/sai.c Core/Src/audio.c Core/Src/wifi_iot.c \
Core/Src/oled.c Core/Src/button.c Core/Src/freertos.c \
Middlewares/Third_Party/FreeRTOS/Source/tasks.c Middlewares/Third_Party/FreeRTOS/Source/queue.c Middlewares/Third_Party/FreeRTOS/Source/list.c \
Middlewares/Third_Party/FreeRTOS/Source/timers.c Middlewares/Third_Party/FreeRTOS/Source/event_groups.c Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c \
Middlewares/Third_Party/FreeRTOS/Source/croutine.c Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1/port.c \
Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c \
Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sai.c Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sai_ex.c \
Drivers/CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_f32.c Drivers/CMSIS/DSP/Source/TransformFunctions/arm_rfft_fast_init_f32.c \
Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_f32.c Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_init_f32.c \
Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix2_f32.c Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix4_f32.c \
Drivers/CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix8_f32.c Drivers/CMSIS/DSP/Source/TransformFunctions/arm_bitreversal.c \
Drivers/CMSIS/DSP/Source/TransformFunctions/arm_bitreversal2.c \
Drivers/CMSIS/DSP/Source/CommonTables/arm_const_structs.c Drivers/CMSIS/DSP/Source/CommonTables/arm_common_tables.c

CPP_SOURCES = \
Core/Src/tflm_runner.cc Core/Src/dscnn_model.cc Core/Src/audio_preproc.cc \
third_party/tflite-micro/tensorflow/lite/micro/micro_op_resolver.cc third_party/tflite-micro/tensorflow/lite/micro/micro_interpreter.cc \
third_party/tflite-micro/tensorflow/lite/micro/micro_allocator.cc third_party/tflite-micro/tensorflow/lite/micro/memory_helpers.cc \
third_party/tflite-micro/tensorflow/lite/micro/micro_interpreter_context.cc third_party/tflite-micro/tensorflow/lite/micro/micro_interpreter_graph.cc \
third_party/tflite-micro/tensorflow/lite/micro/micro_allocation_info.cc \
third_party/tflite-micro/tensorflow/lite/micro/memory_planner/linear_memory_planner.cc \
third_party/tflite-micro/tensorflow/lite/micro/memory_planner/greedy_memory_planner.cc \
third_party/tflite-micro/tensorflow/lite/micro/arena_allocator/single_arena_buffer_allocator.cc \
third_party/tflite-micro/tensorflow/lite/micro/micro_context.cc third_party/tflite-micro/tensorflow/lite/micro/micro_resource_variable.cc \
third_party/tflite-micro/tensorflow/lite/micro/micro_log.cc third_party/tflite-micro/tensorflow/lite/micro/micro_utils.cc \
third_party/tflite-micro/tensorflow/lite/micro/debug_log.cc third_party/tflite-micro/tensorflow/lite/micro/system_setup.cc \
third_party/tflite-micro/tensorflow/lite/micro/flatbuffer_utils.cc \
third_party/tflite-micro/tensorflow/lite/micro/kernels/cmsis_nn/conv.cc third_party/tflite-micro/tensorflow/lite/micro/kernels/cmsis_nn/depthwise_conv.cc \
third_party/tflite-micro/tensorflow/lite/micro/kernels/cmsis_nn/fully_connected.cc \
third_party/tflite-micro/tensorflow/lite/micro/kernels/conv_common.cc third_party/tflite-micro/tensorflow/lite/micro/kernels/depthwise_conv_common.cc \
third_party/tflite-micro/tensorflow/lite/micro/kernels/fully_connected_common.cc \
third_party/tflite-micro/tensorflow/lite/micro/kernels/softmax.cc third_party/tflite-micro/tensorflow/lite/micro/kernels/softmax_common.cc \
third_party/tflite-micro/tensorflow/lite/micro/kernels/reshape.cc third_party/tflite-micro/tensorflow/lite/micro/kernels/reshape_common.cc \
third_party/tflite-micro/tensorflow/lite/micro/kernels/pad.cc third_party/tflite-micro/tensorflow/lite/micro/kernels/pad_common.cc \
third_party/tflite-micro/tensorflow/lite/micro/kernels/reduce.cc third_party/tflite-micro/tensorflow/lite/micro/kernels/reduce_common.cc \
third_party/tflite-micro/tensorflow/lite/core/c/common.cc third_party/tflite-micro/tensorflow/lite/core/api/flatbuffer_conversions.cc \
third_party/tflite-micro/tensorflow/lite/micro/tflite_bridge/flatbuffer_conversions_bridge.cc \
third_party/tflite-micro/tensorflow/lite/micro/tflite_bridge/micro_error_reporter.cc \
third_party/tflite-micro/tensorflow/compiler/mlir/lite/core/api/error_reporter.cc \
third_party/tflite-micro/tensorflow/compiler/mlir/lite/schema/schema_utils.cc \
third_party/tflite-micro/tensorflow/lite/kernels/internal/tensor_ctypes.cc \
third_party/tflite-micro/tensorflow/lite/kernels/internal/internal_common.cc \
third_party/tflite-micro/tensorflow/lite/kernels/internal/quantization_util.cc \
third_party/tflite-micro/tensorflow/lite/kernels/internal/portable_tensor_utils.cc \
third_party/tflite-micro/tensorflow/lite/kernels/kernel_util.cc \
third_party/tflite-micro/tensorflow/lite/micro/kernels/micro_kernel_util.cc

ASM_SOURCES = startup_stm32h743xx.s

PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CXX = $(PREFIX)g++
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S
CPU = -mcpu=cortex-m7
FPU = -mfpu=fpv5-d16
FLOAT = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT)

C_DEFS = -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H743xx -DTF_LITE_STATIC_MEMORY -DCMSIS_NN -DARM_MATH_CM7 -DARM_MATH_DSP
C_INCLUDES = -ICore/Inc -IDrivers/STM32H7xx_HAL_Driver/Inc -IDrivers/STM32H7xx_HAL_Driver/Inc/Legacy \
-IDrivers/CMSIS/Device/ST/STM32H7xx/Include -IDrivers/CMSIS/Include -IDrivers/CMSIS/DSP/Include -IDrivers/CMSIS/DSP/PrivateInclude \
-Ithird_party/tflite-micro -Ithird_party/cmsis-nn/Include -Ithird_party/cmsis-nn \
-Ithird_party/flatbuffers/include -Ithird_party/gemmlowp -Ithird_party/ruy \
-IMiddlewares/Third_Party/FreeRTOS/Source/include -IMiddlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1

CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections -ffast-math -g -gdwarf-2 -MMD -MP -MF"$(@:%.o=%.d)"
CXXFLAGS = $(CFLAGS) -std=c++17 -fno-rtti -fno-exceptions -fno-threadsafe-statics

LDFLAGS = $(MCU) -specs=nano.specs -TSTM32H743XX_FLASH.ld -lc -lm -lnosys \
-Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections -Wl,--no-warn-rwx-segments -u _printf_float

C_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
CPP_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(CPP_SOURCES:.cc=.o)))
ASM_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
OBJECTS = $(C_OBJECTS) $(CPP_OBJECTS) $(ASM_OBJECTS)

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.cc $(sort $(dir $(CPP_SOURCES)))

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR):
	mkdir $@

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.cc Makefile | $(BUILD_DIR)
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(BIN) $< $@

clean:
	-rm -fR $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)
