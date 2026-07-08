"""Download real-world audio datasets for AI Hear 2-class training.

Sources:
  OpenSLR SLR99 — Scream + Crying for "baby_cry" class
  FSD50K        — Glass + Impact + Door + Knock → "other" (via HuggingFace)
"""

import os, sys, glob, shutil, urllib.request, tarfile, csv

SR_TARGET = 16000
DATA_DIR = 'data'

# Ensure class dirs exist
for d in ['baby_cry', 'other']:
    os.makedirs(f'{DATA_DIR}/{d}', exist_ok=True)


# ====== Step 1: OpenSLR SLR99 → baby_cry (scream + crying) ======
def download_slr99():
    """Deeply Nonverbal Vocalization Dataset — screaming + crying classes."""
    url = 'https://www.openslr.org/resources/99/NonverbalVocalization_tgz.tar.gz'
    tgz = 'slr99.tar.gz'
    extract_dir = 'slr99_extracted'

    if os.path.exists(tgz):
        print('[SLR99] Using existing slr99.tar.gz')
    else:
        print('[SLR99] Downloading ~600MB from OpenSLR...')
        print(f'  URL: {url}')
        urllib.request.urlretrieve(url, tgz)
        print('[SLR99] Downloaded!')

    if not os.path.exists(extract_dir):
        os.makedirs(extract_dir)
        print('[SLR99] Extracting...')
        with tarfile.open(tgz, 'r:gz') as tf:
            tf.extractall(extract_dir)
        print('[SLR99] Extracted!')

    # Find label CSV files
    label_files = glob.glob(f'{extract_dir}/**/*label*.csv', recursive=True) + \
                  glob.glob(f'{extract_dir}/**/*.csv', recursive=True)
    print(f'[SLR99] Found {len(label_files)} CSV files')

    added = 0
    for lf in label_files:
        with open(lf, 'r', encoding='utf-8-sig') as f:
            reader = csv.reader(f)
            header = next(reader, None)
            print(f'  CSV: {lf}  header={header}')
            for row in reader:
                if len(row) < 2:
                    continue
                filename = row[0].strip()
                # Label might be string or int; check for scream(15)/cry(11)
                label_str = ','.join(row[1:]).lower()
                is_scream = 'scream' in label_str or '15' in row[1] if len(row) > 1 else False
                is_cry = 'cry' in label_str or '11' in row[1] if len(row) > 1 else False

                if is_scream or is_cry:
                    # Find the WAV file
                    wavs = glob.glob(f'{extract_dir}/**/{filename}*', recursive=True)
                    for wav in wavs:
                        if wav.endswith('.wav'):
                            dst = f'{DATA_DIR}/baby_cry/slr99_{os.path.basename(wav)}'
                            if not os.path.exists(dst):
                                shutil.copy2(wav, dst)
                                added += 1
    print(f'[SLR99] Added {added} scream/cry files → data/baby_cry/')


# ====== Step 2: FSD50K via HuggingFace datasets ======
def download_fsd50k():
    """Download FSD50K Glass + Impact + Door clips via HuggingFace."""
    try:
        from datasets import load_dataset
    except ImportError:
        print('[FSD50K] Installing datasets...')
        import subprocess
        subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'datasets', 'soundfile', '-q'])
        from datasets import load_dataset

    print('[FSD50K] Loading dataset from HuggingFace (this may take a while on first run)...')
    ds = load_dataset('Fhrozen/FSD50k', split='train', trust_remote_code=True,
                      streaming=True)  # streaming to avoid downloading all 24GB

    # Target FSD50K labels → other
    target_map = {
        'Glass':                     'other',
        'Shatter':                   'other',
        'Glass_breaking':            'other',
        'Impact':                    'other',
        'Door':                      'other',
        'Door_slam':                 'other',
        'Slamming_door':             'other',
        'Door_banging':              'other',
        'Knock':                     'other',
        'Door_knocking':             'other',
        'Tapping':                   'other',
        'Clapping':                  'other',
        'Hand_clapping':             'other',
        'Thump_and_thud':            'other',
        'Fireworks':                 'other',
        'Gunshot_and_gunfire':       'other',
        'Explosion':                 'other',
    }

    import numpy as np
    import soundfile as sf

    print('[FSD50K] Streaming and filtering for other class...')
    added = {'other': 0}
    max_per_class = 1000

    for item in ds:
        # Get labels (string or list)
        labels_raw = item.get('labels', '')
        if labels_raw is None:
            continue
        if isinstance(labels_raw, list):
            labels_list = labels_raw
        else:
            labels_list = str(labels_raw).split(',')

        labels_clean = [l.strip() for l in labels_list]

        matched = None
        for lbl in labels_clean:
            if lbl in target_map:
                matched = target_map[lbl]
                break

        if not matched:
            continue
        if added[matched] >= max_per_class:
            if all(v >= max_per_class for v in added.values()):
                break
            continue

        # Get audio
        audio = item.get('audio', None)
        if audio is None:
            continue
        audio_array = audio.get('array', None)
        audio_sr = audio.get('sampling_rate', 44100)
        if audio_array is None:
            continue

        # Resample to 16kHz if needed
        if audio_sr != SR_TARGET:
            try:
                import librosa
                audio_array = librosa.resample(
                    np.array(audio_array, dtype=np.float32),
                    orig_sr=audio_sr, target_sr=SR_TARGET)
            except ImportError:
                pass  # keep original sample rate

        fname = item.get('fname', f'fsd50k_{added[matched]:04d}')
        dst = f'{DATA_DIR}/{matched}/fsd50k_{fname}.wav'
        if os.path.exists(dst):
            continue

        sf.write(dst, audio_array, SR_TARGET)
        added[matched] += 1
        if added[matched] % 50 == 0:
            print(f'  {matched}: {added[matched]}/{max_per_class}')

    print(f'[FSD50K] Done: other={added["other"]}')


# ====== Main ======
if __name__ == '__main__':
    print('=== AI Hear Dataset Download ===\n')

    if '--slr99' in sys.argv:
        download_slr99()
    elif '--fsd50k' in sys.argv:
        download_fsd50k()
    else:
        print('Downloading BOTH datasets...\n')
        try:
            download_slr99()
        except Exception as e:
            print(f'\n[SLR99] Error: {e}')
            print('You can download manually from:')
            print('  https://www.openslr.org/99/')
            import traceback
            traceback.print_exc()

        print()
        try:
            download_fsd50k()
        except Exception as e:
            print(f'\n[FSD50K] Error: {e}')
            print('You can download manually from:')
            print('  https://zenodo.org/records/4060432')
            import traceback
            traceback.print_exc()

    # Report
    print('\n=== Final dataset counts ===')
    for cls in ['baby_cry', 'other']:
        wavs = glob.glob(f'{DATA_DIR}/{cls}/*.wav')
        print(f'  {cls}: {len(wavs)} files')
