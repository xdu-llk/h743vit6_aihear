# AI Hear 开发与构建说明

本文集中记录源码入口、工程参数和现有构建前提。硬件验证结果归档在 [系统设计与历史验证](final_system_overview.md)，引脚以 [硬件引脚表](hardware_pin_plan.md) 为准。

## 源码入口

### STM32 固件

| 文件 | 作用与实现 |
|---|---|
| [main.c](../Core/Src/main.c) | HAL、480 MHz 时钟、MPU 与 IWDG 初始化 |
| [freertos.c](../Core/Src/freertos.c) | 音频门控、概率分级、200 ms 时序复核与运行健康日志 |
| [audio.c](../Core/Src/audio.c) | SAI3 / I2S DMA 双缓冲、24 bit 音频解码、环形缓冲与不可变推理快照；无采集进度时恢复 DMA |
| [audio_preproc.cc](../Core/Src/audio_preproc.cc) | CMSIS-DSP FFT、Mel 滤波、Log 能量与 Z-score 归一化 |
| [tflm_runner.cc](../Core/Src/tflm_runner.cc) | TFLite Micro、CMSIS-NN、512 KiB tensor arena 与模型量化参数 |
| [dscnn_model.cc](../Core/Src/dscnn_model.cc) | 已导出的二分类 INT8 模型数组，原记录模型大小为 102592 字节 |
| [wifi_iot.c](../Core/Src/wifi_iot.c) | UART4 循环 DMA、D-Cache 维护、行解析、照片分片与有界上传 |
| [camera.c](../Core/Src/camera.c) | OV2640 SCCB、DCMI / DMA、有界 RGB / JPEG 采集和超时恢复 |
| [alarm.c](../Core/Src/alarm.c) | 六状态告警控制，环境告警独立于哭声监听 |
| [mq135_monitor.c](../Core/Src/mq135_monitor.c) | MQ-135 预热、动态基线、EMA、持续异常确认与恢复回差 |
| [particle_monitor.c](../Core/Src/particle_monitor.c) | GP2Y 配对差值、中值滤波、动态基线和持续异常确认 |

### ESP8266 与 Android

ESP8266 入口为 [src/main.cpp](../esp8266_firmware/src/main.cpp)，构建配置为 [platformio.ini](../esp8266_firmware/platformio.ini)。配网热点名称采用 `AIHear_Setup_XXXXXX`，设备 ID 根据 ESP 的 MAC 后三字节生成，按键长按 5 秒可清除 Wi-Fi 配置。设备 ID 应从消息中读取，不能把某一台实物的 ID 写死在 STM32 或 App 中。

Android 使用 Java Activity / 前台 Service 与 WebView：

| 文件 | 职责 |
|---|---|
| [MainActivity.java](../apk_build/MainActivity.java) | Activity、WebView、通知权限与生命周期 |
| [AndroidBridge.java](../apk_build/AndroidBridge.java) | JS 与原生接口、异步 DeepSeek HTTPS 调用 |
| [MqttService.java](../apk_build/MqttService.java) | TCP Socket MQTT 3.1.1、WakeLock、通知、环境聚合及照片重组 |
| [AlertDbHelper.java](../apk_build/AlertDbHelper.java) | SQLite 告警、喂养和小时环境统计 |
| [DeviceRegistry.java](../apk_build/DeviceRegistry.java) | 设备管理 |
| [assets/index.html](../apk_build/assets/index.html) | 监护、喂养、趋势、分析四个页面 |

STM32 与 ESP 使用 115200 baud 的 UART 文本协议。ESP 与 Android 的 Broker 为 `broker-cn.emqx.io:1883`，当前竞赛实现使用明文 MQTT、QoS 0。设备级 topic、命令确认和照片载荷见 [MQTT 协议](mqtt_protocol.md)。

## 音频与模型参数

```text
16 kHz 音频 → 512 点 FFT / Hann 窗 → 40 路 Mel 滤波
            → 96 帧 Log-Mel → Z-score → 全 INT8 DS-CNN
```

训练和固件需保持参数一致：`SR=16000`、`N_FFT=512`、`HOP=160`、`N_MELS=40`、`FRAMES=96`，分类为 `baby_cry / other`。

网络结构为初始卷积与批归一化、四个深度可分离卷积块、全局平均池化和二分类全连接层。训练脚本包含 MixUp、SpecAugment、类别均衡采样、标签平滑、AdamW、余弦学习率调度和梯度裁剪。

### 哭声复核

| 条件 | 处理 |
|---|---|
| 哭声概率 ≥ 90% | 立即告警 |
| 75% ≤ 概率 < 90% | 每获得 3200 个新采样点，约 200 ms，复核一次，共 3 窗 |
| 复核任一窗 ≥ 90%，或三窗平均 ≥ 85% | 告警 |
| 概率 < 75% | 返回正常监听 |

哭声告警冷却为 20 秒。环境告警有独立的持续异常确认与恢复逻辑；Android 对环境异常锁存，在收到恢复事件前避免重复通知。

### 内存布局

| 区域 | 起始地址 | 容量 | 专用分配 |
|---|---|---|---|
| DTCM | `0x20000000` | 128 KiB | 栈、BSS 与 heap |
| AXI SRAM | `0x24000000` | 512 KiB | `.tensor_arena`，512 KiB |
| D2 SRAM | `0x30000000` | 288 KiB | `.audio_d2` 131456 字节、`.camera_rgb` 115200 字节 |
| D3 SRAM | `0x38000000` | 64 KiB | `.camera_jpeg` 61440 字节 |

D3 仅剩约 4 KiB，D2 约剩 47 KiB。增加缓冲区或修改 DMA 数据位置前，应同时核对 [链接脚本](../STM32H743XX_FLASH.ld)、Cache 一致性和外设 DMA 可访问范围。

历史实现的 IWDG 启动期标称约 4 秒，运行期标称约 32 秒；抓拍、TFT 显示和照片上传分阶段喂狗。JPEG 上限为 60 KiB，上传时间上限为 15 秒。照片失败不应撤销已经产生的哭声告警。

## 构建环境与依赖现状

### 工具要求

| 部分 | 工具与配置 |
|---|---|
| STM32 | GNU Make、Arm GNU Toolchain，`arm-none-eabi-gcc/g++/objcopy/size` 加入 PATH；烧录使用 OpenOCD 与 ST-Link |
| ESP8266 | PlatformIO CLI；`esp01_1m`、Arduino 框架、PubSubClient `^2.8`，以实际配置文件为准 |
| Android | Windows + Git Bash、Java、Android SDK；当前脚本使用 Platform 34、Build Tools 34.0.0，以及 Build Tools 37.0.0 的 D8 |
| 模型训练 | Python、PyTorch、NumPy、librosa、soundfile、scikit-learn、tqdm；模型导出额外使用 TensorFlow |

目前未保存完整的工具版本锁定清单。表中的版本来自现有配置，不能视为经过全新环境验证的兼容性范围。

### 本机依赖基准

下表记录 2026-10-17 盘点时的依赖基准与工作区状态。TFLite Micro 的本地适配尚需一并保存，才能恢复原构建环境。

| 依赖 | 基准提交 | 本机状态 |
|---|---|---|
| [TensorFlow Lite Micro](https://github.com/tensorflow/tflite-micro) | `9f5ac257ee7f6a07f1f3c28aa1c86411c05f11e3` | 含本地修改与新增源文件 |
| [CMSIS-NN](https://github.com/ARM-software/CMSIS-NN) | `6d9d61d8a586c39160d0c1ba58f6948e4cf61ad0` | 独立 Git 工作区，未见未提交修改 |
| [FlatBuffers](https://github.com/google/flatbuffers) | `187240970746d00bbd26b0f5873ed54d2477f9f3` | 独立 Git 工作区，未见未提交修改 |
| [gemmlowp](https://github.com/google/gemmlowp) | `fda83bdc38b118cc6b56753bd540caa49e570745` | 独立 Git 工作区，未见未提交修改 |
| FreeRTOS | 本地头文件标识为 Kernel V10.6.2 | 位于 `Middlewares/Third_Party/FreeRTOS/Source/`，未纳入当前仓库跟踪 |
| ruy profiler | 无独立上游提交记录 | 本地仅提供 `ruy/profiler/instrumentation.h` 空实现适配头 |

### 全新克隆的已知缺口

1. `third_party/` 不在 Git 中，新克隆后应先创建该目录。不能通过克隆各上游最新分支来保证还原原开发环境。
2. TFLite Micro 的 CMSIS-NN 卷积实现有本地适配；Makefile 还引用本地新增的 `internal_common.cc` 和 `micro_kernel_util.cc`。仅检出上表基准提交会缺少这些变化。
3. 本机忽略规则 `third_party/` 还匹配到了 `Middlewares/Third_Party/`，使 Makefile 所需的 FreeRTOS 源码未进入 Git。
4. 本机 ruy profiler 空实现头文件同样位于被忽略的目录；它并非完整的上游 ruy 检出。

要形成可独立复现的公开版本，需要补齐 FreeRTOS、导出并纳入 TFLite Micro 适配补丁、保存 ruy 适配头及对应许可证，再记录工具版本并完成全新环境构建。

## STM32 编译与烧录

补齐上述源码与依赖后，在仓库根目录执行：

```bash
make -j4
```

产物为 `build/h743vit6_aihear.elf`、`.hex`、`.bin` 和 `.map`。需要清空构建目录时可单独执行 `make clean`。

连接 ST-Link、确认目标硬件及供电后，使用仓库配置烧录：

```bash
openocd -f openocd_flash.cfg -c "program build/h743vit6_aihear.elf verify reset exit"
```

调试串口为 USART1，PA9 / PA10，115200 baud。ST-Link 不承担整机 5 V 供电，具体连接见硬件引脚表。

## ESP8266 编译与烧录

在仓库根目录执行：

```bash
pio run --project-dir esp8266_firmware
pio run --project-dir esp8266_firmware --target upload
```

烧录时使用独立 USB-TTL 连接，GPIO0 在上电时拉低进入下载模式；烧录后恢复正常启动接线。配网与运行时的 GPIO0 按键用途见硬件引脚表。

## Android APK 构建

当前 [build_apk.sh](../apk_build/build_apk.sh) 面向 Windows + Git Bash，尚含原开发机的固定路径。使用前需按本机安装位置核对：

- `JHOME`、`SDK`：Java 与 Android SDK 位置。
- `BT`、`BT34`、`JAR`：D8、打包工具与 Android Platform 34 的路径。
- `DESKTOP`：APK 最终复制位置。

从仓库根目录运行：

```bash
bash apk_build/build_apk.sh
```

构建顺序为 javac → D8 → aapt → zipalign → apksigner。脚本重建 `apk_build/obj/`、`tmp/`、`res/`，生成 `app-signed.apk` 后复制到指定目录。

当前脚本使用已公开的调试签名材料。该材料应视为公开凭据，不应作为正式发布签名；正式发布应配置并妥善保存本机签名密钥。

## 模型训练

固件构建直接使用 `Core/Src/dscnn_model.cc`，重新训练属于可选流程。原始数据和特征缓存未随仓库提供，训练数据来源、授权和划分应另行记录。

在独立 Python 环境中安装所需包；当前未锁定版本，下列命令只列出脚本使用的主要依赖：

```bash
python -m pip install torch numpy librosa soundfile scikit-learn tqdm tensorflow
cd training
python preprocess_audio.py
python cache_features.py
python train_dscnn.py
python reexport_v3.py
```

流程读取 `training/data/`，预处理输出到 `processed_v2/`，特征缓存写入 `feature_cache_v2/`。训练脚本生成 `best_dscnn.pth`，导出脚本读取该文件并写出固件模型数组。运行导出前应保存现有模型；新模型需要重新验证量化输入输出、板端特征一致性和识别表现。

仓库中的 `best_dscnn_v6_9967.pth` 是已有命名检查点，不能仅凭文件名把 `99.67%` 当作当前脚本重训或现场实测结果。

## 验证工具与记录

| 入口 | 用途 |
|---|---|
| [tests/](../tests/) | 温湿度、MQ-135、颗粒物状态机测试源码 |
| [mqtt_e2e_test.py](../tools/mqtt_e2e_test.py) | MQTT 端到端检查 |
| [mqtt_snapshot_verify.py](../tools/mqtt_snapshot_verify.py) | 照片分片重组与完整性检查 |
| [mqtt_monitor.py](../tools/mqtt_monitor.py) / [mqtt_audit.py](../tools/mqtt_audit.py) | MQTT 监测与审计 |
| [mqtt_command.py](../tools/mqtt_command.py) | 向指定设备发送控制命令 |
| [test_webview_bridge.mjs](../apk_build/test_webview_bridge.mjs) | Android WebView 桥接与 JPEG 页面回归入口 |

MQTT Python 工具使用 `paho-mqtt`；串口采集脚本使用 `pyserial`。这些工具与历史测试记录不能代替当前版本的实物测试。

历史待处理事项包括 MQ-135 AO 电压范围、载板与外壳实物复核，以及更长时间的整机验证。保持 [系统验证记录](final_system_overview.md) 中的日期和条件，只有取得新的实测证据后才更新验证结论。
