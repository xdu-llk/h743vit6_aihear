# AI Hear — 基于STM32H7的边缘AI语音识别居家安防物联网预警系统

> 当前实现：婴儿哭声实时检测 | 全国大学生嵌入式芯片与设计大赛作品
>
> **⚠️ AI 助手说明：请完整阅读本文档后再动手，项目的架构、引脚、构建命令全在这里。每次修改代码后提醒用户 git commit。**

## 当前进展

| 模块 | 状态 | 备注 |
|------|------|------|
| STM32 固件 | ✅ 完成 | HSE=8MHz, PLL→480MHz, 2分类 DS-CNN |
| ESP8266 | ✅ 完成 | broker-cn.emqx.io, 已知问题: STM32复位后偶发连不上需拔插3.3V |
| Android App | ✅ 完成 | 4Tab, MQTT, SQLite, WakeLock |
| 训练管线 | ✅ 完成 | 99.67% val_acc, INT8量化 |
| PCB+外壳 | ✅ 完成 | 共阴RGB LED, 两颗轻触按键 |
| 比赛报告 | ✅ 完成 | docs/竞赛报告全文.md |
| GitHub | ✅ 已推送 | https://github.com/xdu-llk/h743vit6_aihear |

## 待处理

1. ESP8266 RST悬空 → STM32复位后需拔插3.3V恢复（下一版PCB接GPIO）
2. 串口 WiFi 日志包含 ESP8266 启动信息（噪声，不影响功能）
3. 模型可扩展多分类（玻璃破碎/门撞击等）

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

## 第一部分：STM32 嵌入式固件

### 文件清单

| 文件 | 作用 | 技术要点 |
|------|------|----------|
| `Core/Src/main.c` | 入口，HAL初始化 | 时钟480MHz (HSE=8MHz, M=1/N=60/P=1)、MPU、IWDG |
| `Core/Src/freertos.c` | 主任务调度 | 单任务+vTaskNotifyGiveFromISR唤醒、margin多级阈值、MQTT payload带置信度 |
| `Core/Src/audio.c` | I2S DMA音频采集 | DMA双缓冲+环形缓冲、Cache一致性(SCB_InvalidateDCache)、24bit I2S解码 |
| `Core/Src/audio_preproc.cc` | 特征提取 | 512ptFFT(CMSIS-DSP)+Mel滤波+Z-score归一化，与Python训练pipeline严格对齐 |
| `Core/Src/tflm_runner.cc` | TFLM推理 | CMSIS-NN加速、512KB tensor arena(AXI SRAM)、动态量化参数 |
| `Core/Src/wifi_iot.c` | ESP8266通信 | 单字节UART中断+环形缓冲+行解析、+PUB协议 |
| `Core/Src/alarm.c` | 告警状态机 | C99查找表驱动5状态(IDLE/ARMED/DETECTING/ALERT/RECOVERY) |
| `Core/Src/dscnn_model.cc` | 模型权重 | DS-CNN float32 C数组(275KB) |

### 内存布局

| 区域 | 地址 | 大小 | 用途 |
|------|------|------|------|
| DTCM | 0x20000000 | 128KB | 栈/BSS/heap |
| AXI SRAM | 0x24000000 | 512KB | `.tensor_arena` (512KB) |
| D2 SRAM | 0x30000000 | 288KB | ring[16384]@0x30010000, dmabuf@0x30020000 |

### 告警逻辑

```
margin = |scores[0] - scores[1]|
margin >= 0.8 → ALERT (蜂鸣器+MQTT发布baby_cry:0.95 + 5s冷却)
margin >= 0.5 → MAYBE (仅printf)
margin <  0.5 → WEAK
top == 1     → NORMAL
```

---

## 第二部分：ESP8266 WiFi-MQTT 桥接

| 文件 | 作用 |
|------|------|
| `esp8266_firmware/src/main.cpp` | WiFi-MQTT桥接固件 |
| `esp8266_firmware/platformio.ini` | PlatformIO配置(ESP01_1M) |

### 技术要点
- **双模式**: AP配网(`AIHear_Setup`热点+网页配网) / STA连接+MQTT
- **UART协议**: `+PUB:topic:payload` 文本协议，115200 baud
- **MQTT Broker**: `broker-cn.emqx.io:1883` (与Android/Flask统一)
- **设备ID**: `aihear_03cb03` (MAC: 2c:3a:e8:03:cb:03)
- **告警链路**: STM32检测到哭声 → UART发 `+PUB:aihear/alert:baby_cry:0.95` → ESP8266通过MQTT发布到 `broker-cn.emqx.io` → Android App收到后系统通知+震动
- **GPIO0长按5秒**: 清除WiFi凭据，恢复AP配网模式

---

## 第三部分：Android 移动端 App

### 架构

```
index.html (4Tab PWA前端)
    ↕ @JavascriptInterface
AndroidBridge.java (JS↔Native桥接)
    ↕
MqttService.java (前台Service, TCP Socket MQTT通信, WakeLock保活)
    ↕
AlertDbHelper.java (SQLite: alerts + feeding 双表)
```

### 文件清单

| 文件 | 作用 | 技术要点 |
|------|------|----------|
| `MqttService.java` | MQTT后台服务 | TCP Socket MQTT 3.1.1通信、WakeLock息屏保活、10s通知冷却+10min入库去重 |
| `AlertDbHelper.java` | SQLite持久化 | 双表+时间索引、直接返回JSONArray |
| `AndroidBridge.java` | JS桥接 | WeakReference防泄漏、try-catch全包裹、双向通信 |
| `MainActivity.java` | Activity容器 | 全屏沉浸、通知权限适配、WebView安全配置 |
| `assets/index.html` | 前端UI | 4Tab底部导航、系统通知告警、DeepSeek AI分析、设备绑定 |

### 前端4个Tab

| Tab | 功能 |
|-----|------|
| 🚨 告警(首页) | MQTT状态+消息计数、设备信息、启停按钮、告警记录列表 |
| 🍼 喂养 | 喂养时间+备注记录、历史列表 |
| 📊 统计 | 今日/本周计数、7天柱状图 |
| 🤖 AI分析 | DeepSeek API聚合分析哭声+喂养规律 |

### 去重策略

| 层级 | 冷却 | 作用 |
|------|------|------|
| STM32 | 5秒 | 推理出告警后暂不发MQTT |
| Android通知 | 10秒 | 弹窗+震动不重复 |
| Android入库 | 10分钟 | SQLite同类告警只记一次 |

### 构建

```bash
bash apk_build/build_apk.sh
# javac → d8 → aapt → zipalign → apksigner(v2+v3) → 桌面AI_Hear.apk
```

---

## 第四部分：Python 训练管线

| 文件 | 作用 | 技术要点 |
|------|------|----------|
| `training/train_dscnn.py` | 模型训练 | DS-CNN + MixUp + SpecAugment + AdamW + CosineAnnealing |
| `training/cache_features.py` | 特征预提取 | Mel谱参数与STM32端完全同步(SR=16000, n_fft=512, hop=160, n_mels=40) |
| `training/reexport_v3.py` | 模型导出 | PyTorch→Keras权重移植→TFLite float32→C数组→dscnn_model.cc |

### 模型架构

```
Conv2D(1→64,3×3)+BN+ReLU
→ DSConv(64→64) → DSConv(64→128,s=2)
→ DSConv(128→128) → DSConv(128→256,s=2)
→ GlobalAvgPool → Linear(256→2)
```

### 训练技巧
WeightedRandomSampler + CrossEntropyLoss(weight) + MixUp(α=0.2) + SpecAugment + LabelSmoothing(0.1) + AdamW(wd=1e-4) + CosineAnnealingWarmRestarts + GradientClipping(5.0)

---

## 第五部分：统一通信架构

| 端 | Broker | Topic |
|----|--------|-------|
| ESP8266 | broker-cn.emqx.io:1883 | 发布 aihear/alert, aihear/status |
| Android | broker-cn.emqx.io:1883 | 订阅 aihear/alert |

---

## 硬件引脚速查

| 外设 | 引脚 | 备注 |
|------|------|------|
| INMP441 | PE4(WS) PE5(SCK) PE6(SD) | I2S, VCC→3.3V, L/R→GND |
| ESP8266 | PA1(RX) PA0(TX) | UART4, 交叉连接 |
| OLED | PB10(SCL) PB11(SDA) | hardware I2C2, 400kHz |
| DHT11 | PD0(DATA) | single-wire, 3.3V |
| RGB LED | PB4(R) PB5(G) PB8(B) | 各串220Ω |
| 蜂鸣器 | PB0 (TIM3 PWM) | OCPOLARITY_LOW |
| 按键 | PC4 → GND | 上拉输入 |
| USB-TTL | PA9(TX) PA10(RX) | 调试串口 CH340 |
| ST-Link | SWDIO/SWCLK/GND/3.3V | |

### ESP8266 设备信息
- Chip: ESP8266EX, Crystal: 26MHz
- MAC: 2c:3a:e8:03:cb:03
- Device ID: `aihear_03cb03`

---

## 构建命令速查

```bash
# STM32 编译+烧录
cd d:/stm32cubemx/projects/h743vit6_aihear
make clean && make -j4
openocd -f interface/stlink.cfg -c "transport select swd" \
  -f target/stm32h7x.cfg -c "program build/h743vit6_aihear.elf verify reset exit"

# ESP8266 编译+烧录
cd esp8266_firmware
C:/Users/jeveux/.platformio/penv/Scripts/pio.exe run -t upload

# Android APK 构建
bash apk_build/build_apk.sh

# Python 训练
cd training
D:/Anaconda/envs/aihear/python.exe train_dscnn.py
```

---

## 项目目录结构

```
h743vit6_aihear/
├── Core/                    # STM32 原创代码
│   ├── Inc/  (头文件)
│   └── Src/  (源文件: audio_preproc/tflm_runner/audio/wifi_iot/alarm/freertos/main)
├── esp8266_firmware/        # ESP8266 WiFi-MQTT 桥接
│   ├── src/main.cpp
│   └── platformio.ini
├── apk_build/               # Android 移动端
│   ├── MqttService.java
│   ├── AlertDbHelper.java
│   ├── AndroidBridge.java
│   ├── MainActivity.java
│   ├── AlarmReceiver.java
│   ├── AndroidManifest.xml
│   ├── build_apk.sh
│   └── assets/index.html
├── training/                # Python 训练管线
│   ├── train_dscnn.py
│   ├── cache_features.py
│   └── reexport_v3.py
├── backend/                 # Flask 后端
│   └── server.py
├── Makefile                 # STM32 构建
├── STM32H743XX_FLASH.ld     # 链接脚本(自定义内存段)
├── startup_stm32h743xx.s    # 启动汇编
└── CLAUDE.md                # 本文档
```
