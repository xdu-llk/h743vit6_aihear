"""Subtle augmentation on already-split 1s baby_cry chunks.

Reads from processed/baby_cry/, applies light real-world effects,
writes augmented copies alongside originals. Keeps it realistic.
"""
import numpy as np, librosa, os, glob, random, soundfile as sf
from tqdm import tqdm
from scipy import signal as scipy_signal

SR = 16000
IN_DIR = 'processed/baby_cry'       # already-split 1s chunks
OUT_DIR = 'processed/baby_cry_aug'  # augmented output (separate dir)
BG_DIR = 'data/other'

AUGMENT_PER_SAMPLE = 1  # 1 augmented copy per original → 2× total

_bg_cache = []

def _load_bg_cache():
    global _bg_cache
    if _bg_cache: return
    bg_files = sorted(glob.glob(os.path.join(BG_DIR, 'bg_*.wav')))
    if not bg_files: bg_files = sorted(glob.glob(os.path.join(BG_DIR, 'normal_*.wav')))
    if not bg_files: bg_files = sorted(glob.glob(os.path.join(BG_DIR, '*.wav')))[:100]
    print(f"  Loading {len(bg_files)} background noise files...")
    for f in tqdm(bg_files, desc='  bg cache'):
        try:
            y, _ = librosa.load(f, sr=SR, mono=True)
            if len(y) >= SR: _bg_cache.append(y)
        except Exception: pass
    print(f"  → {len(_bg_cache)} usable backgrounds")


def mix_background(y):
    """Very light background — baby room is quiet. SNR 20-35dB."""
    if not _bg_cache: return y
    snr_db = random.uniform(20, 35)
    bg = random.choice(_bg_cache)
    if len(bg) > len(y): start = random.randint(0, len(bg)-len(y)); bg_seg = bg[start:start+len(y)]
    else: bg_seg = np.pad(bg, (0, len(y)-len(bg)))
    rms_s = np.sqrt(np.mean(y**2)+1e-10); rms_n = np.sqrt(np.mean(bg_seg**2)+1e-10)
    bg_seg *= (rms_s/(10**(snr_db/20)))/(rms_n+1e-10)
    return np.clip(y+bg_seg, -1, 1)


def apply_light_reverb(y):
    """Very light room reflection — nursery with soft furnishings."""
    delay = int(random.uniform(0.05, 0.12) * SR)
    decay = random.uniform(0.05, 0.12)
    if delay < len(y):
        echo = np.zeros_like(y)
        echo[delay:] = y[:-delay] * decay
        y = y + echo
        peak = np.max(np.abs(y))
        if peak > 0.01: y *= 0.95 / peak
    return y


def apply_subtle_pitch(y):
    """Natural baby voice variation (±3 semitones)."""
    steps = random.uniform(-3.0, 3.0)
    return librosa.effects.pitch_shift(y=y, sr=SR, n_steps=steps)


def apply_time_stretch(y):
    """Vary crying speed (±15%) — natural urgency variation."""
    rate = random.uniform(0.85, 1.15)
    ys = librosa.effects.time_stretch(y=y, rate=rate)
    if len(ys) < len(y): ys = np.pad(ys, (0, len(y)-len(ys)))
    else: ys = ys[:len(y)]
    return ys


def apply_distance(y):
    """Simulate mic at 1-3m: gentle high-freq rolloff + slight volume drop."""
    nyq = SR / 2
    cutoff = random.uniform(3500, 6000)
    if cutoff < nyq - 10:
        b, a = scipy_signal.butter(1, cutoff/nyq, btype='low')
        y = scipy_signal.lfilter(b, a, y)
    gain = random.uniform(0.7, 0.9)
    return np.clip(y * gain, -1, 1)


def augment_one(y):
    """Apply 2-3 subtle effects in random order."""
    effects = [
        (mix_background, 0.7),      # usually — room ambience
        (apply_light_reverb, 0.5),  # sometimes — room reflection
        (apply_subtle_pitch, 0.5),  # sometimes — vocal range variation
        (apply_time_stretch, 0.4),  # sometimes — crying speed
        (apply_distance, 0.4),      # sometimes — mic distance
    ]
    random.shuffle(effects)
    for func, prob in effects:
        if random.random() < prob:
            try: y = func(y)
            except Exception: pass

    peak = np.max(np.abs(y))
    if peak > 0.01: y *= 0.95 / peak
    return y.astype(np.float32)


def main():
    _load_bg_cache()

    files = sorted(glob.glob(os.path.join(IN_DIR, '*.wav')))
    print(f"\nSource: {len(files)} 1s baby_cry chunks in {IN_DIR}/")
    print(f"Augment: ×{AUGMENT_PER_SAMPLE} per chunk → ~{len(files) * AUGMENT_PER_SAMPLE} augmented")

    if os.path.exists(OUT_DIR):
        import shutil; shutil.rmtree(OUT_DIR)
    os.makedirs(OUT_DIR)

    idx = 0
    for f in tqdm(files, desc='Augment baby_cry chunks'):
        try:
            y, _ = sf.read(f)
            if len(y) < int(0.3 * SR): continue
            if len(y) != SR:  # ensure exactly 1s
                if len(y) < SR: y = np.pad(y, (0, SR-len(y)))
                else: y = y[:SR]
        except Exception: continue

        for _ in range(AUGMENT_PER_SAMPLE):
            try:
                ya = augment_one(y.copy().astype(np.float64))
                sf.write(os.path.join(OUT_DIR, f'babyaug_{idx:05d}.wav'), ya, SR, subtype='PCM_16')
                idx += 1
            except Exception: pass

    print(f"\nDone: {idx} augmented chunks in {OUT_DIR}/")
    print(f"  Original: {len(files)}, Augmented: {idx}")
    print(f"  → Combined: {len(files) + idx} total for training")


if __name__ == '__main__':
    main()
