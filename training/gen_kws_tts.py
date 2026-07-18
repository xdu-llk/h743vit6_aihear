"""Generate TTS for 6 KWS trigger words. 16kHz mono WAV."""
import asyncio, edge_tts, os, glob, subprocess, random, numpy as np, soundfile as sf

OUT_DIR = 'data/help_kws'
SR = 16000
WORDS = ['救命', '救命啊', '救救我', '快救命', '来人救命', '啊救命']

ALL_VOICES = [
    'zh-CN-XiaoxiaoNeural', 'zh-CN-YunxiNeural',
    'zh-CN-XiaoyiNeural', 'zh-CN-YunjianNeural',
    'zh-CN-YunyangNeural', 'zh-CN-XiaochenNeural',
    'zh-CN-XiaohanNeural', 'zh-CN-XiaoshuangNeural',
    'zh-CN-XiaoxuanNeural', 'zh-CN-YunfengNeural',
    'zh-CN-YunhaoNeural', 'zh-CN-YunzeNeural',
    'zh-TW-HsiaoChenNeural', 'zh-TW-HsiaoYuNeural',
    'zh-HK-HiuMaanNeural', 'zh-HK-WanLungNeural',
]

VOICE_MAP = {
    '救命':   ALL_VOICES, '救命啊': ALL_VOICES,
    '救救我': ['zh-CN-XiaoxiaoNeural','zh-CN-YunxiNeural','zh-CN-XiaoyiNeural','zh-CN-YunjianNeural','zh-CN-YunyangNeural','zh-CN-XiaoxuanNeural','zh-TW-HsiaoChenNeural','zh-TW-HsiaoYuNeural','zh-HK-HiuMaanNeural','zh-HK-WanLungNeural'],
    '快救命':  ALL_VOICES, '来人救命': ALL_VOICES, '啊救命':  ALL_VOICES,
}

TAKES = 25

async def gen_one(voice, text, label, out_path):
    rate = random.choice(['-1%','-2%','-3%','-4%','-5%'])
    communicate = edge_tts.Communicate(text=text, voice=voice, rate=rate, volume='+25%')
    mp3_path = out_path.replace('.wav','.mp3')
    await communicate.save(mp3_path)
    subprocess.run(['ffmpeg','-y','-i',mp3_path,'-ar',str(SR),'-ac','1','-sample_fmt','s16',out_path], capture_output=True)
    if os.path.exists(mp3_path): os.remove(mp3_path)
    try:
        y, sr = sf.read(out_path)
        dur = len(y)/sr
        # Strict quality: reject if too short (partial reading) or too long (noise)
        if dur < MIN_DUR.get(label, 0.5) or dur > 3.0:
            os.remove(out_path); return None
        # Reject if mostly silence (RMS too low)
        rms = np.sqrt(np.mean(y.astype(float)**2))
        if rms < 0.01: os.remove(out_path); return None
        # Center and pad/trim to exactly 1s
        if len(y) < SR: y = np.pad(y, (0, SR-len(y)))
        elif len(y) > SR: start = (len(y)-SR)//2; y = y[start:start+SR]
        sf.write(out_path, y, SR, subtype='PCM_16')
        return True
    except: return None

async def main():
    tasks = []; counts = {}
    for word in WORDS:
        word_dir = os.path.join(OUT_DIR, word); os.makedirs(word_dir, exist_ok=True)
        for voice in VOICE_MAP[word]:
            counts[word] = counts.get(word, 0)
            for _ in range(TAKES):
                out = os.path.join(word_dir, f'tts_{counts[word]:03d}.wav')
                tasks.append(gen_one(voice, word, word, out)); counts[word] += 1

    print(f'Generating {len(tasks)} TTS samples...')
    BATCH = 8; good = 0
    for i in range(0, len(tasks), BATCH):
        results = await asyncio.gather(*tasks[i:i+BATCH], return_exceptions=True)
        good += sum(1 for r in results if r)
        if (i+BATCH) % 200 == 0: print(f'  {min(i+BATCH, len(tasks))}/{len(tasks)}')

    for mp3 in glob.glob(os.path.join(OUT_DIR, '**','*.mp3'), recursive=True): os.remove(mp3)
    for word in WORDS:
        wavs = sorted(glob.glob(os.path.join(OUT_DIR, word, '*.wav')))
        print(f'  {word}: {len(wavs)}')
    print(f'Done! {good} valid samples in {OUT_DIR}/')

if __name__ == '__main__':
    asyncio.run(main())
