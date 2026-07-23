# AI Hear — 多设备单移动端婴幼儿物联网监护系统

> 婴儿哭声实时检测 + 温湿度环境监护 | STM32H743 + DS-CNN + ESP8266 MQTT + Android
>
> 全国大学生嵌入式芯片与设计大赛作品 · 西北赛区

---

## 功能特性

- 🍼 **婴儿哭声实时检测** — 边缘 AI 推理 (DS-CNN, 99.67% val_acc)，3 级动态投票降误报
- 🌡️ **温湿度环境监护** — 4 阈值告警 (温度 >30/<16°C, 湿度 >80/<30%)，独立冷却
- 🏠 **多房间统一管理** — 单 App 同时监视多台设备，MQTT 设备级 topic 隔离
- 🔊 **TTS 语音播报** — 告警时手机朗读"婴儿房01 检测到婴儿哭声"
- 📊 **数据看板** — Chart.js 双折线图 (7天哭声+喂养 / 24h温湿度)
- 🤖 **AI 分析** — DeepSeek API 综合哭声规律+温湿度+喂养建议
- 🔒 **隐私保护** — 音频在 MCU 本地完成推理，不上传云端

---

## 系统架构

```
INMP441麦克风 → I2S(PE4/5/6) → SAI1 DMA双缓冲 → 环形缓冲(16384)
  ↓
96帧×512点FFT → Hann窗 → Mel滤波器组(40×257) → Log能量 → Z-score归一化
  ↓
40×96 float32特征 → TFLite Micro + CMSIS-NN 推理 (DS-CNN, 2分类)
  ↓
3级动态推理 → 蜂鸣器+RGB LED+OLED → UART→ESP8266→MQTT→Android App (通知+震动+TTS)
  ↓
DHT11 温湿度 @10s → 4阈值判断 → 同上告警链路
```

---

## 硬件需求

| 外设 | 规格 | 数量 | 备注 |
|------|------|:--:|------|
| 主控 | STM32H743VIT6 核心板 (鹿小班 V2.0) | 2 | HSE=25MHz, PLL→480MHz |
| 麦克风 | INMP441 I2S MEMS | 2 | |
| WiFi | ESP8266-01S | 2 | |
| 温湿度 | DHT11 | 1 | 另一块待补焊 |
| 显示 | SSD1306 128×64 OLED I2C | 2 | |
| 指示灯 | RGB LED 共阴 5mm | 2 | 各串 220Ω |
| 蜂鸣器 | 有源蜂鸣器 3.3V | 2 | TIM3 PWM |
| 按键 | 6×6mm 轻触按键 | 2 | 短按布防/撤防, 长按≥3s 复位 |
| 调试器 | ST-Link V2 | 1 | |

完整 BOM 见 [docs/BOM.md](docs/BOM.md)

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
python cache_features.py     # 预处理特征
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
│   ├── MqttService.java     # MQTT 前台 Service + TTS + env 聚合
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
├── docs/                    # 物料清单 + 协议文档 + 竞赛报告
├── Makefile                 # STM32 构建系统 (arm-none-eabi-gcc)
├── STM32H743XX_FLASH.ld     # 自定义链接脚本 (tensor_arena + ring_buf)
├── startup_stm32h743xx.s    # 启动汇编
├── CLAUDE.md                # 项目详细技术文档
└── README.md                # 本文档
```

---

## 核心技术

### STM32 嵌入式

| 模块 | 文件 | 技术要点 |
|------|------|----------|
| 音频采集 | `audio.c` | SAI1 DMA双缓冲、Cache一致性(`SCB_InvalidateDCache`)、24bit I2S→int32 |
| 特征提取 | `audio_preproc.cc` | CMSIS-DSP 512pt FFT + 40×257 Mel 滤波 + per-sample Z-score |
| 模型推理 | `tflm_runner.cc` | TFLite Micro + CMSIS-NN, 512KB tensor arena (AXI SRAM), INT8 动态量化 |
| 任务调度 | `freertos.c` | 单任务 + `vTaskNotifyGiveFromISR` 唤醒, IWDG 8s |
| 告警控制 | `alarm.c` | 6 状态查找表: IDLE/ARMED/DETECTING/ALERT/RECOVERY/ENV_ALERT |
| WiFi通信 | `wifi_iot.c` | UART4 单字节中断+环形缓冲+行解析, `+PUB:topic:payload` 协议 |
| 温湿度 | `dht11.c` | DWT 微秒级 bit-bang, 关中断读 40bit |

### 告警逻辑

**婴儿哭声 — 3 级动态推理：**
```
prob_cry ≥ 90% → 即时 ALERT (蜂鸣器+RGB+OLED+MQTT, 20s 冷却)
60% ≤ prob < 90% → 滑动投票: 0.2s 后补推 2 次, 3 次平均 ≥ 80% → ALERT
prob < 60% → NORMAL (跳过)
```

**温湿度 — 阈值告警（撤防时跳过）：**
```
temp > 30°C → temp_high (RGB 红灯, 蜂鸣器不响, 5min 冷却)
temp < 16°C → temp_low
humi > 80%  → humi_high
humi < 30%  → humi_low
婴儿哭优先级高于温湿度
```

### 模型训练

- 架构：DS-CNN (4 层深度可分离卷积) → GlobalAvgPool → Linear(256→2)
- 输入：40×96 log-Mel spectrogram (SR=16000, n_fft=512, hop=160, n_mels=40)
- 训练技巧：MixUp(α=0.2) + SpecAugment + WeightedRandomSampler + LabelSmoothing(0.1) + AdamW + CosineAnnealingWarmRestarts + GradientClipping(5.0)

### Android App

- 基于 TCP Socket 手写 MQTT 3.1.1 协议栈（零第三方 MQTT 库）
- 前台 Service + WakeLock 息屏保活
- TTS 语音播报（房间名 + 告警类型）
- 4 Tab：告警(首页)、喂养、统计(Chart.js 双折线图)、AI 分析(DeepSeek)
- env_hourly 聚合：每 10s 更新 SQLite，每小时一行 (avg/max/min)
- 去重策略：STM32 20s(哭)/5min(env) / 通知 20s(哭)/5min(env) / 入库 10min

### MQTT 通信架构

| 端 | Broker | Topic |
|------|------|------|
| ESP8266 | broker-cn.emqx.io:1883 | `aihear/v1/demo/{id}/alert`, `/status`, `/env`, `/state` |
| Android | broker-cn.emqx.io:1883 | 订阅所有设备级 topic + `aihear/cmd` (命令下发) |

Payload 格式：
- 婴儿哭告警: `baby_cry:0.95`
- 温湿度告警: `temp_high` (纯 class 名)
- 环境数据: `{"deviceId":"…","temp":28.5,"humi":55,"uptimeMs":…}`
- 控制命令: `{"cmd":"arm","device":"aihear_03cb03"}`

---

## 硬件引脚速查

| 外设 | 引脚 | 备注 |
|------|------|------|
| INMP441 | PE4(WS) PE5(SCK) PE6(SD) | I2S, VCC→3.3V, L/R→GND |
| ESP8266 | PA1(RX) PA0(TX) | UART4, 交叉连接 |
| DHT11 | PD0(DATA) | single-wire, 3.3V |
| OLED | PB10(SCL) PB11(SDA) | I2C2, 400kHz |
| RGB LED | PB4(R) PB5(G) PB8(B) | 各串 220Ω |
| 蜂鸣器 | PB0 (TIM3 PWM) | OCPOLARITY_LOW |
| 按键 | PC4(布防/撤防) | 上拉输入, 短按=布防, 长按≥3s=复位 |
| USB-TTL | PA9(TX) PA10(RX) | 调试串口 CH340 |
| ST-Link | SWDIO/SWCLK/GND/3.3V | |

### ESP8266 设备信息

- Chip: ESP8266EX, Crystal: 26MHz
- Device 1: `aihear_03cb03` (MAC: 2c:3a:e8:03:cb:03)
- Device 2: `aihear_876f07` (MAC: 84:f3:eb:87:6f:07)

---

## 许可证

MIT License
