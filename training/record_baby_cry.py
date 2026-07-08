"""Record baby_cry audio samples for training. Press Enter to record, Ctrl+C to stop."""
import sounddevice as sd, soundfile as sf, os, time

SR = 16000
OUT = 'data/baby_cry'
os.makedirs(OUT, exist_ok=True)

# Find next file number
existing = sorted([f for f in os.listdir(OUT) if f.startswith('mycry_') and '_aug' not in f and '_caug' not in f])
idx = len(existing)

print(f"Will save to {OUT}/mycry_XXX.wav")
print("Press ENTER to record 1 second of audio (shout '救命！救命！')")
print("Ctrl+C to quit\n")

try:
    while True:
        input(f"[{idx:03d}] Press ENTER to start recording...")
        print("  RECORDING... (1 second)")
        audio = sd.rec(int(SR * 1.0), samplerate=SR, channels=1, dtype='float32')
        sd.wait()
        path = f'{OUT}/mycry_{idx:03d}.wav'
        sf.write(path, audio, SR)
        print(f"  Saved: {path}")
        idx += 1
        time.sleep(0.3)
except KeyboardInterrupt:
    print(f"\nDone! Recorded {idx - len(existing)} samples.")
