"""Extract and cache features once. Subsequent training loads .npy instead of WAV."""
import numpy as np, librosa, os, glob
from tqdm import tqdm

SR, N_MELS, FRAMES, N_FFT, HOP = 16000, 40, 96, 512, 160
DATA = 'processed'
CACHE = 'feature_cache'

def extract(wav_path):
    y, _ = librosa.load(wav_path, sr=SR, mono=True)
    if len(y) < SR: y = np.pad(y, (0, SR - len(y)))
    else: y = y[:SR]
    mel = librosa.feature.melspectrogram(y=y, sr=SR, n_fft=N_FFT, hop_length=HOP,
            n_mels=N_MELS, fmin=20, fmax=8000, window='hann')
    lm = librosa.power_to_db(mel, ref=np.max)
    if lm.shape[1] < FRAMES: lm = np.pad(lm, ((0,0),(0,FRAMES-lm.shape[1])))
    else: lm = lm[:,:FRAMES]
    lm = (lm - lm.mean()) / (lm.std() + 1e-6)
    return lm.astype(np.float32)

for cls in ['baby_cry', 'other']:
    os.makedirs(f'{CACHE}/{cls}', exist_ok=True)
    files = sorted(glob.glob(f'{DATA}/{cls}/*.wav'))
    for f in tqdm(files, desc=cls):
        name = os.path.splitext(os.path.basename(f))[0]
        np.save(f'{CACHE}/{cls}/{name}.npy', extract(f))
print('Done!')
