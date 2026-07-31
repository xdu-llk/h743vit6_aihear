# AI Hear — 基于STM32H7的边缘AI语音识别居家安防物联网预警系统

> 当前实现：婴儿哭声实时检测 | 全国大学生嵌入式芯片与设计大赛作品
>
> **⚠️ AI 助手说明：请完整阅读本文档后再动手，项目的架构、引脚、构建命令全在这里。每次修改代码后提醒用户 git commit。**

## 当前进展

| 模块 | 状态 | 备注 |
|------|------|------|
| STM32 固件 | 阶段2.3基础实板回归通过/刺激告警待测 | MQ-135动态基线实板进入normal；GP2Y1010经双层中值后连续约110秒无误报；音频、ESP/MQTT、DHT与环境定向上报同时正常 |
| ESP8266 | 两台均已更新并通过隔离验证 | broker-cn.emqx.io，RST接PA2；精确设备ID匹配、伪造后缀拒绝、定向控制与状态回传均实测通过 |
| Android App | ✅ 完成 | 4Tab，多设备绑定与定向控制，MQTT, SQLite, WakeLock |
| TFT | 驱动已随固件烧录/待屏幕验证 | ST7789 240x240，30MHz SPI4，局部刷新；当前未连接屏幕也不阻塞系统 |
| 训练管线 | 二分类基线 | INT8量化；99.67%为历史验证集结果，不作为独立实测准确率 |
| PCB+外壳 | ✅ 完成 | 共阴RGB LED, 两颗轻触按键 |
| GitHub | ✅ 已推送 | https://github.com/xdu-llk/h743vit6_aihear |

## 待处理

1. TFT到货后只连接TFT，验证方向、颜色、状态变化和温湿度2s刷新；无需重新烧录即可先看首版界面
2. 继续累计测试 STM32/ESP8266 单独复位后的 PA2 自动恢复稳定性
3. 分别对MQ-135和GP2Y1010做一次受控刺激，验证持续异常、红灯/TFT、App告警与恢复；禁止用打火机气体或高浓度烟雾测试MQ
4. MQ-135按用户最终演示选择AO直连PA3，但厂家资料标称高浓度下可到约4V，此接法存在超过H7 ADC 3.3V范围的已知风险
5. 国赛阶段保持二分类，多分类不进入当前关键路径

---

## 系统架构

```
INMP441麦克风 → I2S(PD4/PD0/PD1) → SAI3 DMA双缓冲 → 环形缓冲(16384)
  ↓
96帧×512点FFT → Hann窗 → Mel滤波器组(40×257) → Log能量 → Z-score归一化
  ↓
40×96 float32特征 → TFLite Micro + CMSIS-NN 推理 (DS-CNN, 2分类)
  ↓
prob≥90%立即告警；75%~90%进入真实200ms时序复核 → 本地声光/TFT → ESP8266 MQTT → Android
```

---

## 第一部分：STM32 嵌入式固件

### 文件清单

| 文件 | 作用 | 技术要点 |
|------|------|----------|
| `Core/Src/main.c` | 入口，HAL初始化 | 时钟480MHz (HSE=25MHz, M=5/N=192/P=2)、MPU、IWDG |
| `Core/Src/freertos.c` | 主任务调度 | 幅度门控、概率分级、真实200ms时序复核、MQTT payload带置信度 |
| `Core/Src/audio.c` | I2S DMA音频采集 | DMA双缓冲+环形缓冲+不可变推理快照、24bit I2S解码 |
| `Core/Src/audio_preproc.cc` | 特征提取 | 512ptFFT(CMSIS-DSP)+Mel滤波+Z-score归一化，与Python训练pipeline严格对齐 |
| `Core/Src/tflm_runner.cc` | TFLM推理 | CMSIS-NN加速、512KB tensor arena(AXI SRAM)、动态量化参数 |
| `Core/Src/wifi_iot.c` | ESP8266通信 | UART4 1024字节循环DMA+D-Cache维护+行解析、+PUB协议 |
| `Core/Src/alarm.c` | 告警状态机 | C99查找表驱动5状态(IDLE/ARMED/DETECTING/ALERT/RECOVERY) |
| `Core/Src/mq135.c` / `mq135_monitor.c` | MQ-135采集与状态机 | PA3 ADC1，通道切换后丢弃首样本、16次均值、60秒最低预热+动态稳定基线、EMA、5次确认、回差恢复；只显示相对指数 |
| `Core/Src/gp2y1010.c` / `particle_monitor.c` | 颗粒物采集与状态机 | PC1 ADC1+PD7 ILED，每秒9个厂家时序脉冲取中值，再做5点跨秒中值、10点基线、EMA、5次确认与回差恢复；只显示相对指数 |
| `Core/Src/dscnn_model.cc` | 模型权重 | 102592字节全INT8 TFLite模型，2分类 |

### 内存布局

| 区域 | 地址 | 大小 | 用途 |
|------|------|------|------|
| DTCM | 0x20000000 | 128KB | 栈/BSS/heap |
| AXI SRAM | 0x24000000 | 512KB | `.tensor_arena` (512KB) |
| D2 SRAM | 0x30000000 | 288KB | `.audio_d2`: 音频DMA/环形缓冲/15712采样快照、UART4 RX DMA缓冲 |

### 告警逻辑

```
prob_cry >= 90% → 立即 ALERT，20s冷却
75% <= prob_cry < 90% → 每获得3200个新采样点（约200ms）复核一次，共3窗
复核通过条件 → 任一窗>=90%，或三窗平均>=85%
prob_cry < 75% → NORMAL
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
- **发布语义**: PubSubClient QoS 0；失败时本地重试一次，不宣称Broker确认
- **设备ID**: 每台ESP按MAC后三字节生成唯一ID；当前两台为`aihear_03cb03`和`aihear_876f07`
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
| `MqttService.java` | MQTT后台服务 | TCP Socket MQTT 3.1.1通信、WakeLock、哭声20s/环境5min通知冷却、10min入库去重 |
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
| STM32 | 哭声20秒；环境5分钟 | 告警后抑制重复发送 |
| Android通知 | 哭声20秒；环境5分钟 | 弹窗+震动不重复 |
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
| INMP441 | PD4(WS) PD0(SCK) PD1(SD) | SAI3/I2S, VCC→3.3V, L/R→GND |
| ESP8266 | PA1(RX) PA0(TX) | UART4, 交叉连接 |
| OV2640 | 开发板 CAMERA FPC | PB8/PB9 SCCB + 8-bit DCMI，详见 `docs/hardware_pin_plan.md` |
| DHT11 | PE7(DATA) | single-wire, 3.3V |
| RGB LED | PB4(R) PB5(G) PB3(B) | 共阴模块，板载限流电阻；三路位于同侧相邻排针 |
| 蜂鸣器 | PB0 (TIM3 PWM) | OCPOLARITY_LOW |
| ST7789 | PE12(SCK) PE14(MOSI) PE11(CS) PE15(DC) PD15(BL) | 开发板 DISPLAY FPC，SPI4，3.3V，无RES/MISO |
| MQ-135 | PA3(AO) | ADC12_INP15，DO不接；厂家标称AO最高约4V，直连为用户接受的演示风险，不是安全量产接法 |
| GP2Y1010 | PC1(AO) PD7(ILED) | ADC123_INP11 + LED采样脉冲 |
| 按键 | RST→GND；ESP GPIO0→GND | KEY1硬复位H7；KEY2长按配网 |
| USB-TTL | PA9(TX) PA10(RX) | 调试串口 CH340 |
| ST-Link | SWDIO/SWCLK/GND/3.3V | |

完整载板排母、CAMERA FPC、板载占线和布局限制以 `docs/hardware_pin_plan.md` 为准。

### ESP8266 设备信息

两台设备均为ESP8266EX、26MHz晶振；设备ID由各自MAC后三字节生成，不能在H7或App中写死。

| 设备ID | MAC | 状态 |
|---|---|---|
| `aihear_03cb03` | `2c:3a:e8:03:cb:03` | bridge-v4已烧录，精确ID隔离通过 |
| `aihear_876f07` | `84:f3:eb:87:6f:07` | bridge-v4已烧录，精确ID隔离通过 |

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
└── AGENTS.md                # 本文档
```
