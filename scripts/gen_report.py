"""Generate project report PDF — AI Hear (updated June 2026)"""
from fpdf import FPDF
import os

pdf = FPDF()
pdf.add_font('ZH', '', r'C:\Windows\Fonts\msyh.ttc')
pdf.add_font('ZH', 'B', r'C:\Windows\Fonts\msyh.ttc')

def title_page():
    pdf.add_page()
    pdf.ln(45)
    pdf.set_font('ZH', 'B', 26)
    pdf.cell(0, 14, '基于STM32H7的边缘AI', align='C', new_x='LMARGIN', new_y='NEXT')
    pdf.cell(0, 14, '语音识别居家安防系统', align='C', new_x='LMARGIN', new_y='NEXT')
    pdf.ln(25)
    pdf.set_font('ZH', '', 13)
    for t in ['组号：____', '', '成员1  李林坤  25019100199  25通信06班',
              '成员2  张杨硕  25019100334  25通信06班',
              '成员3  缑润菁  25019100262  25通信06班']:
        pdf.cell(0, 11, t, align='C', new_x='LMARGIN', new_y='NEXT')
    pdf.ln(15)
    pdf.set_font('ZH', '', 11)
    pdf.cell(0, 8, '意法半导体（ST）STM32H743VIT6  Cortex-M7  最高480MHz', align='C', new_x='LMARGIN', new_y='NEXT')
    pdf.ln(5)
    pdf.cell(0, 8, '2026年6月', align='C', new_x='LMARGIN', new_y='NEXT')

def hdr(t):
    pdf.set_fill_color(30,60,120); pdf.set_text_color(255,255,255)
    pdf.set_font('ZH','B',14)
    pdf.cell(0,10,f'  {t}', fill=True, new_x='LMARGIN', new_y='NEXT')
    pdf.set_text_color(0,0,0); pdf.ln(5)

def sub(t):
    pdf.set_font('ZH','B',11.5); pdf.ln(2)
    pdf.cell(0,8,t,new_x='LMARGIN',new_y='NEXT')

def body(t):
    pdf.set_font('ZH','',10.5)
    pdf.multi_cell(0,6.5,t); pdf.ln(1)

def bullet(t):
    pdf.set_font('ZH','',10)
    pdf.cell(0,6.5,f'    - {t}',new_x='LMARGIN',new_y='NEXT')

def cell_row(cells, widths, alt=False):
    pdf.set_font('ZH','',10)
    pdf.set_fill_color(245,248,252) if alt else pdf.set_fill_color(255,255,255)
    for i,c in enumerate(cells):
        pdf.cell(widths[i],7.5,c,border=1,fill=True,align='C')
    pdf.ln()

def table_header(cols, widths):
    pdf.set_fill_color(50,70,110); pdf.set_text_color(255,255,255)
    pdf.set_font('ZH','B',9)
    for i,c in enumerate(cols):
        pdf.cell(widths[i],8,c,border=1,fill=True,align='C')
    pdf.ln(); pdf.set_text_color(0,0,0)

# ==================== PAGE 1: COVER ====================
title_page()

# ==================== PAGE 2: INTRO ====================
hdr('1. 项目介绍及成果')

sub('1.1 项目概述')
body('本系统以 STM32H743VIT6 为主控芯片（Cortex-M7 内核，最高 480MHz），采集环境音频信号，在本地运行轻量化 TFLite Micro 语音关键词边缘 AI 推理，实时识别异响、呼救等异常声音事件；触发声光报警提示，同时通过 WiFi 模块以 MQTT 协议将告警信息远程上传云端，实现本地实时监测、异常智能判别、无线远程告警与事件记录存储，完成低延迟、高可靠居家安防智能化管控。')

sub('1.2 项目背景与意义')
body('居家安全是社会民生的核心关切。传统安防方案面临三大痛点：一是门磁、红外等单一传感器误报率高，宠物经过即可触发；二是云端方案依赖网络，存在延迟和隐私风险；三是市售智能音箱类产品价格高、功耗大，难以 24 小时布防。')
body('边缘 AI 技术为解决上述问题提供了新路径。STM32H743 的 Cortex-M7 内核配合 CMSIS-NN 神经网络加速库，可在 MCU 端完成音频推理，无需将原始录音上传云端。本系统瞄准"低延迟、低成本、高可靠"的居家安防细分场景，通过声音识别替代/补充传统传感器，实现对呼救、玻璃破碎等高危事件的精准检测。')
body('硬件平台选用 STM32H743VIT6（Cortex-M7, 480MHz, 2MB Flash, 1MB SRAM），具备双精度 FPU、硬件 DSP 指令集和 ART 加速器，为实时音频 DSP 和神经网络推理提供了充足的算力保障。')

sub('1.3 系统总体架构')
body('系统由四层组成：感知层（INMP441 数字麦克风 + DMA 双缓冲采集）、计算层（CMSIS-DSP 预处理 + TFLite Micro 推理）、执行层（RGB LED + 蜂鸣器 + OLED 显示）、通信层（ESP8266 WiFi + MQTT + Flask 后端 + 微信推送）。软件层面采用 FreeRTOS 实时操作系统，单任务流水线架构，DMA 中断通过 TaskNotify 唤醒音频处理任务。')
pdf.ln(2)
body('数据流：I2S 音频 → DMA 环形缓冲 → 32 帧 512-pt FFT → 40 mel 滤波器组 → log-mel 频谱 → Z-score 归一化 → DS-CNN 推理 → 3 分类输出 → 告警判断 → MQTT Publish。')
pdf.ln(4)

sub('1.4 硬件设计')
body('主控采用鹿小班 STM32H743VIT6 核心板 V2.0，板上集成 CH340 USB 转串口。外设包括：')
bullet('INMP441 数字 MEMS 麦克风（I2S 接口，16kHz/24bit，SAI1 外设）')
bullet('ESP8266-01S WiFi 模块（UART4, 115200bps，TX/RX 交叉连接）')
bullet('SSD1306 128x64 OLED 显示屏（I2C bit-bang，PD0/PD1）')
bullet('共阳 RGB LED（PB4/5/8，串 220Ω 限流电阻）')
bullet('有源蜂鸣器（PB0，TIM3 PWM，OCPOLARITY_LOW 低电平触发）')
bullet('轻触按键（PC4，上拉输入，软件消抖，布防/撤防切换）')
bullet('HC-SR501 人体红外传感器（5V，预留待集成）')
pdf.ln(2)

body('核心硬件引脚速查表：')
pin_header = ['外设', '引脚', '备注']
pin_rows = [
    ['INMP441', 'PE4(WS) PE5(SCK) PE6(SD)', 'I2S, VCC->3.3V, L/R->GND'],
    ['ESP8266', 'PA1(RX) PA0(TX)', 'UART4, TX/RX交叉'],
    ['OLED', 'PD0(SCL) PD1(SDA)', 'I2C bit-bang, 3.3V'],
    ['RGB LED', 'PB4(R) PB5(G) PB8(B)', '各220ohm, 共阳'],
    ['蜂鸣器', 'PB0(TIM3 PWM)', 'OCPOLARITY_LOW'],
    ['按键', 'PC4 -> GND', '上拉, 按下=0'],
    ['USB-TTL', 'PA9(TX) PA10(RX)', 'CH340, 115200bps'],
    ['ST-Link', 'SWDIO/SWCLK/GND/3.3V', '烧录调试'],
]
w = [32, 76, 52]
table_header(pin_header, w)
for i, r in enumerate(pin_rows):
    cell_row(r, w, i % 2 == 0)
pdf.ln(4)
body('（此处贴入系统原理图）')
pdf.ln(20)
body('[原理图预留区域]')
pdf.ln(4)

sub('1.5 嵌入式软件设计')
body('软件基于手动集成的 FreeRTOS v10.6.2 内核，采用单任务架构：AudioInferTask 优先级 3、8KB 栈空间。DMA 中断通过 vTaskNotifyGiveFromISR 发送通知，任务唤醒后一次性读取 32 帧 PCM 数据（共 5472 采样点，约 342ms 窗口），帧顺序从最旧到最新（与 Python 训练严格一致），经 CMSIS-DSP 512-pt RFFT + Mel 滤波器组生成 40x32 特征矩阵，送入 TFLite Micro DS-CNN 模型进行 3 分类推理。')

body('预处理管线严格对齐 Python 训练管线，期间解决了两个关键预处理 BUG：')
body('(1) PCM 缩放修正：INMP441 输出 24bit I2S 数据，经 (int32_t)raw >> 8 转换为 int32 PCM，范围约 [-8388608, 8388607]。初期代码错误地除以 32768（16bit 范围），导致信号放大 256 倍、特征空间完全错位。修正为除以 8388608.0f 后，FFT 输入归一化至 [-1,1]，与 Python librosa.load 输出范围一致。')
body('(2) 时间轴修正：Audio_ReadSamples 读取时，offset=512 的帧为最近 512 样本，offset=5472 的帧为最旧 512 样本。初期代码 for(f=0->31) 先喂入最新帧，导致 MCU 特征矩阵 spec[:,0] 为最新帧而 Python 的 lm[:,0] 为最早帧——spectrogram 时间轴反转。修正为 for(f=31->0) 从最旧帧开始喂入。')

body('模型采用 float32 格式，TFLite 模型 78KB，arena 大小 512KB（AXI SRAM），环形缓冲区 32KB 置于 DTCM。推理耗时 < 10ms。未采用 INT8 量化——onnx2tf 的 flatbuffer_direct 模式使用固定 scale=1/128，无法提供校准数据，量化后精度从 92.6% 骤降至 ~25%。')

body('报警逻辑包括：峰值阈值门控（仅 peak > 50000 触发推理）、margin 置信度过滤（top1-top2 >= 1.5 才触发告警，滤除低置信度误报）、5 秒冷却防止重复告警。蜂鸣器 PWM 驱动扫频警笛，RGB LED 根据状态显示多种颜色模式。按键采用 button.c 独立消抖模块，PC4 上拉输入，连续 3 次读到低电平才确认按下。')

sub('1.6 模型训练')
body('数据集经过三次迭代后采用纯语义三类数据集，总计 874 条 16kHz 单声道样本：')
body('  - help（救命）：102 条 = PC 录制的真人"救命"语音 + Microsoft Edge TTS 中文"救命"（6 种音色 x 5 速率 x 5 音调）')
body('  - glass_break（玻璃碎裂）：210 条 = PC 录制的敲玻璃声 + ESC-50 glass_breaking 类（40 源文件 x 8 窗口峰值提取）')
body('  - impact（撞击）：562 条 = PC 录制的拍手/敲门声 + ESC-50 door_wood_knock + clapping 类（各 40 源文件 x 8 窗口）')

body('数据增强采用在线方式（训练时实时生成）：音高偏移（+/-3 半音，30% 概率）、时间拉伸（0.85-1.15x，30% 概率）、高斯噪声叠加（SNR 15-30dB，40% 概率）、音量增益（0.6-1.4x，30% 概率）、SpecAugment 频域时域 Masking（50% 概率）。')

body('模型采用 DS-CNN 架构：Conv2D(1->32) + 4 x DepthwiseSeparableConv2d（32->64->128） + GlobalAveragePool + FC(128->3)，约 1.9 万参数，36M MACs。训练策略包括 WeightedRandomSampler（类平衡）、CrossEntropyLoss(class_weight)、MixUp（alpha=0.2）、AdamW（lr=0.002）、CosineAnnealingWarmRestarts 学习率调度。训练 80 轮，验证集准确率 92.6%（help=100%, glass=94.8%, impact=96.8%）。')

body('模型导出流程：PyTorch -> ONNX（torch.onnx.export, TorchScript 后端, opset=17） -> float32 TFLite（onnx2tf flatbuffer_direct 转换） -> C 数组（dscnn_model.cc）。未采用 INT8 量化的原因：onnx2tf 的 flatbuffer_direct 模式不支持校准数据集、Keras 权重传输存在 BN 层 set_weights 时序问题。')

sub('1.7 WiFi 通信与云平台')
body('ESP8266 固件基于 Arduino/PlatformIO 开发。STM32 与 ESP8266 之间使用简单文本协议（+PUB:topic:payload、+STATUS、+PUBACK）。MQTT Broker 为 broker-cn.emqx.io（公网免费），主题包括 aihear/alert（告警）、aihear/status（心跳，30s 周期）。')
body('本地部署 Flask Web 后端，通过 paho-mqtt 订阅同一 Broker，收到告警后存入 SQLite 数据库并提供 Web 仪表盘（localhost:5000）。同时通过 WxPusher 免费 API 将告警推送至用户微信。')

sub('1.8 成果展示')
body('系统实现了完整的功能闭环：')
bullet('安静环境：OLED 显示 ARMED + 实时峰值')
bullet('拍桌子/敲门：检测 IMPACT（margin >= 1.5），红色 LED 急闪 + 蜂鸣器警笛，MQTT 推送')
bullet('敲玻璃：检测 GLASS，同上触发全部报警')
bullet('喊"救命"：检测 HELP，同上触发全部报警')
bullet('低置信度声音：显示 [WEAK] + 类别，不触发告警')
bullet('按键切换撤防：OLED 显示 DISARMED，停止检测（软件消抖）')
bullet('Web 仪表盘：实时告警时间线，运行时长，设备在线状态')
bullet('微信推送：WxPusher 免费公众号推送告警信息到手机')
pdf.ln(2)
body('（此处贴入实物照片、OLED显示特写、Web面板截图、微信推送截图等）')
pdf.ln(15)
body('[实物照片及截图预留区域]')
pdf.ln(4)

# ==================== PERFORMANCE ====================
sub('1.9 性能指标')
rows = [
    ['固件大小', '374KB text + 0.5KB data + 534KB bss'],
    ['模型大小', '78KB float32 TFLite (1.9万参数)'],
    ['模型推理耗时', '< 10ms（480MHz Cortex-M7）'],
    ['预处理耗时', '32帧 512-pt FFT < 3ms'],
    ['DMA 中断间隔', '16ms（256 采样 @ 16kHz）'],
    ['检测延迟', '< 500ms（含预处理+推理+告警输出）'],
    ['MQTT 告警延迟', '< 50ms（局域网 WiFi）'],
    ['Tensor Arena', '512KB（AXI SRAM）'],
    ['环形缓冲', '32KB（DTCM，8192 样本）'],
    ['验证准确率', '92.6%（3分类，float32）'],
    ['硬件成本', '< 200元（全部元器件）'],
]
w = [44, 120]
pdf.set_fill_color(50,70,110); pdf.set_text_color(255,255,255)
pdf.set_font('ZH','B',9)
pdf.cell(w[0],8,'指标',border=1,fill=True,align='C')
pdf.cell(w[1],8,'数值',border=1,fill=True,align='C')
pdf.ln(); pdf.set_text_color(0,0,0)
for i,r in enumerate(rows):
    cell_row(r, w, i%2==0)
pdf.ln(4)

# ==================== TEAM ====================
hdr('2. 团队分工及贡献度评价')
sub('2.1 成员分工')
body('本项目由三名成员协作完成，具体分工如下：')
body('李林坤（队长）：负责系统整体架构设计、FreeRTOS 固件开发、音频预处理和 TFLite Micro 推理集成、模型训练与量化导出、硬件驱动开发（I2S/DMA/TIM/PWM/OLED/按键）、系统联调与性能优化。完成核心代码约 2000 行（C/C++/Python）。')
body('张杨硕：负责硬件原理图设计、元器件 BOM 编制、硬件焊接与调试、项目报告撰写与排版、答辩 PPT 制作。')
body('缑润菁：负责 ESP8266 WiFi 模块固件开发、MQTT 通信协议设计与调试、Flask 后端开发与部署、Web 仪表盘前端设计、WxPusher 微信推送集成。')
pdf.ln(2)
sub('2.2 贡献度评价')
body('团队成员在各自分工领域内均完成了预定任务。李林坤作为队长承担了最核心的嵌入式 AI 软件工作，贡献度约 50%；张杨硕和缑润菁分别负责硬件与通信/后端，各贡献约 25%。团队成员之间沟通顺畅，定期同步进度，遇到技术难题共同讨论解决方案。')
pdf.ln(4)

# ==================== REFLECTION ====================
hdr('3. 心得与经验')

sub('3.1 数据驱动的 AI 开发（五个关键踩坑点）')
body('本项目最深刻的体会是：AI 模型效果严重依赖训练数据质量和预处理的一致性。以下按时间线记录五个关键踩坑点及解决过程：')

body('(1) 数据质量清洗：初期 ESC-50 数据集经固定偏移切片后产生 372 个静音文件（6.3%），glass_break 类最严重（15.8%）。对静音文件运行数据增强导致静音 x10——无效数据翻倍。改用峰值检测窗口提取（寻找 5 秒文件中 RMS 最高的 1 秒区间，每个文件取 3 个重叠窗口）后零静音。')

body('(2) 类别语义映射：初期将 ESC-50 的 crying_baby 映射为 help、footsteps 映射为 normal、fireworks/hand_saw/keyboard_typing 混入 impact——这些声音的频谱特征与目标类别完全不同。修正为纯语义映射：help 仅含中文"救命"、glass 仅含 glass_breaking、impact 仅含 door_wood_knock+clapping。')

body('(3) 训练-推理预处理一致性（三个致命 BUG）：')
body('  a) PCM 缩放范围不匹配：MCU 端 INMP441 的 24bit I2S >> 8 后范围为 [-8388608, 8388607]，但预处理代码除以 32768（16bit 范围），导致 FFT 输入信号放大 256 倍。Python librosa.load 输出范围为 [-1,1]，特征空间完全错位，模型输出恒为常数。修正为除以 8388608.0f。')
body('  b) Spectrogram 时间轴反转：for(f=0->31) 先读 offset=512（最新帧）再读 offset=5472（最旧帧），导致 spec[:,0]=最新帧而 Python 的 lm[:,0]=最早帧——两个 spectrogram 互为镜像。修正为 for(f=31->0) 从最旧帧开始喂入。')
body('  c) float32 vs INT8 量化陷阱：onnx2tf 的 flatbuffer_direct 模式使用固定 scale=1/128 进行全整数量化，无法提供校准数据，量化后精度从 92.6% 骤降至 ~25%。Keras 重建 PyTorch 权重时 BN 层 set_weights 存在时序问题（逐层对比验证确认 d1.bn1 开始分叉）。最终采用 float32 模型，以 512KB arena 换取无需量化校准的可靠性。')

body('(4) 域迁移问题：PC 耳机麦克风录制的训练数据在 INMP441 设备端完全无法泛化——不同麦克风的频率响应、灵敏度、噪声底各不相同。z-score 归一化虽然消除了绝对幅度差异，但频率响应差异在 mel 频谱中仍显著。解决方案是尽可能使用目标设备（INMP441）自身录制训练数据。')

body('(5) 边际置信度过滤：仅凭 argmax 选最高分会导致低置信度误报。引入 top1-top2 margin 阈值（>= 1.5），滤除模型不确定的边界样本，显著减少安静环境下的随机误报。')

sub('3.2 FreeRTOS 实时系统调试')
body('FreeRTOS 集成过程中遇到的最大问题——DMA 中断优先级。CubeMX 默认将 DMA IRQ 设为优先级 0（NVIC 最高优先级），但 FreeRTOS 要求调用 API 的 ISR 优先级必须 >= configMAX_SYSCALL_INTERRUPT_PRIORITY(5)。优先级 0 的 ISR 中调用 vTaskNotifyGiveFromISR 不会报错但静默失效，AudioTask 永远等不到通知。排查两小时后才定位到该问题。')
body('跨任务通信方面，xQueueSend 从高优先级 AudioTask 调用会导致 DMA 通知管道断裂（原因至今未明），vTaskDelay 在 MqttAlarmTask 中也静默失效。最终采用最简单的 volatile flag + 合并任务方案，牺牲了架构优雅性换取了系统可靠性。')

sub('3.3 硬件调试要点')
body('STM32H743 的 SRAM ECC 是冷启动陷阱：.tensor_arena 段在链接脚本中标记为 (NOLOAD)，启动代码不会对其清零。冷上电时该区域 SRAM 的 ECC 校验位随机，CPU 首次读取即触发 HardFault。解决方法是在 TFLM_Init 开头显式 memset 整个 tensor arena。')
body('蜂鸣器调试中，PWM 输出极性与有源蜂鸣器匹配是关键。TIM_OCPOLARITY_HIGH 配合比较值=0 产生持续低电平，对有源（低电平触发）蜂鸣器即为长鸣；改为 TIM_OCPOLARITY_LOW 后比较值=0 输出高电平，蜂鸣器才正常关闭。')
body('按键消抖也是硬件调试中的常见问题。初期 freertos.c 中直接用 HAL_GPIO_ReadPin 读取 + btn_prev/btn_now 边沿检测，因机械抖动导致 3 次连续变化判断失效。改为 button.c 独立消抖模块——连续 3 次读到低电平才确认按下——后按键可靠工作。')
body('内存布局方面，DTCM 仅 128KB，环形缓冲区（32KB）和 PCM 录音 buffer（64KB）同时存在时会导致 DTCM 溢出。最终将大型静态 buffer 通过链接脚本 .ring_buf 段放置到 512KB AXI SRAM，DTCM 仅保留 BSS 和栈。')

sub('3.4 工程实践体会')
body('通过本项目，团队成员完整地经历了嵌入式 AI 产品从需求分析、硬件选型、驱动开发、模型训练、系统集成到上线演示的完整流程，提升了以下能力：')
bullet('嵌入式系统设计：FreeRTOS 任务优先级、中断管理、DMA 双缓冲、内存布局（DTCM vs AXI SRAM 分配策略）')
bullet('音频 DSP：FFT 频谱分析、Mel 滤波器组设计、特征工程、训练-推理预处理一致性的逐环节验证')
bullet('深度学习：CNN 模型设计、数据集工程（清洗->标注->增强->质量审计）、域迁移问题诊断、ONNX->TFLite 部署管线')
bullet('全栈开发：Python Flask、MQTT 协议、Web 前端、微信 API 集成')
bullet('工程调试：DWT 计时、HardFault 诊断（SRAM ECC 冷启动）、逻辑分析仪使用、链路逐级排查')
bullet('C++ 与 Python 联合调试：逐层对比 PyTorch vs Keras 中间张量定位 BN 权重传输错误、串口 PCM dump 验证 MCU 端数据质量')

sub('3.5 未来改进方向')
bullet('模型升级：增加连续 N 帧确认降误报，引入 MobileNetV3 提升精度')
bullet('传感器融合：集成 HC-SR501 PIR 人体红外，实现声音+人体双重检测')
bullet('低功耗优化：利用 FreeRTOS Tickless Idle 模式降低待机功耗')
bullet('本地部署：Mosquitto MQTT Broker 本地化，减少公网依赖')
bullet('OTA 升级：ESP8266 OTA 固件升级 + STM32 Bootloader')
bullet('量产：PCB 打样、外壳设计、成本优化')
pdf.ln(6)

# ==================== REFERENCES ====================
hdr('4. 参考文献')
refs = [
    '[1] STMicroelectronics. RM0433 Reference Manual: STM32H743/753[Z]. 2023.',
    '[2] Amazon Web Services. FreeRTOS Kernel v10.6.2 Developer Guide[Z]. 2021.',
    '[3] Google LLC. TensorFlow Lite for Microcontrollers[EB/OL].',
    '[4] ARM Ltd. CMSIS-DSP Software Library v1.14.0[EB/OL].',
    '[5] Piczak K J. ESC: Dataset for Environmental Sound Classification[C]. ACM MM 2015.',
    '[6] TDK InvenSense. INMP441 Datasheet[Z]. 2020.',
    '[7] Espressif Systems. ESP8266EX Datasheet v6.6[Z]. 2020.',
    '[8] Solomon Systech. SSD1306 Datasheet[Z]. 2008.',
    '[9] Howard A G, et al. MobileNets: Efficient CNNs for Mobile Vision[J]. arXiv:1704.04861.',
    '[10] 意法半导体. AN4838: STM32H7x3 应用笔记[Z]. 2023.',
    '[11] 意法半导体. AN4938: STM32H7 DMA 使用指南[Z]. 2022.',
    '[12] WxPusher 开发者文档[EB/OL]. https://wxpusher.zjiecode.com/docs/.',
]
for r in refs:
    pdf.set_font('ZH','',9.5)
    pdf.cell(0,6.5,r,new_x='LMARGIN',new_y='NEXT')
pdf.ln(6)

# ==================== COMPETITION ====================
hdr('5. 竞赛截图及原理图')
body('（如该项目参加了嵌入式大赛，请贴出报名截图和完赛截图）')
pdf.ln(35)
body('[报名/完赛截图预留区域]')
pdf.ln(10)
body('（系统原理图请贴于下方或另附页）')
pdf.ln(50)
body('[原理图预留区域]')

# Save
desktop = os.path.join(os.path.expanduser('~'), 'Desktop')
path = os.path.join(desktop, 'AI_Hear_项目报告_更新版.pdf')
pdf.output(path)
print(f'Done: {path}')
