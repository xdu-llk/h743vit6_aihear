"""Audio data augmentation: pitch shift, noise, time stretch, volume."""
import librosa, numpy as np, os, glob, random

SR = 16000
DATA_ROOT = 'data'
random.seed(42)
np.random.seed(42)

def augment(y, sr=SR):
    """Apply random augmentation chain, return list of augmented versions."""
    results = []

    # 1. Pitch shift (±3 semitones)
    for semitones in [-3, -1.5, 1.5, 3]:
        y2 = librosa.effects.pitch_shift(y=y, sr=sr, n_steps=semitones)
        results.append(y2)

    # 2. Add Gaussian noise (SNR 20dB)
    for snr in [25, 20]:
        rms = np.sqrt(np.mean(y**2))
        noise_rms = rms / (10**(snr/20))
        noise = np.random.randn(len(y)) * noise_rms
        results.append(y + noise)

    # 3. Time stretch (0.8x, 1.2x)
    for rate in [0.85, 1.15]:
        y2 = librosa.effects.time_stretch(y=y, rate=rate)
        if len(y2) < sr:
            y2 = np.pad(y2, (0, sr - len(y2)))
        else:
            y2 = y2[:sr]
        results.append(y2)

    # 4. Volume change
    for gain in [0.7, 1.4]:
        results.append(y * gain)

    return results

classes = ['baby_cry','other']
for cls in classes:
    files = glob.glob(f'{DATA_ROOT}/{cls}/*.wav')
    # Skip TTS files (they're already synthetic)
    tts_files = [f for f in files if 'tts_' in os.path.basename(f)]
    orig_files = [f for f in files if f not in tts_files]

    # For under-represented class, generate more augmentations
    if cls == 'baby_cry':
        max_per_file = 10
    else:
        max_per_file = 1   # other is already well-represented

    added = 0
    for f in orig_files:
        if added >= 3000:  # cap per class
            break
        y, _ = librosa.load(f, sr=SR, mono=True)
        if len(y) < SR:
            y = np.pad(y, (0, SR - len(y)))
        else:
            y = y[:SR]

        for aug in augment(y)[:max_per_file]:
            out_path = f.replace('.wav', f'_aug{added:04d}.wav')
            if not os.path.exists(out_path):
                import soundfile as sf
                sf.write(out_path, aug, SR)
                added += 1

    print(f'{cls}: +{added} augmented files')

print('\nAfter augmentation:')
for cls in classes:
    n = len(os.listdir(f'{DATA_ROOT}/{cls}'))
    print(f'  {cls}: {n} files')
