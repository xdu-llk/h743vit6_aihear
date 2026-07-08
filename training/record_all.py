"""Record training audio using PC microphone.

Usage: python record_all.py baby_cry   (shout 救命)
       python record_all.py other      (any background: glass, knock, quiet, normal)
       python record_all.py other --start 30  (continue from index 30)
"""
import sounddevice as sd
import numpy as np
import wave, os, sys, time

SR = 16000
OUT = 'data_real'
TARGET = 60

if len(sys.argv) < 2:
    print(__doc__)
    sys.exit(1)

label = sys.argv[1]
# Parse --start N
start_idx = 0
args = [a for a in sys.argv[2:] if not a.startswith('--start')]
for a in sys.argv[2:]:
    if a.startswith('--start='):
        start_idx = int(a.split('=')[1])
    elif a == '--start' and sys.argv.index(a) + 1 < len(sys.argv):
        start_idx = int(sys.argv[sys.argv.index(a) + 1])

label_map = {'baby_cry': 'baby_cry', 'other': 'other'}
if label not in label_map:
    print(f'Unknown. Use: baby_cry, other')
    sys.exit(1)

cls_dir = os.path.join(OUT, label_map[label])
os.makedirs(cls_dir, exist_ok=True)

# Auto-detect start index from existing files if not specified
if start_idx == 0:
    existing = [f for f in os.listdir(cls_dir) if f.startswith(f'{label}_') and f.endswith('.wav')]
    if existing:
        start_idx = len(existing)
        print(f'Found {start_idx} existing files, continuing from {start_idx}')

print(f'Recording {TARGET} clips for [{label}] from index {start_idx}')
print('Starting in 2 seconds...')
time.sleep(2)

recorded = 0

try:
    while recorded < TARGET:
        audio = sd.rec(SR, samplerate=SR, channels=1, dtype='float32', blocking=True).flatten()
        rms = np.sqrt(np.mean(audio**2))

        if label == 'other' and rms < 0.001:
            print(f'  skip dead_silence (rms={rms:.3f})')
            continue
        if label != 'other' and rms < 0.002:
            print(f'  skip quiet (rms={rms:.3f})')
            continue

        peak = max(abs(audio.min()), abs(audio.max()))
        scale = 32767 / peak if peak > 0 else 1.0
        audio16 = (audio * scale * 0.9).astype(np.int16)

        idx = start_idx + recorded
        fname = f'{cls_dir}/{label}_{idx:03d}.wav'
        wf = wave.open(fname, 'wb')
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SR)
        wf.writeframes(audio16.tobytes())
        wf.close()

        recorded += 1
        if recorded % 10 == 0:
            print(f'  {recorded}/{TARGET}')

except KeyboardInterrupt:
    pass

print(f'Done: {recorded} clips -> {cls_dir}/ (indices {start_idx}-{start_idx+recorded-1})')
