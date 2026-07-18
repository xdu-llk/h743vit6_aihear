"""Prepare KWS 3-class data: augment help -> cache features."""
import numpy as np, librosa, os, glob, random, soundfile as sf
from tqdm import tqdm

SR, N_MELS, FRAMES, N_FFT, HOP = 16000, 40, 96, 512, 160
CACHE_DIR = 'feature_cache_kws'
AUG_DIR = 'data/help_kws_aug'
SPEECH_AUG_DIR = 'data/other_speech_aug'
AUG_PER = 10
SPEECH_AUG_PER = 1  # Real speech is diverse enough, light aug
BG_COUNT = 15000

_bg_cache = []
def _load_bg():
    global _bg_cache
    if _bg_cache: return
    for f in tqdm(sorted(glob.glob('data/other/*.wav'))[:100], desc='bg'):
        try:
            y, _ = librosa.load(f, sr=SR, mono=True)
            if len(y) >= SR: _bg_cache.append(y)
        except: pass

def mix_bg(y):
    if not _bg_cache: return y
    snr = random.uniform(20, 35)
    bg = random.choice(_bg_cache)
    if len(bg) > len(y): bg = bg[random.randint(0,len(bg)-len(y)):][:len(y)]
    else: bg = np.pad(bg,(0,len(y)-len(bg)))
    rs = np.sqrt(np.mean(y**2)+1e-10); rn = np.sqrt(np.mean(bg**2)+1e-10)
    return np.clip(y + bg*(rs/(10**(snr/20)))/(rn+1e-10), -1, 1)

def aggressive_urgent(y):
    """Transform calm speech → urgent shouting: higher pitch, faster, louder, strained."""
    # Pitch: raise 4-10 semitones (vs normal ±3) for tense/urgent voice
    y = librosa.effects.pitch_shift(y=y, sr=SR, n_steps=random.uniform(4, 10))
    # Speed: 1.1-1.35x = more rushed/urgent
    rate = random.uniform(1.1, 1.35)
    y = librosa.effects.time_stretch(y=y, rate=rate)
    if len(y) < SR: y = np.pad(y, (0, SR-len(y)))
    else: y = y[:SR]
    # Volume boost: simulate shouting loudness
    y = y * random.uniform(1.3, 2.5)
    y = np.clip(y, -1, 1)
    return y

def light_reverb(y):
    d = int(random.uniform(0.05,0.12)*SR); dec = random.uniform(0.05,0.12)
    if d < len(y): e = np.zeros_like(y); e[d:] = y[:-d]*dec; y = y+e
    p = np.max(np.abs(y)); return y*(0.95/p) if p>0.01 else y

def pitch_shift(y):
    return librosa.effects.pitch_shift(y=y, sr=SR, n_steps=random.uniform(-3,3))

def time_stretch(y):
    r = random.uniform(0.85,1.15)
    ys = librosa.effects.time_stretch(y=y, rate=r)
    return np.pad(ys,(0,len(y)-len(ys))) if len(ys)<len(y) else ys[:len(y)]

def distance(y):
    from scipy import signal as sg
    nyq = SR/2; cutoff = random.uniform(3500,6000)
    if cutoff < nyq-10:
        b,a = sg.butter(1, cutoff/nyq, 'low'); y = sg.lfilter(b,a,y)
    return np.clip(y*random.uniform(0.7,0.9), -1, 1)

def slice_long_audio(y, is_human_voice=False):
    """Slice long audio into 1s windows at RMS peaks. Returns list of 1s chunks.
    If is_human_voice=True, apply centroid/flatness quality checks for voice."""
    MAX_SAMPLES = SR * 600  # 10 minutes max
    if len(y) > MAX_SAMPLES: y = y[:MAX_SAMPLES]  # truncate
    if len(y) <= SR + SR//4:  # ~1.25s or less, just center it
        if len(y) < SR: y = np.pad(y, (0, SR-len(y)))
        else: y = y[(len(y)-SR)//2:(len(y)-SR)//2+SR]
        return [y]
    chunks = []
    hop = int(0.05 * SR)
    rms = librosa.feature.rms(y=y, frame_length=int(0.1*SR), hop_length=hop)[0]
    thresh = np.percentile(rms, 70)
    peaks = []
    for i in range(1, len(rms)-1):
        if rms[i] > thresh and rms[i] > rms[i-1] and rms[i] >= rms[i+1]:
            center = i * hop + hop // 2
            if SR//2 <= center <= len(y) - SR//2:
                peaks.append((center, rms[i]))
    peaks.sort(key=lambda x: -x[1])
    kept = []
    for center, val in peaks:
        if kept and min(abs(center-c) for c,_ in kept) < SR//2: continue
        kept.append((center, val))
    for center, _ in kept:
        chunk = y[center-SR//2:center+SR//2].astype(float)
        rms = np.sqrt(np.mean(chunk**2))
        if rms < 0.005: continue
        if is_human_voice:
            centroid = librosa.feature.spectral_centroid(y=chunk, sr=SR)[0].mean()
            if centroid < 800: continue
            flatness = librosa.feature.spectral_flatness(y=chunk)[0].mean()
            if flatness > 0.5: continue
        chunks.append(chunk)
    if not chunks:  # fallback
        c = y[(len(y)-SR)//2:(len(y)-SR)//2+SR].astype(float)
        if np.sqrt(np.mean(c**2)) >= 0.005:
            chunks.append(c)
    return chunks

def augment_one(y):
    effects = [(mix_bg,0.7),(light_reverb,0.5),(pitch_shift,0.5),(time_stretch,0.4),(distance,0.4)]
    random.shuffle(effects)
    for f,p in effects:
        if random.random()<p:
            try: y = f(y)
            except: pass
    pk = np.max(np.abs(y)); return (y*(0.95/pk)).astype(np.float32) if pk>0.01 else y.astype(np.float32)

def extract_mel(wav_path):
    y, _ = librosa.load(wav_path, sr=SR, mono=True)
    if len(y) < SR: y = np.pad(y, (0, SR-len(y)))
    else: y = y[:SR]
    mel = librosa.feature.melspectrogram(y=y, sr=SR, n_fft=N_FFT, hop_length=HOP, n_mels=N_MELS, fmin=20, fmax=8000, window='hann', center=False)
    lm = librosa.power_to_db(mel, ref=np.max)
    lm = lm[:,:FRAMES] if lm.shape[1]>=FRAMES else np.pad(lm,((0,0),(0,FRAMES-lm.shape[1])))
    return ((lm - lm.mean())/(lm.std()+1e-6)).astype(np.float32)

def cache_cls(cls_name, files, aug_dir):
    out = os.path.join(CACHE_DIR, cls_name); os.makedirs(out, exist_ok=True)
    all_files = list(files)
    if aug_dir: all_files.extend(sorted(glob.glob(os.path.join(aug_dir, '*.wav'))))
    for f in tqdm(all_files, desc=f'cache {cls_name}'):
        name = os.path.splitext(os.path.basename(f))[0]
        try: feat = extract_mel(f); np.save(os.path.join(out, f'{name}.npy'), feat)
        except: pass

def main():
    import shutil
    shutil.rmtree(AUG_DIR, ignore_errors=True); shutil.rmtree(SPEECH_AUG_DIR, ignore_errors=True)
    os.makedirs(CACHE_DIR, exist_ok=True); _load_bg()

    # Class 2: help_call
    help_files = []
    for d in sorted(os.listdir('data/help_kws')):
        sub = os.path.join('data/help_kws', d)
        if os.path.isdir(sub): help_files.extend(sorted(glob.glob(os.path.join(sub, '*.wav'))))
    print(f'Class 2 (help_call): {len(help_files)} real recordings')
    # Slice long files into 1s windows, save to temp dir for caching
    SLICE_DIR = 'data/help_kws_sliced'
    os.makedirs(SLICE_DIR, exist_ok=True)
    for old in glob.glob(os.path.join(SLICE_DIR, '*.wav')): os.remove(old)
    all_slices = []
    for f in tqdm(help_files, desc='slice help'):
        try: y, _ = librosa.load(f, sr=SR, mono=True)
        except: continue
        chunks = slice_long_audio(y.astype(np.float64), is_human_voice=True)
        for ch in chunks: all_slices.append(ch)
    print(f'  Sliced {len(all_slices)} 1s windows from {len(help_files)} files')
    # Save slices with aggressive "urgent" augmentation to simulate real shouting
    for i, ch in enumerate(all_slices):
        pk = np.max(np.abs(ch))
        if pk > 0.01: ch = ch * (0.95/pk)
        # Original slice
        sf.write(os.path.join(SLICE_DIR, f'slice_{i:05d}_orig.wav'), ch.astype(np.float32), SR, subtype='PCM_16')
        # 4 aggressive variants per slice: pitch-up + speed-up + volume
        for v in range(4):
            try:
                urgent = aggressive_urgent(ch.copy().astype(np.float64))
                sf.write(os.path.join(SLICE_DIR, f'slice_{i:05d}_urg{v}.wav'), urgent, SR, subtype='PCM_16')
            except: pass
    slice_files = sorted(glob.glob(os.path.join(SLICE_DIR, '*.wav')))
    cache_cls('help_call', slice_files, None)

    # Class 0: background — slice + quality, same as help_call
    BG_SLICE_DIR = 'data/other_sliced'
    os.makedirs(BG_SLICE_DIR, exist_ok=True)
    for old in glob.glob(os.path.join(BG_SLICE_DIR, '*.wav')): os.remove(old)
    print(f'\nClass 0 (background): slicing all files from data/other/')
    other_files = sorted(glob.glob('data/other/*.wav'))
    bg_slices = []
    for f in tqdm(other_files, desc='slice bg'):
        try: y, _ = librosa.load(f, sr=SR, mono=True)
        except: continue
        chunks = slice_long_audio(y.astype(np.float64))
        for ch in chunks: bg_slices.append(ch)
    print(f'  Sliced {len(bg_slices)} 1s windows from {len(other_files)} files')
    for i, ch in enumerate(bg_slices):
        pk = np.max(np.abs(ch))
        if pk > 0.01: ch = ch * (0.95/pk)
        sf.write(os.path.join(BG_SLICE_DIR, f'bg_{i:05d}.wav'), ch.astype(np.float32), SR, subtype='PCM_16')
    bg_slice_files = sorted(glob.glob(os.path.join(BG_SLICE_DIR, '*.wav')))
    cache_cls('background', bg_slice_files, None)

    total = 0
    for cls in ['background','help_call']:
        n = len(glob.glob(os.path.join(CACHE_DIR, cls, '*.npy')))
        print(f'  {cls}: {n} .npy'); total += n
    print(f'  Total: {total}\nDone! Run train_kws.py next.')

if __name__ == '__main__': main()
