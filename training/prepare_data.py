"""Prepare audio dataset for 2-class training: baby_cry vs other (ESC-50 + Speech Commands + TTS)."""
import os, shutil, csv, glob, random

random.seed(42)
DATA_DIR = 'data'

for cls in ['baby_cry','other']:
    os.makedirs(f'{DATA_DIR}/{cls}', exist_ok=True)

# === ESC-50 ===
MAPPING = {
    'crying_baby':     'baby_cry',
    'glass_breaking':  'other',
    'clapping':        'other',
    'door_wood_knock': 'other',
    'fireworks':       'other',
    'hand_saw':        'other',
    'engine':          'other',
    'rain':            'other',
    'wind':            'other',
    'footsteps':       'other',
    'keyboard_typing': 'other',
    'vacuum_cleaner':  'other',
}

with open('ESC-50-master/meta/esc50.csv') as f:
    for row in csv.DictReader(f):
        cat = row['category']
        if cat in MAPPING:
            cls = MAPPING[cat]
            src = os.path.join('ESC-50-master/audio', row['filename'])
            dst = os.path.join(DATA_DIR, cls, os.path.basename(row['filename']))
            if not os.path.exists(dst):
                shutil.copy2(src, dst)

# === Speech Commands v2 → other (spoken words as background speech) ===
SC_WORDS = ['yes', 'no', 'up', 'down', 'left', 'right',
            'on', 'off', 'stop', 'go', 'one', 'two',
            'three', 'four', 'five', 'bed', 'bird', 'dog',
            'cat', 'tree', 'wow', 'sheila', 'marvin']
n_added = 0
for word in SC_WORDS:
    wavs = glob.glob(f'{word}/*.wav')
    random.shuffle(wavs)
    for w in wavs[:40]:  # max 40 per word to keep balance
        dst = os.path.join(DATA_DIR, 'other', f'sc_{word}_{os.path.basename(w)}')
        if not os.path.exists(dst):
            shutil.copy2(w, dst)
            n_added += 1
print(f'Speech Commands → normal: {n_added} files')

# === Background noise as other ===
bg_dir = '_background_noise_'
if os.path.exists(bg_dir):
    for w in glob.glob(f'{bg_dir}/*.wav'):  # long background recordings
        dst = os.path.join(DATA_DIR, 'other', f'bg_{os.path.basename(w)}')
        if not os.path.exists(dst):
            shutil.copy2(w, dst)
            n_added += 1

# === TTS baby_cry already in data/baby_cry/ ===

# Report
print('\nFinal dataset:')
for cls in ['baby_cry','other']:
    n = len(os.listdir(f'{DATA_DIR}/{cls}'))
    print(f'  {cls}: {n} files')
