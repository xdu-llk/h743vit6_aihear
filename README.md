# AI Hear — STM32H743 边缘 AI 哭声与环境监护系统

AI Hear 是一个面向婴幼儿房间的竞赛监护原型：在 STM32H743 上完成婴儿哭声识别和环境状态判断，通过 ESP8266 将告警、环境数据与事件照片传到 Android App。

> 全国大学生嵌入式芯片与设计大赛参赛项目

## 作者与开发方式

本项目由 [xdu-llk](https://github.com/xdu-llk) 独立完成。开发过程中使用 AI 工具辅助代码开发、代码组织和问题排查，项目的方案取舍、系统集成与最终交付由作者负责。

工程使用了 STM32 HAL、FreeRTOS、TensorFlow Lite Micro、CMSIS-DSP、CMSIS-NN 等第三方组件，保留各自的版权声明与许可证。

## 主要功能

- **板端哭声识别**：INMP441 采集音频，H743 运行全 INT8 DS-CNN，识别 `baby_cry / other`，通过概率阈值和连续时间窗复核触发告警。
- **环境监测**：DHT11 监测温湿度，MQ-135 与 GP2Y1014AU0F 监测相对基线变化；环境告警独立于哭声监听。
- **本地反馈与事件抓拍**：TFT 显示状态和环境数据，RGB LED、蜂鸣器反馈哭声告警，OV2640 抓拍并向 App 发送照片。
- **多设备管理**：单个 Android App 绑定和切换多台终端，按设备 ID 隔离消息与控制。
- **记录与趋势**：保存告警、喂养和环境统计，展示历史曲线；可选用 DeepSeek 接口分析汇总数据。

原始音频在 MCU 本地处理。告警、环境数据和事件照片通过 MQTT 传输；App 的 DeepSeek 分析使用远端接口。

## 系统结构

```text
INMP441 → SAI3 / DMA → 音频缓冲 → Log-Mel 特征 → INT8 DS-CNN
                                                    ↓
DHT11 / MQ-135 / GP2Y1014AU0F → 环境状态机 → 告警调度
                                                    ├→ TFT / RGB LED / 蜂鸣器
                                                    ├→ OV2640 事件抓拍
                                                    └→ UART4 → ESP8266 → MQTT → Android
```

STM32 负责采集、推理、状态机和本地外设，ESP8266 负责配网与网络桥接，Android 负责通知、记录和多设备交互。

## 硬件组成

| 模块 | 选型与用途 |
|---|---|
| 主控 | 鹿小班 STM32H743VIT6 V2.0 核心板，HSE 25 MHz，系统时钟 480 MHz |
| 麦克风 | INMP441，SAI3 / I2S，16 kHz 采样 |
| 网络 | ESP8266-01S，Wi-Fi 配网与 MQTT 桥接 |
| 环境传感器 | DHT11、MQ-135、GP2Y1014AU0F + PM2.5 V2.1 适配板 |
| 摄像头 | OV2640，通过开发板 CAMERA FPC 连接 |
| 显示 | ST7789 兼容 240 × 240 屏，通过 DISPLAY FPC / SPI4 连接 |
| 声光与按键 | 共阴 RGB LED、低电平触发无源蜂鸣器、ESP GPIO0 配网按键 |
| 调试 | ST-Link、CH340 / USB-TTL |

完整接线、FPC 方向和供电约束见 [硬件引脚表](docs/hardware_pin_plan.md)。历史原型的 MQ-135 AO 直连存在超过 ADC 3.3 V 范围的风险，复现前需处理该接口。

## 实现与验证记录

下表归纳既有工程记录，硬件验证记录截至 **2026-08-08**。后续版本的实物验证应另行记录日期、条件与结果。

| 部分 | 已记录的验证范围 |
|---|---|
| STM32 整机 | 焊接整机连续 5 次抓拍、TFT 预览、MQTT 照片分片及接收端 JPEG / CRC 校验通过，未出现异常复位 |
| ESP8266 | 两台 bridge-v5 实板联网，设备隔离、UART / MQTT、断线恢复和照片链路通过 |
| Android | APK 构建与签名、多设备控制、通知、照片重组与显示通过 |
| 环境监测 | 三套环境状态机有验证记录；颗粒物模块约 150 秒静置回归未误报警 |
| PCB 与外壳 | 原记录中引脚与 FPC 约束已确定，载板、外壳和长时间整机验证仍列为待完成事项 |

- **识别效果**：`99.67%` 是历史验证集结果，不代表独立设备的现场实测准确率。
- **传感器结果**：MQ-135 和 GP2Y1014AU0F 未进行标准气体或颗粒物标定，只显示相对基线指数。
- **网络边界**：竞赛链路使用明文 MQTT、QoS 0，本地发布成功不等于 Broker 确认收到；历史老化记录中出现过可自行恢复的发布失败。
- **供电**：整机使用开发板 USB-C 5 V 供电；ST-Link 仅用于调试与参考电压连接。

具体测试条件、构建尺寸和运行日志摘要见 [系统设计与历史验证](docs/final_system_overview.md)。

## 文档导航

| 文档 | 内容 |
|---|---|
| [开发与构建说明](docs/development.md) | 源码入口、内存布局、依赖现状、构建步骤和训练流程 |
| [系统设计与历史验证](docs/final_system_overview.md) | 功能链路、告警优先级、演示流程和既有验证记录 |
| [硬件引脚表](docs/hardware_pin_plan.md) | 模块接线、FPC、供电和载板约束 |
| [MQTT 协议](docs/mqtt_protocol.md) | 设备级 topic、控制消息、状态和照片分片格式 |

## 获取代码与构建入口

```bash
git clone https://github.com/xdu-llk/h743vit6_aihear.git
cd h743vit6_aihear
```

**公开仓库的依赖打包尚未补齐。** 当前本机环境包含未纳入 Git 的 FreeRTOS、修改过的 TFLite Micro 和 ruy profiler 适配头文件；仅克隆上游依赖不能完整还原现有工程。依赖版本、缺失内容及工具要求见 [开发与构建说明](docs/development.md#构建环境与依赖现状)。

在补齐对应依赖并配置好工具后，可从仓库根目录使用以下入口：

```bash
# STM32：GNU Make + Arm GNU Toolchain
make -j4

# ESP8266：PlatformIO
pio run --project-dir esp8266_firmware

# Android：先按开发文档配置脚本中的本机路径，再在 Git Bash 中运行
bash apk_build/build_apk.sh
```

STM32 产物位于 `build/`，ESP8266 产物位于 `esp8266_firmware/.pio/build/esp01_1m/`。Android 脚本生成 `apk_build/app-signed.apk`，并复制到其 `DESKTOP` 配置指定的目录。

固件已包含模型数组，普通固件构建无需重新训练。训练和导出步骤见 [模型训练](docs/development.md#模型训练)。

## 代码与目录

```text
h743vit6_aihear/
├── Core/                    # STM32 应用、外设适配与初始化代码
├── Drivers/                 # STM32 HAL、CMSIS 与 CMSIS-DSP
├── esp8266_firmware/        # ESP8266 配网与 MQTT 桥接
├── apk_build/               # Android Java / WebView 源码与构建脚本
├── training/                # 数据预处理、DS-CNN 训练与模型导出
├── tools/                   # MQTT 监测、命令与照片校验工具
├── tests/                   # 环境状态机测试源码
├── scripts/                 # Mel 滤波器生成与报告辅助脚本
├── docs/                    # 工程、硬件、协议和历史验证文档
├── Makefile                 # STM32 固件构建入口
├── STM32H743XX_FLASH.ld     # 链接脚本与专用内存段
└── startup_stm32h743xx.s    # 启动汇编
```

本机的 `third_party/`、`Middlewares/Third_Party/FreeRTOS/` 以及构建产物未纳入当前 Git 跟踪，具体影响见开发文档。

| 关键代码 | 实现内容 |
|---|---|
| [audio.c](Core/Src/audio.c) / [audio_preproc.cc](Core/Src/audio_preproc.cc) | DMA 音频采集、Cache 一致性、推理快照和 Log-Mel 特征 |
| [tflm_runner.cc](Core/Src/tflm_runner.cc) | TFLite Micro、CMSIS-NN 与独立 tensor arena |
| [freertos.c](Core/Src/freertos.c) / [alarm.c](Core/Src/alarm.c) | 任务调度、概率复核、看门狗和告警状态机 |
| [camera.c](Core/Src/camera.c) / [wifi_iot.c](Core/Src/wifi_iot.c) | 有界抓拍、照片分片、通信超时与恢复 |
| [ESP8266 main.cpp](esp8266_firmware/src/main.cpp) | 配网、设备 ID、MQTT 转发和定向控制 |
| [MqttService.java](apk_build/MqttService.java) | Android 前台服务、通知、设备隔离与照片重组 |

## 许可证

本项目自有代码按 [MIT License](LICENSE) 发布。第三方驱动、库和资源保留原有版权声明，使用时需同时遵守各自许可证。
