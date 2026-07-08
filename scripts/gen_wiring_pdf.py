"""生成 AI Hear 硬件接线手册 PDF 到桌面"""
from fpdf import FPDF
import os

pdf = FPDF()
pdf.add_page()
pdf.set_auto_page_break(auto=True, margin=15)

# 中文字体
font_paths = [r'C:\Windows\Fonts\msyh.ttc', r'C:\Windows\Fonts\simhei.ttf']
font_file = None
for fp in font_paths:
    if os.path.exists(fp): font_file = fp; break
use_cjk = font_file is not None
if use_cjk:
    pdf.add_font('ZH', '', font_file)
    pdf.add_font('ZH', 'B', font_file)
else:
    pdf.add_font('ZH', '', font_file)
    pdf.add_font('ZH', 'B', font_file)  # fallback, should never happen

def section_heading(text):
    """浅蓝色条 + 黑色粗体标题"""
    pdf.set_fill_color(235, 242, 250)
    pdf.set_text_color(30, 60, 120)
    pdf.set_font('ZH','B', 15)
    pdf.cell(0, 11, f'  {text}', new_x='LMARGIN', new_y='NEXT', fill=True)
    pdf.set_text_color(0, 0, 0)
    pdf.ln(4)

def sub_heading(text):
    pdf.set_font('ZH','B', 12)
    pdf.cell(0, 8, text, new_x='LMARGIN', new_y='NEXT')

def body(text):
    pdf.set_font('ZH','', 10)
    pdf.cell(0, 7, f'    {text}', new_x='LMARGIN', new_y='NEXT')

def numbered(n, text):
    pdf.set_font('ZH','', 10)
    pdf.cell(0, 7, f'  {n}. {text}', new_x='LMARGIN', new_y='NEXT')

def table_header(cells, widths):
    pdf.set_fill_color(50, 70, 110)
    pdf.set_text_color(255, 255, 255)
    pdf.set_font('ZH','B', 9)
    for i, val in enumerate(cells):
        pdf.cell(widths[i], 8, val, border=1, fill=True, align='C')
    pdf.ln()
    pdf.set_text_color(0, 0, 0)

def table_row(cells, widths, alt=False):
    pdf.set_font('ZH','', 8.5)
    if alt: pdf.set_fill_color(245, 248, 252)
    else:   pdf.set_fill_color(255, 255, 255)
    for i, val in enumerate(cells):
        pdf.cell(widths[i], 7, val, border=1, fill=True, align='C')
    pdf.ln()

# ===== 封面标题 =====
pdf.set_font('ZH','B', 24)
pdf.cell(0, 14, 'AI Hear 智能语音安防', new_x='LMARGIN', new_y='NEXT', align='C')
pdf.set_font('ZH','', 12)
pdf.set_text_color(80, 80, 80)
pdf.cell(0, 8, '硬件接线手册', new_x='LMARGIN', new_y='NEXT', align='C')
pdf.cell(0, 8, 'STM32H743 + FreeRTOS + Edge AI', new_x='LMARGIN', new_y='NEXT', align='C')
pdf.set_text_color(0, 0, 0)
pdf.ln(10)

# ===== 一、面包板布局 =====
section_heading('一、面包板布局（共需 2 块）')

sub_heading('面包板 A：主控 + 传感器 + 执行器')
items_a = ['STM32H743VIT6 核心板 V2.0（插面包板中间，跨槽）',
           'INMP441 I2S 数字麦克风',
           'OLED 0.96英寸 128x64 SSD1306（4脚 I2C）',
           'RGB LED 模块（共阳，3色 + 公共端）',
           '有源蜂鸣器 3.3V',
           '轻触按键（2脚）']
for t in items_a: body(t)
pdf.ln(4)

sub_heading('面包板 B：通信 + 电源')
items_b = ['ESP8266-01S WiFi 模块',
           'USB-TTL 调试串口（CH340）',
           '电源分配：5V 轨 / 3.3V 轨 / GND 轨']
for t in items_b: body(t)
pdf.ln(8)

# ===== 二、完整接线表 =====
section_heading('二、完整引脚接线表')
w = [48, 48, 48, 52]
table_header(['外设', '外设引脚', 'STM32引脚', '说明'], w)

data = [
    # (device, dev_pin, stm32_pin, note)
    ('INMP441 麦克风', 'VDD',  '3.3V',   '电源正极'),
    ('',              'GND',  'GND',    '共地'),
    ('',              'WS',   'PE4',    'I2S 字选信号'),
    ('',              'SCK',  'PE5',    'I2S 位时钟'),
    ('',              'SD',   'PE6',    'I2S 音频数据'),
    ('',              'L/R',  'GND',    '接地 = 左声道'),
    ('---', '---', '---', '---'),
    ('ESP8266-01S',   'VCC+EN','3.3V', '并联后接 3.3V'),
    ('',              'GND',  'GND',    '共地'),
    ('',              'TX',   'PA1',    'UART4 接收'),
    ('',              'RX',   'PA0',    'UART4 发送'),
    ('',              'GPIO0','悬空',   '烧录时才接 GND'),
    ('',              'RST',  '悬空',   '烧录时碰 GND'),
    ('---', '---', '---', '---'),
    ('OLED 128x64',   'VCC',  '3.3V',   ''),
    ('',              'GND',  'GND',    ''),
    ('',              'SCL',  'PD0',    'I2C 时钟线'),
    ('',              'SDA',  'PD1',    'I2C 数据线'),
    ('---', '---', '---', '---'),
    ('RGB LED（共阳）','公共', '3.3V',  '最长脚接 3.3V'),
    ('',              'R红',  'PB4',    '串 220 欧电阻'),
    ('',              'G绿',  'PB5',    '串 220 欧电阻'),
    ('',              'B蓝',  'PB8',    '串 220 欧电阻'),
    ('---', '---', '---', '---'),
    ('蜂鸣器（有源）', 'VCC',  '3.3V',   ''),
    ('',              'GND',  'GND',    ''),
    ('',              'I/O',  'PB0',    'TIM3 PWM 驱动'),
    ('---', '---', '---', '---'),
    ('轻触按键',       '脚1',  'PC4',   ''),
    ('',              '脚2',  'GND',   '按下 = 低电平'),
    ('---', '---', '---', '---'),
    ('USB-TTL 串口',  'TXD',  'PA10',  '接STM32的RX'),
    ('',              'RXD',  'PA9',   '接STM32的TX'),
    ('',              'GND',  'GND',   '共地'),
]
for i, r in enumerate(data):
    if r[0] == '---':
        pdf.set_fill_color(230, 230, 230)
        for j in range(4):
            pdf.cell(w[j], 3, '', border=1, fill=True)
        pdf.ln()
    else:
        table_row(r, w, i % 2 == 0)
pdf.ln(8)

# ===== 三、电源 =====
section_heading('三、电源连接')

numbered(1, 'STM32 通过 USB 线供电（5V），或外部 5V 电源接到核心板 5V 引脚')
numbered(2, '面包板两侧分别设置 5V 轨和 3.3V 轨')
numbered(3, '所有模块的 GND 连到一起（面包板 GND 轨）')
numbered(4, '[!] ESP8266 不能从 STM32 的 3.3V 引脚取电，电流不够！请从面包板 3.3V 轨供电')
numbered(5, '面包板 3.3V → ESP8266、OLED、INMP441、RGB LED')
numbered(6, '面包板 5V → 仅 STM32 核心板')
pdf.ln(8)

# ===== 四、上电顺序 =====
pdf.set_fill_color(255, 235, 235)
pdf.set_text_color(180, 30, 30)
pdf.set_font('ZH','B', 14)
pdf.cell(0, 11, '  [!] 重要：上电顺序', new_x='LMARGIN', new_y='NEXT', fill=True)
pdf.ln(4)
pdf.set_text_color(0, 0, 0)
pdf.set_font('ZH','B', 11)
pdf.cell(0, 8, '  第一步：先给 ESP8266 上电，等待 3~5 秒连上 WiFi', new_x='LMARGIN', new_y='NEXT')
pdf.cell(0, 8, '  第二步：再给 STM32 上电（或按 RST 复位按钮）', new_x='LMARGIN', new_y='NEXT')
pdf.ln(2)
pdf.set_font('ZH','', 10)
pdf.cell(0, 7, '  原因：STM32 启动后 10 秒内必须收到 ESP8266 的 +READY 握手信号，', new_x='LMARGIN', new_y='NEXT')
pdf.cell(0, 7, '  否则初始化超时。先让 ESP8266 连好 WiFi，再启动 STM32。', new_x='LMARGIN', new_y='NEXT')
pdf.ln(8)

# ===== 五、ESP8266 烧录 =====
section_heading('五、ESP8266 烧录步骤')

numbered(1, 'ESP8266 接到 USB-TTL：VCC→3.3V, GND→GND, TX→RXD, RX→TXD')
numbered(2, 'GPIO0 接 GND（进入下载模式）')
numbered(3, 'RST 引脚碰一下 GND 然后松开（复位）')
numbered(4, '在 VS Code 中打开 esp8266_firmware 文件夹')
numbered(5, '点击底部 PlatformIO → Upload 开始烧录')
numbered(6, '烧录完成后拔掉 GPIO0 上的 GND 线')
numbered(7, '恢复 ESP8266 到正常接线（PA1/PA0）')
numbered(8, '给 ESP8266 重新上电')
pdf.ln(8)

# ===== 六、验证清单 =====
section_heading('六、通电验证清单')

checks = [
    'OLED 屏幕显示 "AI HEAR" 字样',
    '串口助手（115200 波特率）有启动日志 [TFLM] [MQTT] 等',
    '安静时 RGB LED 蓝色慢闪（布防中）',
    '拍桌子/敲击 → RGB 红灯急闪 + 蜂鸣器响 + OLED 显示 ALERT',
    '按按键 → OLED 在 DISARMED 和 ARMED 之间切换',
    '串口显示 [WiFiIoT] RX: +PUBACK → MQTT 连通',
    '浏览器打开 http://localhost:5000 有告警记录',
    '手机微信收到 WxPusher 公众号告警推送',
]
for t in checks:
    pdf.set_font('ZH','', 10)
    pdf.cell(0, 7, f'  [ ]  {t}', new_x='LMARGIN', new_y='NEXT')

# 保存
desktop = os.path.join(os.path.expanduser('~'), 'Desktop')
path = os.path.join(desktop, 'AI_Hear_接线手册.pdf')
pdf.output(path)
print(f'已保存: {path}')
