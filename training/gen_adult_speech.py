"""Generate Chinese adult speech samples for 'other' class using edge_tts.
Varied voices, rates, pitches — simulating TV/phone/conversation backgrounds."""
import os, asyncio, numpy as np, librosa, soundfile as sf, random

SR = 16000
OUT = 'data/other'
TARGET = 100
os.makedirs(OUT, exist_ok=True)

VOICES = [
    'zh-CN-XiaoxiaoNeural', 'zh-CN-XiaoyiNeural', 'zh-CN-YunxiNeural',
    'zh-CN-YunyangNeural', 'zh-CN-XiaochenNeural', 'zh-CN-XiaohanNeural',
    'zh-CN-XiaomengNeural', 'zh-CN-XiaomoNeural', 'zh-CN-XiaoxuanNeural',
    'zh-CN-YunfengNeural', 'zh-CN-YunjianNeural', 'zh-CN-XiaoshuangNeural',
]
# Everyday Chinese sentences — NOT "救命", just normal adult speech
TEXTS = [
    '今天天气不错，我们出去走走吧。',
    '晚饭想吃什么？我来做。',
    '明天记得带孩子去打疫苗。',
    '那个快递到了吗？帮我取一下。',
    '你看这个新闻，最近房价又跌了。',
    '妈，我手机找不到了帮我打个电话。',
    '这个菜怎么做啊？要不要放酱油？',
    '周末去不去逛商场？好久没去了。',
    '你作业写完了没有？别看电视了。',
    '老板说明天要加班，烦死了。',
    '喂，你到了吗？我在门口等你。',
    '今天开会讲了什么？我都没听进去。',
    '帮我倒杯水，谢谢。',
    '这衣服好看吗？我刚买的。',
    '来来来，吃饭了吃饭了。',
    '你把垃圾扔一下，别忘了。',
    '哎呀我忘了，今天是星期几来着？',
    '听说隔壁老王家孩子考上大学了。',
    '这个电影评分挺高的，晚上一起看。',
    '等一下我接个电话。喂，你好。',
    '累死了今天，终于下班了。',
    '宝宝睡了吗？小声点别吵醒他。',
    '这个怎么用啊你教教我。',
    '行行行，你说了算。',
    '不会吧，真的假的？',
    '嗯，我觉得差不多就这样吧。',
    '哈哈哈哈太好笑了这个。',
    '没什么事那我先挂了啊。',
    '你放心吧，我都搞定了。',
    '快递放门口就行，谢谢师傅。',
]

async def generate():
    count = 0
    for voice in VOICES:
        for text in TEXTS:
            if count >= TARGET:
                break
            # Random pitch and rate for variety
            rate = random.choice(['-20%', '-10%', '+0%', '+10%', '+20%'])
            pitch = random.choice(['-8Hz', '-4Hz', '+0Hz', '+4Hz', '+8Hz'])
            out_name = f'{OUT}/tts_adult_{count:03d}.wav'
            if os.path.exists(out_name):
                count += 1
                continue
            mp3 = f'{OUT}/_tmp_{count}.mp3'
            try:
                import edge_tts
                comm = edge_tts.Communicate(text, voice, rate=rate, pitch=pitch)
                await comm.save(mp3)
                y, _ = librosa.load(mp3, sr=SR, mono=True)
                os.remove(mp3)
                if len(y) < SR:
                    y = np.pad(y, (0, SR - len(y)))
                else:
                    y = y[:SR]
                rms = np.sqrt(np.mean(y**2))
                if rms < 0.001:
                    continue
                sf.write(out_name, y, SR)
                count += 1
                if count % 20 == 0:
                    print(f'  {count}/{TARGET}')
            except Exception as e:
                print(f'  skip: {e}')
        if count >= TARGET:
            break
    return count

n = asyncio.run(generate())
print(f'Done: {n} adult speech files -> data/other/')
