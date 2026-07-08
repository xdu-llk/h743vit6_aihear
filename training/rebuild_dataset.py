"""Rebuild AI Hear dataset v4 — 2-class: baby_cry vs other.

baby_cry:   ONLY Chinese "救命" (TTS 300 variants)
other:      ESC-50 (glass_breaking + door_wood_knock + clapping) + Speech Commands + background noise
"""
import os, csv, glob, shutil, random, re
import numpy as np, librosa, soundfile as sf
import asyncio, edge_tts

SR = 16000
SEED = 42
random.seed(SEED); np.random.seed(SEED)

DATA = 'data'
ESC50 = 'ESC-50-master'

# ====== Step 0: Clear ======
print('[0] Clearing old data...')
for cls in ['baby_cry','other']:
    d = f'{DATA}/{cls}'
    os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        if f.endswith('.wav'): os.remove(f'{d}/{f}')

# ====== Step 1: TTS "救命" × 300 ======
print('[1] Generating TTS 救命 × 300...')
VOICES = [
    'zh-CN-XiaoxiaoNeural','zh-CN-XiaoyiNeural','zh-CN-YunxiNeural',
    'zh-CN-YunyangNeural','zh-CN-XiaochenNeural','zh-CN-XiaohanNeural',
    'zh-CN-XiaomengNeural','zh-CN-XiaomoNeural','zh-CN-XiaoxuanNeural',
]
RATES = ['-40%','-30%','-20%','-10%','+0%','+10%','+20%','+30%','+40%']
PITCHES = ['-15Hz','-10Hz','-5Hz','+0Hz','+5Hz','+10Hz','+15Hz','+20Hz']
TEXTS = ['救命！','救命啊！','快来人救命！','救命救命！','救命啊救命！','来人啊救命！']

async def gen_tts():
    count = 0
    for voice in VOICES:
        for rate in RATES:
            for pitch in PITCHES:
                if count >= 300: break
                text = TEXTS[count % len(TEXTS)]
                mp3 = f'{DATA}/baby_cry/_tmp_{count}.mp3'
                try:
                    comm = edge_tts.Communicate(text, voice, rate=rate, pitch=pitch)
                    await comm.save(mp3)
                    y, _ = librosa.load(mp3, sr=SR, mono=True)
                    os.remove(mp3)
                    if len(y) < SR: y = np.pad(y, (0, SR-len(y)))
                    else: y = y[:SR]
                    if np.sqrt(np.mean(y**2)) > 0.001:
                        sf.write(f'{DATA}/baby_cry/tts_{count:03d}.wav', y, SR)
                        count += 1
                except: pass
            if count >= 300: break
        if count >= 300: break
    return count

n_tts = asyncio.run(gen_tts())
print(f'  baby_cry: {n_tts} TTS files')

# ====== Step 2: ESC-50 aggressive peak extraction ======
print('[2] ESC-50 10-window extraction...')
MAPPING = {
    'glass_breaking': 'other',
    'door_wood_knock': 'other',
    'clapping': 'other',
}

def extract_windows(y, n_windows=10):
    """Extract n_windows 1-second clips spread across the audio."""
    win_len = SR
    if len(y) <= win_len:
        if len(y) < win_len: y = np.pad(y, (0, win_len - len(y)))
        return [y]

    step = max(1, (len(y) - win_len) // (n_windows - 1)) if n_windows > 1 else 0
    windows = []
    for i in range(n_windows):
        start = min(i * step, len(y) - win_len)
        windows.append(y[start:start + win_len])
    return windows

with open(f'{ESC50}/meta/esc50.csv') as f:
    for row in csv.DictReader(f):
        cat = row['category']
        if cat not in MAPPING: continue
        target = MAPPING[cat]
        y, _ = librosa.load(f'{ESC50}/audio/{row["filename"]}', sr=SR, mono=True)
        base = os.path.splitext(row['filename'])[0]
        for wi, win in enumerate(extract_windows(y, 10)):
            rms = np.sqrt(np.mean(win**2))
            if rms < 0.003: continue
            sf.write(f'{DATA}/{target}/{base}_w{wi:02d}.wav', win, SR)

# Report ESC-50 extraction
n_esc50 = len([f for f in os.listdir(f'{DATA}/other') if not f.startswith('tts_') and not f.startswith('sc_') and not f.startswith('bg_')])
print(f'  other: {n_esc50} ESC-50 files')

# ====== Step 3: Speech Commands → other ======
print('[3] Speech Commands → other (sampling)...')
SC_WORDS = ['yes','no','left','right','go','stop','one','two','three',
            'bed','bird','dog','cat','tree','wow','sheila','marvin',
            'eight','nine','seven','up','down','on','off','four','five']
added = 0
for word in SC_WORDS:
    if not os.path.isdir(word): continue
    wavs = glob.glob(f'{word}/*.wav')
    random.shuffle(wavs)
    for w in wavs[:40]:  # 40 per word
        dst = f'{DATA}/other/sc_{word}_{os.path.basename(w)}'
        if not os.path.exists(dst):
            y, _ = librosa.load(w, sr=SR, mono=True)
            if len(y) < SR: y = np.pad(y, (0, SR-len(y)))
            else: y = y[:SR]
            if np.sqrt(np.mean(y**2)) > 0.001:
                sf.write(dst, y, SR); added += 1
print(f'  other: +{added} Speech Commands')

# ====== Step 4: Background noise → other ======
print('[4] Background noise...')
bg = 0
for fn in os.listdir('_background_noise_'):
    if not fn.endswith('.wav'): continue
    y, _ = librosa.load(f'_background_noise_/{fn}', sr=SR, mono=True)
    for start in range(0, len(y) - SR, SR):
        chunk = y[start:start+SR]
        if np.sqrt(np.mean(chunk**2)) < 0.001: continue
        sf.write(f'{DATA}/other/bg_{fn}_{start//SR}.wav', chunk, SR); bg += 1
print(f'  other: +{bg} background noise')

# ====== Step 5: Augment baby_cry (minority class) ======
print('[5] Online-style augmentation for baby_cry...')
def augment_file(src, dst_base, n_variants):
    y, _ = librosa.load(src, sr=SR, mono=True)
    if len(y) < SR: y = np.pad(y, (0, SR-len(y)))
    else: y = y[:SR]
    count = 0
    rng = np.random.RandomState(hash(os.path.basename(src)) % 2**31)
    for vi in range(n_variants):
        ya = y.copy()
        # Random augmentation chain
        if rng.random() < 0.5:  # pitch shift
            steps = rng.uniform(-2, 2)
            try: ya = librosa.effects.pitch_shift(y=ya, sr=SR, n_steps=steps)
            except: pass
        if rng.random() < 0.5:  # noise
            snr = rng.uniform(20, 35)
            rms = np.sqrt(np.mean(ya**2) + 1e-10)
            ya = ya + rng.randn(len(ya)).astype(np.float32) * (rms / (10**(snr/20)))
        if rng.random() < 0.4:  # time stretch
            rate = rng.uniform(0.9, 1.1)
            try:
                ya = librosa.effects.time_stretch(y=ya, rate=rate)
                if len(ya) < SR: ya = np.pad(ya, (0, SR-len(ya)))
                else: ya = ya[:SR]
            except: pass
        if rng.random() < 0.5:  # volume
            ya = ya * rng.uniform(0.7, 1.3)
        ya = np.clip(ya, -1, 1)
        out = f'{dst_base}_aug{vi:02d}.wav'
        if not os.path.exists(out):
            sf.write(out, ya, SR); count += 1
    return count

for cls, n_aug in [('baby_cry', 5)]:
    files = [f for f in glob.glob(f'{DATA}/{cls}/*.wav') if '_aug' not in f and not f.startswith(f'{DATA}/{cls}/tts_')]
    added = 0
    for f in files:
        base = f.replace('.wav', '')
        added += augment_file(f, base, n_aug)
    print(f'  {cls}: +{added} augmented')

# ====== Final report ======
print()
print('='*50)
print('FINAL DATASET:')
total = 0
for cls in ['baby_cry','other']:
    n = len([f for f in os.listdir(f'{DATA}/{cls}') if f.endswith('.wav')])
    total += n
    # Quick quality check
    silent = 0
    for f in os.listdir(f'{DATA}/{cls}'):
        if not f.endswith('.wav'): continue
        y, _ = librosa.load(f'{DATA}/{cls}/{f}', sr=SR, mono=True)
        if np.sqrt(np.mean(y**2)) < 0.001: silent += 1
    print(f'  {cls:15s}: {n:5d}  (silent: {silent})')
print(f'  {"TOTAL":15s}: {total:5d}')
print('='*50)
