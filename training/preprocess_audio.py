"""
Audio preprocessing: slice long files, filter silence/noise, keep only valid 1s segments.
Outputs clean 1s 16kHz mono WAV files.
"""
import librosa, soundfile, os, glob, numpy as np

SR = 16000
DUR = 1.0          # target 1 second
SAMPLES = int(SR * DUR)

# --- thresholds ---
RMS_MIN    = 0.01   # absolute minimum RMS (silence filter)

DATA_IN  = r'd:/stm32cubemx/projects/h743vit6_aihear/training/data'
DATA_OUT = r'd:/stm32cubemx/projects/h743vit6_aihear/training/processed'

def is_valid_baby_cry(y):
    """Baby cry: tonal vocal sound, medium ZCR, low spectral flatness"""
    if len(y) < SAMPLES // 2:
        return False, "too_short"
    rms = np.sqrt(np.mean(y ** 2))
    if rms < RMS_MIN:
        return False, f"low_rms={rms:.4f}"
    zcr = np.mean(np.abs(np.diff(np.sign(y)))) / 2
    if zcr < 0.02:
        return False, f"low_zcr={zcr:.4f}"
    spec = np.abs(np.fft.rfft(y * np.hanning(len(y))))
    spec = spec / (np.sum(spec) + 1e-10)
    flatness = np.exp(np.mean(np.log(spec + 1e-10))) / (np.mean(spec) + 1e-10)
    if flatness > 0.5:  # vocal = not flat
        return False, f"too_flat={flatness:.3f}"
    return True, f"OK rms={rms:.3f} flt={flatness:.3f}"

def is_valid_other(y):
    """Other (glass_break + normal): accept glass-like transients OR normal background.
    Relaxed: just filter silence and extremely short clips."""
    if len(y) < SAMPLES // 4:  # min 250ms
        return False, "too_short"
    rms = np.sqrt(np.mean(y ** 2))
    if rms < RMS_MIN:
        return False, f"low_rms={rms:.4f}"
    # Broadband check: reject pure tones that might confuse with baby_cry
    spec = np.abs(np.fft.rfft(y * np.hanning(len(y))))
    spec = spec / (np.sum(spec) + 1e-10)
    flatness = np.exp(np.mean(np.log(spec + 1e-10))) / (np.mean(spec) + 1e-10)
    if flatness < 0.15:  # very tonal — might be baby cry misclassification
        return False, f"too_tonal(flat={flatness:.3f})"
    return True, f"OK rms={rms:.3f} flat={flatness:.3f}"


def pad_or_trim(y, target=SAMPLES):
    """Pad or trim to exactly target samples"""
    if len(y) < target:
        return np.pad(y, (0, target - len(y)))
    return y[:target]

def process_file(filepath, cls_name, idx_counter):
    """Slice and validate a single audio file. Class-specific rules."""
    try:
        y, sr = librosa.load(filepath, sr=SR, mono=True)
    except Exception as e:
        print(f"  SKIP {os.path.basename(filepath)}: load error {e}")
        return []

    if sr != SR:
        y = librosa.resample(y, orig_sr=sr, target_sr=SR)

    total = len(y)
    duration = total / SR
    saved = []

    # Choose validator and window size per class
    if cls_name == 'baby_cry':
        validator = is_valid_baby_cry
        window_samples = SAMPLES   # 1s
    else:  # other
        validator = is_valid_other
        window_samples = SAMPLES   # 1s

    if duration <= 1.3:
        y_padded = pad_or_trim(y)
        ok, msg = validator(y_padded)
        if ok:
            out_path = os.path.join(DATA_OUT, cls_name, f"{idx_counter[0]:04d}.wav")
            soundfile.write(out_path, y_padded, SR)
            saved.append(out_path)
            idx_counter[0] += 1
        return saved

    # Long file: sliding window, overlap 50%
    hop = window_samples // 2
    windows = []
    for start in range(0, total - window_samples + 1, hop):
        chunk = y[start:start + window_samples]
        rms = np.sqrt(np.mean(chunk ** 2))
        windows.append((start, rms, chunk))

    if not windows:
        return saved

    # Keep windows above 20th percentile RMS
    rms_vals = [w[1] for w in windows]
    threshold = max(RMS_MIN, np.percentile(rms_vals, 20))

    for start, rms, chunk in windows:
        if rms < threshold:
            continue
        chunk_padded = pad_or_trim(chunk)
        ok, msg = validator(chunk_padded)
        if ok:
            if len(saved) == 0:
                print(f"  OK {os.path.basename(filepath)[:50]} ({duration:.1f}s) -> win@{start/SR:.1f}s")
            out_path = os.path.join(DATA_OUT, cls_name, f"{idx_counter[0]:04d}.wav")
            soundfile.write(out_path, chunk_padded, SR)
            saved.append(out_path)
            idx_counter[0] += 1

    return saved


def main():
    for cls in ['baby_cry', 'other']:
        os.makedirs(os.path.join(DATA_OUT, cls), exist_ok=True)

    for cls in ['baby_cry', 'other']:
        print(f"\n{'='*50}")
        print(f"Processing: {cls}")
        print(f"{'='*50}")

        files = glob.glob(os.path.join(DATA_IN, cls, '*'))
        idx = [0]
        total_saved = 0

        for f in sorted(files):
            saved = process_file(f, cls, idx)
            total_saved += len(saved)

        print(f"  -> {cls}: {total_saved} valid segments from {len(files)} source files")

if __name__ == '__main__':
    main()
