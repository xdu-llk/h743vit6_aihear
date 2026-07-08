"""Capture PCM dumps from STM32 serial and save as labeled .wav files.

Usage:
    python capture_pcm.py help        # Record "help" samples (shout)
    python capture_pcm.py glass       # Record "glass_break" samples (tap glass)
    python capture_pcm.py impact      # Record "impact" samples (clap/knock)
    python capture_pcm.py normal      # Record ambient background noise
"""
import serial, sys, os, wave
import numpy as np

SR = 2000  # MCU sends every 8th sample (effective 2kHz, librosa will resample)
OUTPUT_DIR = 'data'
SPAN = 2000  # 1 second decimated by 8 (MCU sends every 8th sample, ~2kHz effective)

if len(sys.argv) < 2:
    print(__doc__)
    sys.exit(1)

label = sys.argv[1]
label_map = {'help': 'help', 'glass': 'glass_break',
             'impact': 'impact', 'normal': 'normal'}
if label not in label_map:
    print(f'Unknown: {label}. Use: help, glass, impact, normal')
    sys.exit(1)

class_dir = os.path.join(OUTPUT_DIR, label_map[label])
os.makedirs(class_dir, exist_ok=True)

# Find STLink serial port
import serial.tools.list_ports
ports = list(serial.tools.list_ports.comports())
port = None
for p in ports:
    if 'STLink' in p.description or 'STMicro' in p.description:
        port = p.device
        break
if not port and ports:
    port = ports[0].device
if not port:
    print('No serial port found!')
    sys.exit(1)

print(f'Port: {port}')
print(f'Label: {label} -> {class_dir}/')
print('Shows all device output. When [PCM] appears, saves .wav automatically.')
print('Ctrl+C to stop.')
print('-' * 60)

ser = serial.Serial(port, baudrate=115200, timeout=1)

existing = len([f for f in os.listdir(class_dir) if f.endswith('.wav')])
saved = 0

try:
    while True:
        line = ser.readline().decode('utf-8', errors='ignore').rstrip()
        if not line:
            continue

        # Show ALL device output
        if line != '[PCM]':
            print(line, flush=True)
            continue

        # Read the PCM data line (5472 int32 values, all on one line)
        chunks = []
        while True:
            c = ser.read(1)
            if not c or c == b'\n':
                break
            chunks.append(c)
        data_line = b''.join(chunks).decode('utf-8', errors='ignore').strip()

        end_line = ser.readline().decode('utf-8', errors='ignore').rstrip()
        if end_line != '[/PCM]':
            print(f'!! Expected [/PCM], got: {end_line}')
            continue

        try:
            values = [int(x) for x in data_line.split()]
        except ValueError:
            print(f'!! Parse error ({len(data_line)} chars)')
            continue

        if len(values) != SPAN:
            print(f'!! Expected {SPAN} samples, got {len(values)}')
            continue

        pcm = np.array(values, dtype=np.int32)

        # Scale to int16 with headroom, preserving relative dynamics.
        # Peak maps to ~80% of max to avoid hard clipping.
        peak = max(abs(pcm.min()), abs(pcm.max()))
        if peak > 0:
            pcm16 = (pcm.astype(np.float64) * (26000.0 / peak)).astype(np.int16)
        else:
            pcm16 = pcm.astype(np.int16)

        fname = f'{class_dir}/{label}_{existing + saved:04d}.wav'
        wf = wave.open(fname, 'wb')
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SR)
        wf.writeframes(pcm16.tobytes())
        wf.close()

        saved += 1
        rms = np.sqrt(np.mean(pcm16.astype(np.float64)**2))
        vmax = max(abs(pcm16.min()), abs(pcm16.max()))
        print(f'>>> SAVED [{saved}] {fname} (rms={rms:.0f}, peak={vmax})', flush=True)

except KeyboardInterrupt:
    print(f'\nDone. {saved} samples -> {class_dir}/ (total: {existing + saved})')
finally:
    ser.close()
