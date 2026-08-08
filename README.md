# AI Hear — 多设备单移动端婴幼儿物联网监护系统

> 婴儿哭声实时检测 + 温湿度环境监护 | STM32H743 + DS-CNN + ESP8266 MQTT + Android
>
> 全国大学生嵌入式芯片与设计大赛作品 · 西北赛区

---

## 当前状态（2026-08-08）

| 模块 | 状态 | 已验证范围 / 待完成项 |
|---|---|---|
| STM32H743 固件 | 阶段2.4焊接整机通过 | 抓拍看门狗修正版已烧录；连续5次抓拍、TFT预览、MQTT分片与Broker端JPEG/CRC校验通过，无异常复位 |
| ESP8266 | 两台 bridge-v5 实板通过 | 两台均已烧录并联网；设备ID隔离、UART/MQTT、断线恢复和真实照片分片链路均已验证 |
| Android App | 构建与实机链路通过 | APK v2/v3签名、多设备控制、通知、照片分片校验和真实照片显示均已验证 |
| 环境监测 | 实板静置回归通过 | MQ-135约179秒建立动态基线；GP2Y1014AU0F使用开灯前/后配对差值与动态基线，180%阈值连续7秒确认，150秒静置无误报警 |
| PCB 与外壳 | 按实物收口中 | 软件侧引脚与FPC约束已冻结；需按实物尺寸完成载板、外壳及30–60分钟整机测试 |

当前STM32构建产物为`483568B` BIN；ELF为text `483064B`、data `496B`、bss `931576B`。焊接整机抓拍后的健康日志保持`stack_free=14036B`、`heap_min=14384B`、`uart_ovf=0`、`pending=0`。

> `99.67%`仅是历史验证集结果，不代表独立设备现场实测准确率。MQ-135与GP2Y1014AU0F未做标准气体/颗粒物标定，界面只显示相对基线指数，不显示ppm或精确PM2.5浓度。整机必须由开发板USB-C 5V供电，ST-Link不得承担整机5V供电。
>
> 公网Broker使用QoS 0。30分钟老化中出现1次ESP本地发布失败，后续状态自动恢复且没有队列积压；现场演示前必须先确认App显示设备在线，再触发哭声告警。

---

## 功能特性

- 🍼 **婴儿哭声实时检测** — H743板端全INT8 DS-CNN推理，概率分级与真实时序复核
- 🌡️ **环境安全监护** — DHT11固定阈值 + MQ-135/GP2Y1014AU0F动态基线，独立确认与恢复
- 📷 **事件抓拍** — 哭声告警触发OV2640单帧抓拍，TFT本地预览并向App分片上传
- 🏠 **多房间统一管理** — 单 App 同时监视多台设备，MQTT 设备级 topic 隔离
- 📊 **数据看板** — Chart.js 双折线图 (7天哭声+喂养 / 24h温湿度)
- 🤖 **AI 分析** — DeepSeek API 综合哭声规律+温湿度+喂养建议
- 🔒 **隐私保护** — 音频在 MCU 本地完成推理，不上传云端

---

## 系统架构

```
INMP441麦克风 → I2S(PD4/PD0/PD1) → SAI3 DMA双缓冲 → 环形缓冲(16384)
  ↓
96帧×512点FFT → Hann窗 → Mel滤波器组(40×257) → Log能量 → Z-score归一化
  ↓
40×96 float32特征 → TFLite Micro + CMSIS-NN 推理 (DS-CNN, 2分类)
  ↓
概率分级与200ms复核 → 蜂鸣器+RGB LED+TFT → UART→ESP8266→MQTT→Android App (通知+震动)
  ↓
DHT11 温湿度 @2s + GP2Y1014AU0F + MQ-135 → TFT实时刷新+持续异常确认 → 环境告警链路
```

---

## 硬件需求

| 外设 | 规格 | 数量 | 备注 |
|------|------|:--:|------|
| 主控 | STM32H743VIT6 核心板 (鹿小班 V2.0) | 1 | HSE=25MHz, PLL→480MHz |
| 麦克风 | INMP441 I2S MEMS | 1 | SAI3, 16kHz |
| WiFi | ESP8266-01S | 1 | 可拆插座 |
| 环境 | DHT11 + GP2Y1014AU0F + PM2.5 V2.1适配板 + MQ-135模块 | 各1 | 温湿度、颗粒物与相对空气质量趋势 |
| 摄像头 | OV2640 双补光模块 | 1 | CAMERA FPC，事件抓拍 |
| 显示 | 鹿小班1.54英寸 ST7789兼容 240×240 | 1 | 开发板 DISPLAY FPC，SPI4 |
| 指示灯 | RGB LED 共阴模块 | 1 | 模块板载限流电阻 |
| 蜂鸣器 | 低电平触发无源蜂鸣器模块 | 1 | TIM3 PWM非阻塞旋律 |
| 按键 | 6×6mm 轻触按键 | 1 | KEY2长按重新配网；载板不设置H7复位按键 |
| 调试器 | ST-Link V2 | 1 | |

### 文档导航

- [最终系统全貌](docs/final_system_overview.md)：产品功能、告警优先级、演示主线与验证边界
- [最终硬件引脚与载板方案](docs/hardware_pin_plan.md)：唯一 BOM、逐脚连接、FPC 与布局约束
- [MQTT 协议](docs/mqtt_protocol.md)：设备级 topic、命令、状态、告警和照片分片格式
- [开发协作说明](AGENTS.md)：代码架构、内存布局、构建命令和当前待办

---

## 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/xdu-llk/h743vit6_aihear.git
cd h743vit6_aihear
```

### 2. 安装第三方依赖

```bash
cd third_party
git clone https://github.com/tensorflow/tflite-micro.git
git clone https://github.com/ARM-software/CMSIS-NN.git cmsis-nn
git clone https://github.com/google/flatbuffers.git
git clone https://github.com/google/gemmlowp.git
git clone https://github.com/google/ruy.git
cd ..
```

### 3. 编译与烧录

**STM32 固件：**
```bash
make clean && make -j4
openocd -f interface/stlink.cfg -c "transport select swd" \
  -f target/stm32h7x.cfg -c "program build/h743vit6_aihear.elf verify reset exit"
```

**ESP8266 固件：**
```bash
cd esp8266_firmware
pio run -t upload          # 烧录时 GPIO0 接 GND，烧完拔掉
```

**Android APK：**
```bash
bash apk_build/build_apk.sh
# 产物: ~/Desktop/AI_Hear.apk
```

### 4. 训练模型（可选）

```bash
cd training
pip install torch librosa numpy sklearn tqdm
python preprocess_audio.py   # 按原始录音来源生成 processed_v2
python cache_features.py     # 生成 feature_cache_v2
python train_dscnn.py        # 训练 DS-CNN
python reexport_v3.py        # 导出 C 数组 → Core/Src/dscnn_model.cc
```

---

## 项目结构

```
h743vit6_aihear/
├── Core/                    # STM32 原创固件代码
│   ├── Inc/                 # 头文件 (alarm, audio, tflm_runner, wifi_iot, ...)
│   └── Src/                 # 源文件 (main, freertos, audio_preproc, alarm, ...)
├── esp8266_firmware/        # ESP8266 WiFi-MQTT 桥接固件
│   ├── src/main.cpp
│   └── platformio.ini
├── apk_build/               # Android 移动端
│   ├── MqttService.java     # MQTT 前台 Service + 通知/震动 + env 聚合
│   ├── AlertDbHelper.java   # SQLite (alerts + feeding + env_hourly)
│   ├── AndroidBridge.java   # JS↔Native 桥接
│   ├── MainActivity.java    # WebView 容器
│   ├── AndroidManifest.xml
│   ├── build_apk.sh         # 一键构建 (javac→d8→aapt→zipalign→apksigner)
│   └── assets/
│       ├── index.html       # 4Tab PWA 前端
│       └── chart.min.js     # Chart.js 4.4.1
├── training/                # Python 训练管线
│   ├── train_dscnn.py       # DS-CNN 训练 (MixUp+SpecAugment+AdamW)
│   ├── cache_features.py    # 离线特征预提取
│   └── reexport_v3.py       # PyTorch→Keras→TFLite INT8→C 数组
├── tools/                   # 开发验证工具
│   └── mqtt_e2e_test.py     # MQTT 端到端验证脚本
├── docs/                    # 最终硬件表、系统全貌与通信协议
├── Makefile                 # STM32 构建系统 (arm-none-eabi-gcc)
├── STM32H743XX_FLASH.ld     # 自定义链接脚本 (tensor_arena + audio_d2)
├── startup_stm32h743xx.s    # 启动汇编
├── AGENTS.md                # 当前技术事实、验证状态与开发约束
├── CLAUDE.md                # 文档入口（避免重复维护技术参数）
└── README.md                # 本文档
```

---

## 核心技术

### STM32 嵌入式

| 模块 | 文件 | 技术要点 |
|------|------|----------|
| 音频采集 | `audio.c` | SAI3 DMA双缓冲、Cache一致性(`SCB_InvalidateDCache`)、24bit I2S→int32 |
| 特征提取 | `audio_preproc.cc` | CMSIS-DSP 512pt FFT + 40×257 Mel 滤波 + per-sample Z-score |
| 模型推理 | `tflm_runner.cc` | TFLite Micro + CMSIS-NN, 512KB tensor arena (AXI SRAM), INT8 动态量化 |
| 任务调度 | `freertos.c` | 单任务 + `vTaskNotifyGiveFromISR`唤醒；IWDG启动期约4秒标称、运行期约32秒标称，抓拍/显示/上传分阶段喂狗 |
| 告警控制 | `alarm.c` | 6 状态查找表: IDLE/ARMED/DETECTING/ALERT/RECOVERY/ENV_ALERT |
| WiFi通信 | `wifi_iot.c` | UART4 1024B循环DMA+行解析, `+PUB:topic:payload` 协议 |
| 温湿度 | `dht11.c` | DWT 微秒级 bit-bang, 关中断读 40bit |

### 告警逻辑

**婴儿哭声 — 3 级动态推理：**
```
prob_cry ≥ 90% → 即时 ALERT (蜂鸣器+RGB+TFT+MQTT, 20s 冷却)
75% ≤ prob < 90% → 每获得约200ms新音频后补推，共3个真实时间窗
复核通过 → 任一窗≥90%，或三窗平均≥85%
prob < 75% → NORMAL (跳过)
```

**环境 — 持续异常告警（不随哭声监听暂停）：**
```
temp > 30°C → temp_high (RGB 红灯, 蜂鸣器不响, STM32每5min最多重报一次)
temp < 16°C → temp_low
humi > 80%  → humi_high
humi < 30%  → humi_low
MQ-135与GP2Y1014AU0F按动态基线确认持续异常
Android对环境异常锁存，恢复前只弹一次通知
婴儿哭优先级高于环境告警
```

### 模型训练

- 架构：DS-CNN (4 层深度可分离卷积) → GlobalAvgPool → Linear(256→2)
- 输入：40×96 log-Mel spectrogram (SR=16000, n_fft=512, hop=160, n_mels=40)
- 训练技巧：MixUp(α=0.2) + SpecAugment + WeightedRandomSampler + LabelSmoothing(0.1) + AdamW + CosineAnnealingWarmRestarts + GradientClipping(5.0)

### Android App

- 基于 TCP Socket 手写 MQTT 3.1.1 协议栈（零第三方 MQTT 库）
- 前台 Service + WakeLock 息屏保活
- 4 Tab：监护、喂养、趋势（Chart.js 折线图）、分析（DeepSeek）
- 事件抓拍：设备级JPEG分片重组、CRC32/SOI/EOI校验、60KB上限和明确失败状态
- env_hourly 聚合：每 2s 接收环境样本并更新 SQLite，每小时一行 (avg/max/min)
- 去重策略：STM32哭声20s、环境持续异常每5min最多重报；Android哭声通知20s、环境异常-恢复锁存；哭声入库10min去重

### MQTT 通信架构

| 端 | Broker | Topic |
|------|------|------|
| ESP8266 | broker-cn.emqx.io:1883 | `aihear/v1/demo/{id}/alert`, `/status`, `/env`, `/state`, `/snapshot` |
| Android | broker-cn.emqx.io:1883 | 订阅所有设备级 topic + `aihear/cmd` (命令下发) |

Broker Payload 格式：
- 告警: `{"eventId":"...","class":"baby_cry","score":0.95,"uptimeMs":...}`
- 环境告警同样使用JSON，`class`可为`temp_high`、`mq135_high`、`particle_high`等
- 环境数据: `{"deviceId":"…","temp":28.5,"humi":55,"uptimeMs":…}`
- 控制命令: `{"cmd":"arm","device":"aihear_03cb03"}`
- 抓拍分片: `captureId|seq|total|crc32|base64`

---

## 硬件引脚速查

| 外设 | 引脚 | 备注 |
|------|------|------|
| INMP441 | PD4(WS/H3-18) PD0(SCK/H3-15) PD1(SD/H3-16) | SAI3/I2S, VCC→3.3V, L/R→GND |
| ESP8266 | PA0(H7 TX/H2-33) PA1(H7 RX/H2-34) PA2(RST/H2-31) | PA0→ESP RX，PA1←ESP TX；PA2低电平复位ESP |
| DHT11 | PE7(DATA/H2-21) | single-wire, 3.3V |
| OV2640 | 开发板 CAMERA 24P FPC | PB8/PB9 SCCB + 8-bit DCMI，逐脚表见硬件文档 |
| ST7789 | PE12(SCK) PE14(MOSI) PE11(CS) PE15(DC) PD15(BL) | 开发板 DISPLAY 10P FPC，SPI4，3.3V |
| MQ-135 | PA3(AO/H2-32) | ADC12_INP15，DO不接 |
| GP2Y1014AU0F + PM2.5 V2.1适配板 | PC1(AO/H3-14) PD7(ILED/H3-21) | ADC123_INP11 + LED采样脉冲；5V供电 |
| RGB LED | PB4(R/H3-23) PB5(G/H3-24) PB3(B/H3-22) | 共阴模块，三路相邻 |
| 蜂鸣器 | PB0 (H2-23, TIM3 PWM) | OCPOLARITY_LOW |
| KEY2 | ESP GPIO0→GND | 长按5秒重新配网；不接H7 |
| USB-TTL | PA9(TX/H3-10) PA10(RX/H3-9) | 调试串口 CH340 |
| ST-Link | SWDIO/SWCLK/GND/3.3V参考电压 | 仅调试；不得连接ST-Link 5V为整机供电 |

完整载板排母、CAMERA FPC、板载占线和布局限制见 [hardware_pin_plan.md](docs/hardware_pin_plan.md)。
厂家VIT6原理图中的两侧扩展排针为 `H2/H3`，均为 `2x20P`。

### ESP8266 设备信息

- Chip: ESP8266EX, Crystal: 26MHz

| 设备ID | MAC | 当前固件状态 |
|---|---|---|
| `aihear_03cb03` | `2c:3a:e8:03:cb:03` | bridge-v5 已烧录 |
| `aihear_876f07` | `84:f3:eb:87:6f:07` | bridge-v5已烧录、联网，UART/MQTT与照片链路验证通过 |

---

## 许可证

MIT License
