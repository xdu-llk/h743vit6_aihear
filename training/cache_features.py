"""Extract Mel features from 1s WAV chunks and cache as .npy for 3-class training."""
import numpy as np, librosa, os, glob
from tqdm import tqdm

SR, N_MELS, FRAMES, N_FFT, HOP = 16000, 40, 96, 512, 160
DATA = 'processed'
CACHE = 'feature_cache'
CLASSES = ['baby_cry', 'help', 'other']
# Map class name → data subdirectory (help data lives in distress_shout/)
DATA_DIRS = {'baby_cry': 'baby_cry', 'help': 'help', 'other': 'other'}


def extract(wav_path):
    y, _ = librosa.load(wav_path, sr=SR, mono=True)
    if len(y) < SR: y = np.pad(y, (0, SR - len(y)))
    else: y = y[:SR]
    mel = librosa.feature.melspectrogram(y=y, sr=SR, n_fft=N_FFT, hop_length=HOP,
            n_mels=N_MELS, fmin=20, fmax=8000, window='hann', center=False)
    lm = librosa.power_to_db(mel, ref=np.max)
    if lm.shape[1] < FRAMES: lm = np.pad(lm, ((0,0),(0,FRAMES-lm.shape[1])))
    else: lm = lm[:,:FRAMES]
    return ((lm - lm.mean())/(lm.std()+1e-6)).astype(np.float32)


def main():
    total = 0
    for cls in CLASSES:
        data_dir = os.path.join(DATA, DATA_DIRS.get(cls, cls))
        files = sorted(glob.glob(os.path.join(data_dir, '*.wav')))
        if not files:
            print(f"  [SKIP] {cls} ({data_dir}): no chunks found")
            continue
        cache_dir = os.path.join(CACHE, cls)
        os.makedirs(cache_dir, exist_ok=True)
        print(f"\n{cls} ({data_dir}): {len(files)} chunks")
        for f in tqdm(files, desc=cls):
            try:
                feat = extract(f)
                name = os.path.splitext(os.path.basename(f))[0]
                np.save(os.path.join(cache_dir, f'{name}.npy'), feat)
            except Exception as e:
                print(f"  [ERR] {os.path.basename(f)}: {e}")
        total += len(files)
    print(f"\n{'='*50}")
    print(f"Total: {total} .npy features")
    for cls in CLASSES:
        d = os.path.join(CACHE, cls)
        if os.path.isdir(d):
            print(f"  {cls}: {len(os.listdir(d))} files")
    print("Done! Run train_dscnn.py next.")


if __name__ == '__main__':
    main()
