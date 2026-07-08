"""Record ambient room noise using sounddevice, save 1s WAVs for training."""
import sounddevice as sd
import numpy as np
import wave, os, time

SR = 16000
DURATION_SEC = 1
OUTPUT_DIR = 'data/other'
MINUTES = 10  # Record 10 minutes of ambient room noise

os.makedirs(OUTPUT_DIR, exist_ok=True)

existing = len([f for f in os.listdir(OUTPUT_DIR) if f.startswith('ambient_')])
target = MINUTES * 60

print(f'Already have {existing} ambient files')
print(f'Target: {MINUTES} min = {target} clips')
print()
print('Starting in 3 seconds...')
print('PLEASE BE QUIET — no talking, no keyboard, no phone')
time.sleep(3)

recorded = 0
try:
    while recorded < target:
        # Record 1 second
        data = sd.rec(int(SR * DURATION_SEC), samplerate=SR, channels=1,
                      dtype='float32', blocking=True)
        data = data.flatten()

        # Skip loud clips (talking, banging, etc.)
        rms = np.sqrt(np.mean(data**2))
        if rms > 0.03:  # Skip anything above noise floor
            print(f'  Skip loud (RMS={rms:.4f})')
            continue

        # Convert to int16 for WAV
        int16_data = (data * 32767).astype(np.int16)

        fname = f'{OUTPUT_DIR}/ambient_{existing + recorded:04d}.wav'
        wf = wave.open(fname, 'wb')
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SR)
        wf.writeframes(int16_data.tobytes())
        wf.close()

        recorded += 1
        if recorded % 50 == 0:
            print(f'  {recorded}/{target} ({100*recorded//target}%)  [{recorded//60}m{recorded%60:02d}s]')

except KeyboardInterrupt:
    print(f'\nInterrupted at {recorded} clips')

print(f'\nDone: {recorded} new ambient clips')
print(f'Total ambient files in data/other/: {existing + recorded}')
