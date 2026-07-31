# AI Hear 国赛版硬件引脚与载板 PCB 方案

> 冻结日期：2026-07-31
>
> 适用主板：鹿小班 STM32H743VIT6 V2.0
>
> 目标：只采购裸 PCB，只焊排母、排针和按键；传感器均使用现成模块。

## 1. 结论

1. OV2640 直接使用 H7 开发板自带的 CAMERA FPC，不在载板上重新布 8 位 DCMI 总线。
2. INMP441 必须从当前 SAI1 的 `PE4/PE5/PE6` 迁移到 SAI3 的 `PD4/PD0/PD1`，否则与摄像头 D4/D6/D7 冲突。
3. 不能改用 `PD11/PD12/PD13` 的 SAI2：这三根线已连接板载 W25Q Flash，会产生实际电气冲突。
4. ST7789 使用开发板 DISPLAY FPC 和 SPI4：`PE11/PE12/PE14/PE15/PD15`，载板不再设置 TFT 排母。
5. DHT11 从 `PD0` 迁移到 `PE7`；`PD0` 留给 SAI3 时钟。
6. MQ-135 只读取 AO，接 `PA3/ADC12_INP15`；DO 不接。
7. GP2Y1010 适配板的 AO 接 `PC1/ADC123_INP11`，ILED 接 `PD7`。
8. 摄像头 SCCB 占用 `PB8/PB9`；RGB 蓝色使用空闲的 `PB3`，与 `PB4/PB5` 位于同侧相邻排针，便于PCB布线。
9. KEY1 连接开发板 V2.0 调试排针上的 `RST` 与 `GND`，实现真正硬件复位；KEY2 连接 ESP-01 `GPIO0` 与 GND。
10. 载板的 5V、3.3V 和 GND 均从 H7 开发板排针取得，USB-C 仍是唯一电源入口。

## 2. 当前烧录版本与最终版

### 2.1 当前过渡测试固件

2026-07-31 已烧录的阶段1固件用于回归原有功能。此时 TFT、OV2640、MQ-135 和 GP2Y1010 尚未加入；RGB蓝灯已经迁移，不能再接回PB8。

阶段2.1固件已经加入 ST7789 与DHT环境状态机，完成全量编译、烧录和无屏回归。TFT未连接时，H7启动、音频推理、DHT每2秒云端上报以及MQTT双向控制均正常。实测环境报文间隔约2.01秒，设备ID、有效标记与状态字段完整。屏幕到货后的板测只连接 DISPLAY FPC 的 TFT；新增传感器按MQ-135、GP2Y1010的顺序接入，OV2640保持最后。

| 模块 | 当前接线 | 说明 |
|---|---|---|
| INMP441 | SCK→PD0，SD→PD1，WS→PD4，L/R→GND | 已迁移到 SAI3 |
| DHT11 | DATA→PE7 | 已迁移，约 2 秒读取一次 |
| OLED | 不连接 | I2C2/OLED不再初始化；PB10/PB11保持空闲 |
| RGB | R→PB4，G→PB5，B→PB3，共阴→GND | 三路仅作GPIO开关，不使用软件PWM |
| 蜂鸣器模块 | I/O→PB0 | 当前告警旋律保持不变 |
| 临时软件按键 | 不连接 | PC4不再初始化，保留给OV2640 RESET；KEY1最终接硬件RST |
| ESP-01 | RX→PA0，TX→PA1，RST→PA2 | UART4，现有链路不变 |
| CH340 | RXD→PA9，TXD→PA10，GND→GND | USART1，115200 8N1；VCC/3.3V/5V 不接 |

TFT首轮板测不连接 OV2640。DHT11 保持每2秒读取并驱动TFT实时指示；高温、低温、高湿、低湿均连续3次越限后确认，恢复使用回差并连续3次确认。传感器连续3次读取失败才进入故障，连续3次有效读取后退出故障。

### 2.2 最终 PCB 引脚分配

| 模块 | 模块信号 | H7 引脚 | 复用功能 | 开发板排针 | 备注 |
|---|---|---:|---|---|---|
| ESP-01 | RX | PA0 | UART4_TX, AF8 | J3-34 | 交叉连接 |
| ESP-01 | TX | PA1 | UART4_RX, AF8 | J3-33 | 板载 LED 对该线有负载，现有实测链路保留 |
| ESP-01 | RST | PA2 | GPIO output | J3-32 | 低电平硬复位 ESP |
| MQ-135 | AO | PA3 | ADC12_INP15 | J3-31 | DO 不接；输入必须不高于 3.3V |
| 调试串口 | RXD | PA9 | USART1_TX, AF7 | V2.0 调试排针 TXD | 接 CH340 RXD |
| 调试串口 | TXD | PA10 | USART1_RX, AF7 | V2.0 调试排针 RXD | 接 CH340 TXD |
| 蜂鸣器模块 | I/O | PB0 | TIM3_CH3, AF2 | J3-24 | 低电平有效，无源模块 |
| RGB | R | PB4 | GPIO output | J2-38 | 共阴模块，自带限流电阻 |
| RGB | G | PB5 | GPIO output | J2-37 | 共阴模块，自带限流电阻 |
| RGB | B | PB3 | GPIO output | 上侧排针，与PB4/PB5相邻 | 避开摄像头和板载QSPI占线 |
| GP2Y1010 | AO | PC1 | ADC123_INP11 | J3-37 | 使用适配板的 AO |
| INMP441 | SCK | PD0 | SAI3_SCK_A, AF6 | J2-27 | 从 PE5 迁移 |
| INMP441 | SD | PD1 | SAI3_SD_A, AF6 | J2-30 | 从 PE6 迁移 |
| INMP441 | WS | PD4 | SAI3_FS_A, AF6 | J2-31 | 从 PE4 迁移 |
| GP2Y1010 | ILED | PD7 | GPIO output | J2-36 | 按传感器时序产生采样脉冲 |
| ST7789 | BL | PD15 | GPIO output | DISPLAY FPC | 背光控制；屏幕没有独立 RES 引脚 |
| DHT11 | DATA | PE7 | GPIO open-drain/input | J3-21 | 从 PD0 迁移 |
| ST7789 | CS | PE11 | GPIO output | DISPLAY FPC | 当前固件使用软件片选，物理引脚与厂家例程一致 |
| ST7789 | SCK | PE12 | SPI4_SCK, AF5 | DISPLAY FPC | 初次板测使用 30MHz SPI 时钟 |
| ST7789 | MOSI | PE14 | SPI4_MOSI, AF5 | DISPLAY FPC | 单向写屏，无 MISO/SDO |
| ST7789 | DC | PE15 | GPIO output | DISPLAY FPC | 数据/命令选择 |

说明：J2/J3 编号来自 2019 M 版原理图；画封装时仍须以 V2.0 实物丝印方向为准，不能仅凭表格镜像放置两个 2x22 插座。

## 3. 模块排母定义

### 3.1 INMP441，1x6

模块丝印顺序为 `SCK, WS, L/R, SD, VDD, GND`。

| 模块脚 | 连接 |
|---|---|
| SCK | PD0 / SAI3_SCK_A |
| WS | PD4 / SAI3_FS_A |
| L/R | GND，固定选择当前训练和固件使用的声道 |
| SD | PD1 / SAI3_SD_A |
| VDD | 3.3V |
| GND | GND |

### 3.2 ST7789，开发板 DISPLAY FPC

屏幕使用与开发板匹配的蓝白软排线直插 DISPLAY FPC，不在载板上增加 1x8 排母或重新布 SPI 信号。

| 屏幕信号 | H7 连接 |
|---|---|
| CS | PE11 / GPIO软件片选 |
| SCK | PE12 / SPI4_SCK |
| MOSI | PE14 / SPI4_MOSI |
| DC | PE15 / GPIO |
| BL | PD15 / GPIO |
| VCC | 3.3V |
| GND | GND |

FPC 触点 `1、3、8` 为 GND，`9、10` 为 3.3V，插座固定脚 `11、12` 接 GND。该屏没有 RES 和 MISO/SDO；不得按普通 8 针 ST7789 模块的针序接线。排线插入前必须按开发板和屏幕实物确认触点朝向。

### 3.3 DHT11，1x3

按模块丝印连接 `GND, I/O, VCC`，不得按照片方向猜针序。

| 模块脚 | 连接 |
|---|---|
| GND | GND |
| I/O | PE7 |
| VCC | 3.3V |

模块板已带上拉和指示灯，载板不重复放上拉电阻。

### 3.4 GP2Y1010 适配板，1x4

使用实物适配板丝印 `VCC, GND, AO, ILED`。

| 模块脚 | 连接 |
|---|---|
| VCC | 5V |
| GND | GND |
| AO | PC1 / ADC123_INP11 |
| ILED | PD7 |

金属传感器继续通过原装 6 芯插头连接适配板；载板只安装适配板的 1x4 插座。

### 3.5 MQ-135 模块，1x4

| 模块脚 | 连接 |
|---|---|
| VCC | 5V |
| GND | GND |
| AO | PA3 / ADC12_INP15 |
| DO | NC，不连接 |

电位器只调 DO 比较阈值，对本项目使用的 AO 没有校准作用。MQ-135 只输出相对基线指数，不宣称 ppm 或具体气体浓度。

### 3.6 RGB 模块，1x4

按实物丝印连接：公共阴极接 GND，R/G/B 分别接 PB4/PB5/PB3。模块已有三个限流电阻。

### 3.7 蜂鸣器模块，1x3

按实物丝印 `GND, I/O, VCC` 连接，I/O 接 PB0。默认沿用已验证的 3.3V 供电和低电平触发方式。播放期间固件暂停新的哭声告警判定，停止后再等待短暂安静期，避免蜂鸣器声音反向触发模型。

### 3.8 ESP-01，2x4 可拆插座

| ESP 信号 | 连接 |
|---|---|
| VCC | 3.3V |
| GND | GND |
| EN / CH_PD | 3.3V |
| TX | PA1 / UART4_RX |
| RX | PA0 / UART4_TX |
| RST | PA2 |
| GPIO0 | KEY2，按键另一端接 GND |
| GPIO2 | NC |

ESP 必须可拔下，用外部 CH340 单独烧录。载板不放 ESP 烧录排针和测试焊盘。不要在上电或复位瞬间按住 KEY2，否则 ESP 会进入串口下载模式；日常配网只在固件正常运行后长按 5 秒。

## 4. OV2640 使用开发板 FPC

开发板 CAMERA FPC 已经完成 DCMI、SCCB、电源和补光控制连接，载板不要再复制这些连线。

| FPC 脚 | 信号 | H7 引脚 |
|---:|---|---|
| - | SCCB_SDA | PB9 |
| - | SCCB_SCL | PB8 |
| - | RESET | PC4 |
| - | FLASH / 双补光灯 | PA5 |
| - | PWDN | PD14 |
| - | HREF | PA4 |
| - | VSYNC | PB7 |
| - | D0 | PC6 |
| - | D1 | PC7 |
| - | D2 | PE0 |
| - | D3 | PE1 |
| - | D4 | PE4 |
| - | D5 | PD3 |
| - | D6 | PE5 |
| - | D7 | PE6 |
| - | PCLK | PA6 |

摄像头模块自带 24MHz 有源晶振，因此不需要 H7 输出 XCLK。`PC4` 原来的 H7 软件按键功能必须删除，因为它已是摄像头 RESET；旧 OLED 的 `PB10/PB11` 也不再初始化。RGB蓝色改用同侧排针的`PB3`。

补光灯只在抓拍前短暂开启，抓拍完成立即关闭；不作为常亮夜灯。哭声确认后先生成本地事件，再抓拍，摄像头失败不能阻塞声音告警和 MQTT 告警。

## 5. 按键与调试排针

V2.0 实物上的 2x4 调试排针丝印包含两组：

- `5V, GND, DIO, CLK`
- `3V3, RST, TXD, RXD`

载板必须按实物方向放置对应 2x4 母座，不能使用旧版原理图推测方向。

| 功能 | 连接 | 行为 |
|---|---|---|
| KEY1 | RST 与 GND | 按下立即硬复位 H7 |
| KEY2 | ESP GPIO0 与 GND | ESP 正常运行时长按 5 秒清除 Wi-Fi 并进入 AP 配网 |
| CH340 | TXD/RXD/GND | 只调试 H7 的 USART1，不给系统供电 |

KEY1 不与普通 GPIO 连接，不依赖任务调度，因此固件卡死时仍可复位。H7 自带 IWDG 仍作为无人操作时的自动恢复手段。

## 6. 开发板上必须保留的占线

以下引脚即使固件暂时不用，也已经连接板载器件，载板不得分配给其他模块：

| 板载器件 | 占用引脚 |
|---|---|
| W25Q QSPI Flash | PB2, PB6, PD11, PD12, PD13, PE2 |
| MicroSD | PC8, PC9, PC10, PC11, PC12, PD2 |
| USB | PA11, PA12 |
| SWD | PA13, PA14 |
| 25MHz HSE | PH0, PH1 |
| 32.768kHz LSE | PC14, PC15 |
| 板载 K1/K2 | PE3, PC5 |
| CAMERA FPC | 第 4 节列出的 15 根信号线 |

特别禁止：不能再提出 `PD11/PD12/PD13` 的 SAI2 音频方案，也不能把 `PC4` 当作普通功能按键。

## 7. DMA 与内存预算

| 用途 | 资源 |
|---|---|
| 音频 SAI3 RX | DMA1_Stream0，DMA_REQUEST_SAI3_A |
| OV2640 DCMI | DMA2_Stream7，DMA_REQUEST_DCMI |
| TFLM tensor arena | AXI SRAM 512KB，现已全部预留 |
| 当前音频 DMA/环形缓冲/快照 | D2 SRAM 130432B |
| D2 SRAM 剩余 | 164480B |
| D3 SRAM | 64KB，当前未使用 |

摄像头只允许设置一个可复用的大缓冲区，建议最大 128KB。RGB565 屏幕图和 JPEG 上传图不得同时各占一份完整帧缓冲。先完成低分辨率事件抓拍，再根据实测 JPEG 大小决定 160x120 或 320x240；照片上传必须分块，不能把整张 Base64 图片拼成一个 UART 文本行或一个默认 256B MQTT 包。

## 8. 载板布局约束

1. H7 开发板放在中部，USB-C、CAMERA FPC、RST/BOOT 和 SD 卡槽周围留操作空间。
2. TFT 位于正面并直插开发板 DISPLAY FPC；载板只预留屏幕、排线和固定结构空间，不画 TFT 电气封装。
3. OV2640 位于外壳上部正面，通过原装 FPC 连接；排线不折死，不从 MQ-135 加热区经过。
4. INMP441 靠近前面板拾音孔，远离蜂鸣器、MQ-135、ESP 天线和 USB 电源线。
5. MQ-135 放在外壳边缘独立开孔处，与 DHT11、INMP441 和塑料结构保持距离，周围不得封闭积热。
6. DHT11 和 GP2Y1010 进气区域放在较冷的一侧，不能贴着 MQ-135。
7. GP2Y1010 金属传感器固定在外壳风道，适配板插在载板 1x4 母座；原装 6 芯线保留。
8. ESP-01 天线端朝 PCB 外缘，天线正下方和前方至少 15mm 不铺铜、不放 H7、TFT 金属件或传感器。
9. RGB 和蜂鸣器靠近外壳可见/可听位置，但蜂鸣器声孔不要正对 INMP441。
10. 所有模块插座在丝印上同时标信号名和 1 脚方向；禁止只写 J1、J2 等无意义编号。

## 9. 上电前必须完成的三项实测

1. 不接 H7 ADC，先给 MQ-135 模块上 5V，用万用表测 AO；在计划演示的刺激范围内确认 AO 始终不超过 3.3V。超过时必须增加现成分压/电平适配，不能直连。
2. 同样测 GP2Y1010 适配板 AO 的最大电压，确认不超过 3.3V 后再接 PC1。
3. 模块全部接齐、ESP 联网、TFT 刷新、OV2640 补光和蜂鸣器同时工作，连续运行 30-60 分钟，检查 H7 是否复位、ESP 是否掉线以及 3.3V 稳压区域是否异常发热。

这三项是接口验收，不是要求设备连续运行 24 小时。比赛前再做分时累计的多轮稳定性测试即可。

## 10. 固件迁移顺序

1. SAI1 改为 SAI3并实板回归原有二分类模型，确认 16kHz、DMA 双缓冲和不可变快照不变。
2. DHT11 改到 PE7，保持 TFT 实时 2 秒刷新与独立告警确认逻辑。
3. 加入 SPI4 ST7789，先完成所有状态页面，不接摄像头。固件已烧录且原功能回归通过，等待屏幕到货验证画面。
4. 加入 ADC 与 GP2Y ILED 时序，完成原始值、基线、滤波、故障恢复和环境告警。
5. 最后加入 DCMI OV2640：先本地抓拍，再 TFT 显示，再做 ESP 分块上传。

每一步都单独编译、烧录和板测；不能在第一次整机上电时同时调试五个新外设。
