# AI Hear — 基于STM32H7的边缘AI语音识别居家安防物联网预警系统

> 当前实现：婴儿哭声实时检测 | 可扩展：玻璃破碎、门撞击、爆炸声等多类异常声音识别
>
> STM32H743 + DS-CNN + ESP8266 MQTT + Android 移动监护 | 全国大学生嵌入式芯片与设计大赛作品

---

## 系统架构

```
INMP441麦克风 → I2S(PE4/5/6) → SAI1 DMA双缓冲 → 环形缓冲(16384)
  ↓
96帧×512点FFT → Hann窗 → Mel滤波器组(40×257) → Log能量 → Z-score归一化
  ↓
40×96 float32特征 → TFLite Micro + CMSIS-NN 推理 (DS-CNN, 2分类)
  ↓
margin≥0.8 → 蜂鸣器+RGB LED+OLED → UART→ESP8266→MQTT→Android App (系统通知+震动)
```

---

## 硬件需求

| 外设 | 规格 | 数量 |
|------|------|------|
| 主控 | STM32H743VIT6 核心板 | 1 |
| 麦克风 | INMP441 I2S MEMS | 1 |
| WiFi | ESP8266-01S | 1 |
| 显示 | SSD1306 128×64 OLED I2C | 1 |
| 指示灯 | RGB LED 共阴 5mm | 1 |
| 蜂鸣器 | 有源蜂鸣器 3.3V | 1 |
| 按键 | 6×6mm 轻触按键 | 2 |
| 调试器 | ST-Link V2/V3 | 1 |

完整 BOM 见 [docs/BOM.md](docs/BOM.md) 和 [docs/BOM_AI_Hear.csv](docs/BOM_AI_Hear.csv)

---

## 快速开始

### 1. 克隆仓库

```bash
git clone <repo-url>
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
# 产物在桌面 AI_Hear.apk
```

### 4. 训练模型（可选）

```bash
cd training
pip install torch librosa numpy sklearn tqdm
python cache_features.py     # 预处理特征
python train_dscnn.py        # 训练 DS-CNN
python reexport_v3.py        # 导出为 C 数组
```

---

## 项目结构

```
h743vit6_aihear/
├── Core/                    # STM32 原创固件代码
│   ├── Inc/                 # 头文件
│   └── Src/                 # 源文件
├── esp8266_firmware/        # ESP8266 WiFi-MQTT 桥接
│   ├── src/main.cpp
│   └── platformio.ini
├── apk_build/               # Android 移动端
│   ├── *.java               # MqttService, AlertDbHelper, AndroidBridge 等
│   ├── assets/index.html    # 4Tab 前端界面
│   └── build_apk.sh         # 一键构建脚本
├── training/                # Python 训练管线
│   ├── train_dscnn.py       # DS-CNN 训练
│   ├── cache_features.py    # 特征预提取
│   ├── reexport_v3.py       # 模型导出
│   └── gen_mel_bank.py      # Mel 滤波器组生成
├── scripts/                 # 文档生成工具
├── docs/                    # 物料清单
├── Makefile                 # STM32 构建系统
├── STM32H743XX_FLASH.ld     # 自定义链接脚本
└── startup_stm32h743xx.s    # 启动汇编
```

---

## 核心技术

### STM32 嵌入式

| 模块 | 文件 | 技术要点 |
|------|------|----------|
| 音频采集 | `audio.c` | DMA双缓冲+Cache一致性、24bit I2S解码 |
| 特征提取 | `audio_preproc.cc` | CMSIS-DSP FFT+Mel滤波+Z-score，与训练pipeline严格对齐 |
| 模型推理 | `tflm_runner.cc` | TFLite Micro + CMSIS-NN，512KB tensor arena |
| 任务调度 | `freertos.c` | 单任务架构，vTaskNotifyGiveFromISR 轻量唤醒 |
| 告警控制 | `alarm.c` | 5状态查找表驱动：IDLE→ARMED→DETECTING→ALERT→RECOVERY |
| WiFi通信 | `wifi_iot.c` | UART中断+环形缓冲，+PUB 文本协议 |

### 模型训练

- 架构：DS-CNN (Depthwise Separable CNN)，4 层深度可分离卷积
- 输入：40×96 log-Mel spectrogram
- 训练技巧：MixUp + SpecAugment + WeightedRandomSampler + CosineAnnealing

### Android App

- 基于 TCP Socket 实现 MQTT 3.1.1 通信（零第三方 MQTT 库）
- 前台 Service + WakeLock 息屏保活
- 4 Tab：告警记录、喂养记录、数据统计、DeepSeek AI 分析
- 去重策略：STM32 5s / 通知 10s / 入库 10min

### 通信架构

```
STM32 → UART → ESP8266 → MQTT(broker-cn.emqx.io:1883) → Android App
```

---

## 硬件引脚速查

| 外设 | 引脚 |
|------|------|
| INMP441 | PE4(WS) PE5(SCK) PE6(SD) |
| ESP8266 | PA1(RX) PA0(TX) |
| OLED | PD0(SCL) PD1(SDA) |
| RGB LED | PB4(R) PB5(G) PB8(B) |
| 蜂鸣器 | PB0 (TIM3 PWM) |
| 按键 | PC4(布防) GPIO0(WiFi重置) |
| 调试串口 | PA9(TX) PA10(RX) |
| ST-Link | SWDIO/SWCLK/GND/3.3V |

---

## 许可证

MIT License
