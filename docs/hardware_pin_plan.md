# AI Hear 最终硬件实施表

> 本文是唯一 BOM 与引脚依据。主控为鹿小班 STM32H743VIT6 V2.0，HSE=25MHz。
>
> 载板只焊排母、排针和按键；ST7789、OV2640分别直插开发板 DISPLAY/CAMERA FPC。

## 1. 最终物料

| 模块 | 数量 | 供电 | 作用/接口 |
|---|---:|---|---|
| STM32H743VIT6 V2.0开发板 | 1 | USB-C 5V | 480MHz主控、DSP、TFLM推理和全部控制 |
| INMP441模块 | 1 | 3.3V | SAI3，16kHz/24bit哭声采集 |
| ESP-01/ESP8266EX | 1 | 3.3V | UART4、AP配网、MQTT；使用可拆2x4插座 |
| DHT11三针模块 | 1 | 3.3V | 温湿度实时显示与固定阈值告警 |
| MQ-135四针模块 | 1 | 5V | AO动态基线，只输出气态污染相对指数 |
| GP2Y1010/1014金属传感器+V2.1适配板 | 1套 | 5V | 颗粒物相对指数；保留原装6芯连接线 |
| 1.54英寸ST7789 240x240 | 1 | FPC 3.3V | 状态、分贝、环境数据和事件画面 |
| OV2640双补光模块 | 1 | FPC 3.3V | 哭声事件/手机请求单帧抓拍，不做视频 |
| 共阴RGB模块 | 1 | 3.3V | 本地状态和告警灯，模块自带限流电阻 |
| 低电平触发无源蜂鸣器模块 | 1 | 3.3V | TIM3 PWM旋律，模块自带驱动 |
| 6x6mm轻触按键 | 2 | - | KEY1硬复位H7；KEY2长按5秒清除ESP配网 |
| 双层载板PCB、2.54mm排母/排针、尼龙螺柱 | 1套 | - | 建议下单3-5块裸板并留1块焊接备份 |

不加入OLED、MQ-2、PIR、MPU6050、槽型红外或连续视频模块。

## 2. 载板唯一接线表

开发板两侧扩展排针为 `H2/H3`，均为 `2x20P`；禁止使用旧的 `J2/J3` 或 `2x22` 编号。

| 模块信号 | H7引脚/功能 | 开发板位置 | 连接 |
|---|---|---|---|
| ESP RX | PA0 / UART4_TX AF8 | H2-33 | PA0 -> ESP RX |
| ESP TX | PA1 / UART4_RX AF8 | H2-34 | PA1 <- ESP TX |
| ESP RST | PA2 / GPIO输出 | H2-31 | 低电平复位ESP |
| MQ-135 AO | PA3 / ADC12_INP15 | H2-32 | DO悬空 |
| 蜂鸣器 I/O | PB0 / TIM3_CH3 AF2 | H2-23 | 低电平触发模块 |
| DHT11 DATA | PE7 / 单总线GPIO | H2-21 | 模块I/O |
| GP2Y1010 AO | PC1 / ADC123_INP11 | H3-14 | 适配板AO |
| GP2Y1010 ILED | PD7 / GPIO输出 | H3-21 | 适配板ILED |
| INMP441 SCK | PD0 / SAI3_SCK_A AF6 | H3-15 | 模块SCK |
| INMP441 SD | PD1 / SAI3_SD_A AF6 | H3-16 | 模块SD |
| INMP441 WS | PD4 / SAI3_FS_A AF6 | H3-18 | 模块WS |
| RGB B/R/G | PB3/PB4/PB5 / GPIO | H3-22/23/24 | 共阴端接GND |
| CH340 RXD | PA9 / USART1_TX AF7 | 调试座TXD、H3-10 | CH340 RXD <- PA9 |
| CH340 TXD | PA10 / USART1_RX AF7 | 调试座RXD、H3-9 | CH340 TXD -> PA10 |
| KEY1 | NRST | 开发板调试座RST | 按键另一端接GND |
| KEY2 | ESP GPIO0 | ESP插座GPIO0 | 按键另一端接GND |

电源母线：`H3-1/H3-2=5V`，`H3-3/H3-4=3.3V`，`H2-1/H2-2=GND`。

模块插座按实物丝印连接：

| 插座 | 针序/连接 |
|---|---|
| INMP441 1x6 | SCK=PD0，WS=PD4，L/R=GND，SD=PD1，VDD=3.3V，GND |
| DHT11 1x3 | 按实物丝印顺序VCC=3.3V，DATA=PE7，GND |
| GP2Y适配板 1x4 | 按实物丝印顺序ILED=PD7，AO=PC1，GND，VCC=5V |
| MQ-135 1x4 | VCC=5V，GND，DO=NC，AO=PA3；最终以模块丝印为准 |
| RGB 1x4 | 共阴=GND，R=PB4，G=PB5，B=PB3 |
| 蜂鸣器 1x3 | GND，I/O=PB0，VCC=3.3V |
| ESP-01 2x4 | VCC/EN=3.3V，GND，TX=PA1，RX=PA0，RST=PA2，GPIO0=KEY2，GPIO2=NC |

ESP必须可拔下，拔下后再用外部CH340单独烧录。正常运行后长按KEY2 5秒配网；上电或复位时按住KEY2会进入串口下载模式。

## 3. DISPLAY FPC：ST7789

| FPC脚 | 信号 | H7连接 |
|---:|---|---|
| 1 | GND | GND |
| 2 | SCK | PE12 / SPI4_SCK AF5 |
| 3 | GND | GND |
| 4 | BL | PD15 / GPIO |
| 5 | DC | PE15 / GPIO |
| 6 | CS | PE11 / GPIO软件片选 |
| 7 | MOSI | PE14 / SPI4_MOSI AF5 |
| 8 | GND | GND |
| 9、10 | VCC | 3.3V |
| 11、12 | FPC座机械固定脚 | GND |

该屏没有RES和MISO/SDO，软件无法读回确认像素。只能使用配套软排线，并按模块与开发板触点方向插入；不是“正反都能插”。

## 4. CAMERA FPC：OV2640

| FPC脚 | 信号 | H7连接 |
|---:|---|---|
| 1、2、3 | VCC | 3.3V |
| 4、5、8、24 | GND | GND |
| 6 | SCCB_SDA | PB9 / 软件开漏 |
| 7 | SCCB_SCL | PB8 / 软件开漏 |
| 9 | RESET | PC4 / 低电平复位 |
| 10 | FLASH | PA5 / 高电平点亮补光 |
| 11 | PWDN | PD14 / 高电平掉电 |
| 12 | HREF/HSYNC | PA4 / DCMI_HSYNC AF13 |
| 13 | VSYNC | PB7 / DCMI_VSYNC AF13 |
| 14-21 | D0-D7 | PC6、PC7、PE0、PE1、PE4、PD3、PE5、PE6 / AF13 |
| 22 | PCLK | PA6 / DCMI_PIXCLK AF13 |
| 23 | NC | 不连接 |
| 25、26 | FPC座机械固定脚 | GND |

模块自带24MHz有源晶振，因此H7不输出XCLK。补光只在抓拍期间短暂开启；相机缺席或失败不得阻断声音与环境监护。

## 5. 板载占线与资源

| 板载/功能 | 不得复用的引脚 |
|---|---|
| W25Q QSPI Flash | PB2、PB6、PD11、PD12、PD13、PE2 |
| MicroSD | PC8-PC12、PD2 |
| USB | PA11、PA12 |
| SWD | PA13、PA14 |
| HSE/LSE | PH0、PH1、PC14、PC15 |
| CAMERA/DISPLAY FPC | 第3、4节全部信号 |

开发板自带两颗按键是 `BOOT0` 和 `RST`，不是PE3/PC5功能键。旧PC0 LED、PC4软件按键和PB10/PB11 OLED均已退出最终构建。

| 内存/DMA | 当前全量构建 |
|---|---|
| Flash BIN | 481344B / 2MB（约23.0%） |
| DTCM | data+bss+链接预留99576B / 128KB（约76.0%） |
| AXI SRAM | TFLM arena 512KB / 512KB |
| D2 SRAM | 音频/UART 131456B + RGB565 115200B；剩余48256B |
| D3 SRAM | JPEG 61440B；剩余4096B，禁止扩大大缓冲 |
| DMA | SAI3 RX=DMA1_Stream0；UART4 RX=DMA1_Stream1；DCMI=DMA2_Stream7 |

## 6. PCB与供电约束

- 厂家机械图给出的H7开发板外形约 `63.22 x 36.42mm`；载板建议先按 `120 x 90mm` 规划，再以实物1:1纸样收口。
- H7置中、USB-C朝外壳右侧；CAMERA/DISPLAY FPC一侧不得被模块遮挡。
- ESP天线朝PCB外缘，天线正下方及前方至少15mm禁布铜、模块和金属固定件。
- MQ-135位于板边热区并开孔，远离DHT11、INMP441、FFC和塑料支柱。
- GP2Y金属传感器固定在独立进气口；DHT11位于冷区；INMP441拾音孔远离蜂鸣器和气流直吹。
- TFT和摄像头安装在外壳前面板，只预留FFC弯折与固定空间，不在载板重复布线。
- MQ-135厂家标称高浓度时AO可能约4V。当前AO直连PA3是用户接受的受控演示风险，不得主动制造高浓度刺激，也不是量产安全接法。
- GP2Y适配板已确认含1:10分压；首次上电仍须按实物丝印复核VCC/GND/AO/ILED。

下单前必须打印1:1纸样，把H7、全部模块、排母和螺柱放上去核对针序、镜像、模块轮廓、USB与FFC空间。整机完成后连续运行30-60分钟，观察H7复位、ESP掉线、UART溢出、任务栈余量和稳压区温升。
