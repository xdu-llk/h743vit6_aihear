# AI Hear — 基于STM32H7的边缘AI语音识别居家安防物联网预警系统

> 当前实现：婴儿哭声实时检测 | 全国大学生嵌入式芯片与设计大赛作品
>
> **⚠️ AI 助手说明：请完整阅读本文档后再动手，项目的架构、引脚、构建命令全在这里。每次修改代码后提醒用户 git commit。**

## 当前进展

| 模块 | 状态 | 备注 |
|------|------|------|
| STM32 固件 | 阶段2.5最终软件已烧录并通过基础实板稳定性检查 | OV2640/TFT缺席可快速降级；任务栈最低余量14200B、heap最低余量14384B、UART溢出0；真实画面待模块验证 |
| ESP8266 | bridge-v5构建通过；两台v4实板隔离通过 | 精确设备ID匹配、定向控制与状态回传已实测；两台设备仍需分别烧录v5后验证真实照片定向topic |
| Android App | ✅ 照片版构建与桥接回归通过 | 4Tab、多设备定向控制、JPEG分片重组/CRC校验、照片显示和失败状态；APK v2/v3签名有效 |
| TFT | 驱动已随固件烧录/待屏幕验证 | ST7789 240x240，30MHz SPI4，局部刷新；当前未连接屏幕也不阻塞系统 |
| OV2640 | 驱动与有界抓拍流程完成/待实物验证 | 240x240 RGB565本地显示、320x240 JPEG、60KB上限、15秒上传上限；缺席不影响告警优先发送 |
| 训练管线 | 二分类基线 | INT8量化；99.67%为历史验证集结果，不作为独立实测准确率 |
| PCB+外壳 | 工程布局方案完成 | `output/`含载板和外壳尺寸布局图；Gerber/STL须按实物1:1测量后完成 |
| GitHub | ✅ 已推送 | https://github.com/xdu-llk/h743vit6_aihear |

## 待处理

1. TFT到货后只连接TFT，验证方向、颜色、状态变化和温湿度2s刷新；日志中的`TFT ready`只代表写流程完成，不能证明像素已显示
2. OV2640到货后验证SCCB ID、RGB方向/字节序、补光、JPEG大小与抓拍耗时，再联调TFT显示
3. 两台ESP分别烧录bridge-v5并安装最新APK，完成真实照片的设备隔离、分片上传和App显示验证
4. 全部模块装齐后连续运行30-60分钟，并继续累计STM32/ESP8266单独复位后的PA2自动恢复测试
5. 焊接GP2Y适配板排针后复测长期稳定性；真实烟尘与气体刺激暂不作为当前阻塞项
6. 按实物卡尺尺寸和1:1纸样把`output/`工程图落实为EDA Gerber与外壳STL
7. MQ-135 AO直连PA3存在高浓度时超过H7 ADC 3.3V范围的已知演示风险
8. 国赛阶段保持二分类，多分类不进入当前关键路径

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
| `Core/Src/freertos.c` | 主任务调度 | 幅度门控、概率分级、真实200ms时序复核、30秒栈/heap健康日志 |
| `Core/Src/audio.c` | I2S DMA音频采集 | DMA双缓冲+环形缓冲+不可变推理快照、24bit I2S解码 |
| `Core/Src/audio_preproc.cc` | 特征提取 | 512ptFFT(CMSIS-DSP)+Mel滤波+Z-score归一化，与Python训练pipeline严格对齐 |
| `Core/Src/tflm_runner.cc` | TFLM推理 | CMSIS-NN加速、512KB tensor arena(AXI SRAM)、动态量化参数 |
| `Core/Src/wifi_iot.c` | ESP8266通信 | UART4 1024字节循环DMA+D-Cache维护+行解析、+PUB协议 |
| `Core/Src/camera.c` | OV2640事件抓拍 | 软件SCCB、DCMI+DMA2、有界RGB/JPEG采集、缺席快速降级 |
| `Core/Src/alarm.c` | 告警状态机 | C99查找表驱动6状态，含独立ENV_ALERT无蜂鸣状态 |
| `Core/Src/mq135.c` / `mq135_monitor.c` | MQ-135采集与状态机 | PA3 ADC1，通道切换后丢弃首样本、16次均值、60秒最低预热+动态稳定基线、EMA、5次确认、回差恢复；只显示相对指数 |
| `Core/Src/gp2y1010.c` / `particle_monitor.c` | 颗粒物采集与状态机 | PC1 ADC1+PD7 ILED，每秒9个厂家时序脉冲取中值，再做5点跨秒中值、10点基线、EMA、5次确认与回差恢复；只显示相对指数 |
| `Core/Src/dscnn_model.cc` | 模型权重 | 102592字节全INT8 TFLite模型，2分类 |

### 内存布局

| 区域 | 地址 | 大小 | 用途 |
|------|------|------|------|
| DTCM | 0x20000000 | 128KB | 栈/BSS/heap |
| AXI SRAM | 0x24000000 | 512KB | `.tensor_arena` (512KB) |
| D2 SRAM | 0x30000000 | 288KB | `.audio_d2` 131456B + `.camera_rgb` 115200B，剩余约47KB |
| D3 SRAM | 0x38000000 | 64KB | `.camera_jpeg` 61440B，剩余4KB，禁止再增加大缓冲 |

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
- **告警链路**: STM32通过UART发内部`aihear/alert`消息 → ESP转换并发布设备级`aihear/v1/demo/{id}/alert` → Android通知+震动
- **抓拍链路(v5)**: `captureId|seq|total|crc32|base64` 分片发布到设备级`/snapshot`；QoS 0，本地发布确认不等于Broker确认
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
AlertDbHelper.java (SQLite: alerts + feeding + env_hourly)
```

### 文件清单

| 文件 | 作用 | 技术要点 |
|------|------|----------|
| `MqttService.java` | MQTT后台服务 | TCP Socket MQTT 3.1.1、WakeLock、告警去重、最多60KB JPEG分片重组与CRC32校验 |
| `AlertDbHelper.java` | SQLite持久化 | 告警、喂养、小时环境聚合三表+索引 |
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
| Android通知 | 哭声20秒；环境异常-恢复锁存 | 同一环境异常恢复前只通知一次 |
| Android入库 | 哭声10分钟；环境异常-恢复成对记录 | 抑制重复并记录环境异常持续时间 |

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
| ESP8266 | broker-cn.emqx.io:1883 | 发布设备级 alert/status/env/state/snapshot |
| Android | broker-cn.emqx.io:1883 | 订阅所有设备级topic，向`aihear/cmd`发送定向命令 |

---

## 硬件引脚速查

| 外设 | 引脚 | 备注 |
|------|------|------|
| INMP441 | PD4(WS/H3-18) PD0(SCK/H3-15) PD1(SD/H3-16) | SAI3/I2S, VCC→3.3V, L/R→GND |
| ESP8266 | PA0(H7 TX/H2-33) PA1(H7 RX/H2-34) PA2(RST/H2-31) | PA0→ESP RX，PA1←ESP TX；PA2低电平复位ESP |
| OV2640 | 开发板 CAMERA 24P FPC | PB8/PB9 SCCB + 8-bit DCMI；完整逐脚表见 `docs/hardware_pin_plan.md` |
| DHT11 | PE7(DATA/H2-21) | single-wire, 3.3V |
| RGB LED | PB4(R/H3-23) PB5(G/H3-24) PB3(B/H3-22) | 共阴模块，板载限流电阻；三路相邻 |
| 蜂鸣器 | PB0 (H2-23, TIM3 PWM) | OCPOLARITY_LOW |
| ST7789 | PE12(SCK) PE14(MOSI) PE11(CS) PE15(DC) PD15(BL) | 开发板 DISPLAY 10P FPC，3.3V，无RES/MISO |
| MQ-135 | PA3(AO/H2-32) | ADC12_INP15，DO不接；直连存在超过3.3V风险 |
| GP2Y1010 | PC1(AO/H3-14) PD7(ILED/H3-21) | ADC123_INP11 + LED采样脉冲 |
| 按键 | RST→GND；ESP GPIO0→GND | KEY1硬复位H7；KEY2长按配网 |
| USB-TTL | PA9(TX/H3-10) PA10(RX/H3-9) | 调试串口 CH340 |
| ST-Link | SWDIO/SWCLK/GND/3.3V | |

完整载板排母、CAMERA FPC、板载占线和布局限制以 `docs/hardware_pin_plan.md` 为准。
厂家VIT6原理图中的两侧扩展排针为 `H2/H3`，均为 `2x20P`；旧的`J2/J3`或`2x22`编号不得继续使用。

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

# Android桥接与真实JPEG页面回归（输入必须是<=60KB的完整JPEG）
node apk_build/test_webview_bridge.mjs path/to/photo.jpg build

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
