"""Re-export TFLite int8 — 2-class (baby_cry vs other), Keras path."""
import torch, numpy as np, tensorflow as tf
import librosa, glob, sys

sys.path.insert(0, '.')
from train_dscnn import DSCNN, CLASSES, N_MELS, FRAMES, N_FFT, HOP, SR

NC = len(CLASSES)
print(f"Classes: {CLASSES} ({NC})")

# ---------- Load PyTorch weights ----------
state = torch.load('best_dscnn.pth', map_location='cpu', weights_only=True)
print("Weights loaded")

# ---------- Build Keras DSCNN ----------
def keras_dscnn():
    inp = tf.keras.Input(shape=(N_MELS, FRAMES, 1), name='in')
    x = inp
    c1w = np.transpose(state['c1.weight'].numpy(), (2,3,1,0))
    x = tf.keras.layers.Conv2D(64, 3, padding='same', use_bias=False,
        kernel_initializer=tf.keras.initializers.Constant(c1w))(x)
    x = tf.keras.layers.BatchNormalization(name='b1', epsilon=1e-5)(x)
    x = tf.keras.layers.ReLU()(x)

    BN_ARGS = {'epsilon': 1e-5}
    def ds_block(x, prefix, out_ch, stride):
        in_ch = x.shape[-1]
        dw_w = np.transpose(state[f'{prefix}.dw.weight'].numpy(), (2,3,0,1))
        dw_w = dw_w.reshape(dw_w.shape[0], dw_w.shape[1], in_ch, 1)
        x = tf.keras.layers.DepthwiseConv2D(3, stride, padding='same', use_bias=False,
            depthwise_initializer=tf.keras.initializers.Constant(dw_w))(x)
        x = tf.keras.layers.BatchNormalization(name=f'{prefix}.bn1', **BN_ARGS)(x)
        x = tf.keras.layers.ReLU()(x)
        pw_w = np.transpose(state[f'{prefix}.pw.weight'].numpy(), (2,3,1,0))
        x = tf.keras.layers.Conv2D(out_ch, 1, use_bias=False,
            kernel_initializer=tf.keras.initializers.Constant(pw_w))(x)
        x = tf.keras.layers.BatchNormalization(name=f'{prefix}.bn2', **BN_ARGS)(x)
        x = tf.keras.layers.ReLU()(x)
        return x

    x = ds_block(x, 'd1', 64, 1)
    x = ds_block(x, 'd2', 128, 2)
    x = ds_block(x, 'd3', 128, 1)
    x = ds_block(x, 'd4', 256, 2)
    x = tf.keras.layers.GlobalAveragePooling2D()(x)
    fc_w = np.transpose(state['fc.weight'].numpy())
    fc_b = state['fc.bias'].numpy()
    x = tf.keras.layers.Dense(NC,
        kernel_initializer=tf.keras.initializers.Constant(fc_w),
        bias_initializer=tf.keras.initializers.Constant(fc_b))(x)
    return tf.keras.Model(inp, x, name='DSCNN')

model = keras_dscnn()
print("Keras model built")

# Set BN weights
bn_list = ['b1','d1.bn1','d1.bn2','d2.bn1','d2.bn2','d3.bn1','d3.bn2','d4.bn1','d4.bn2']
for bn_name in bn_list:
    bn = model.get_layer(bn_name)
    gamma = state[f'{bn_name}.weight'].numpy()
    beta  = state[f'{bn_name}.bias'].numpy()
    mean  = state[f'{bn_name}.running_mean'].numpy()
    var   = state[f'{bn_name}.running_var'].numpy()
    bn.set_weights([gamma, beta, mean, var])
print("BN weights set")

# ---------- Verify Keras == PyTorch ----------
test_file = glob.glob('data/baby_cry/*.wav')[0]
y, _ = librosa.load(test_file, sr=SR, mono=True)
y = y[:SR] if len(y) >= SR else np.pad(y, (0, SR - len(y)))
mel = librosa.feature.melspectrogram(y=y, sr=SR, n_fft=N_FFT, hop_length=HOP,
        n_mels=N_MELS, fmin=20, fmax=8000, window='hann')
lm = librosa.power_to_db(mel, ref=np.max)
if lm.shape[1] < FRAMES: lm = np.pad(lm, ((0,0),(0,FRAMES-lm.shape[1])))
else: lm = lm[:,:FRAMES]
lm_norm = (lm - lm.mean()) / (lm.std() + 1e-6)

pt_m = DSCNN(nc=NC); pt_m.load_state_dict(state); pt_m.eval()
with torch.no_grad():
    pt_out = pt_m(torch.tensor(lm_norm).unsqueeze(0).unsqueeze(0)).numpy().ravel()
tf_out = model(lm_norm[np.newaxis,:,:,np.newaxis], training=False).numpy().ravel()
print(f"PyTorch: {[f'{x:.4f}' for x in pt_out]}")
print(f"Keras:   {[f'{x:.4f}' for x in tf_out]}")
print(f"Match: {np.allclose(pt_out, tf_out, atol=1e-3)}")

# ---------- Calibration data ----------
print("\nCalibration data...")
calib = []
# baby_cry
for f in sorted(glob.glob('data/baby_cry/*.wav'))[:60]:
    y, _ = librosa.load(f, sr=SR, mono=True)
    if len(y) < SR: y = np.pad(y, (0, SR - len(y)))
    else: y = y[:SR]
    mel = librosa.feature.melspectrogram(y=y, sr=SR, n_fft=N_FFT, hop_length=HOP,
            n_mels=N_MELS, fmin=20, fmax=8000, window='hann')
    lm = librosa.power_to_db(mel, ref=np.max)
    if lm.shape[1] < FRAMES: lm = np.pad(lm, ((0,0),(0,FRAMES-lm.shape[1])))
    else: lm = lm[:,:FRAMES]
    lm = (lm - lm.mean()) / (lm.std() + 1e-6)
    calib.append(lm.astype(np.float32))
# other
for f in sorted(glob.glob('data/other/*.wav'))[:120]:
    y, _ = librosa.load(f, sr=SR, mono=True)
    if len(y) < SR: y = np.pad(y, (0, SR - len(y)))
    else: y = y[:SR]
    mel = librosa.feature.melspectrogram(y=y, sr=SR, n_fft=N_FFT, hop_length=HOP,
            n_mels=N_MELS, fmin=20, fmax=8000, window='hann')
    lm = librosa.power_to_db(mel, ref=np.max)
    if lm.shape[1] < FRAMES: lm = np.pad(lm, ((0,0),(0,FRAMES-lm.shape[1])))
    else: lm = lm[:,:FRAMES]
    lm = (lm - lm.mean()) / (lm.std() + 1e-6)
    calib.append(lm.astype(np.float32))
print(f"  {len(calib)} samples")

# ---------- TFLite int8 ----------
def rep():
    for f in calib:
        yield [f[np.newaxis, :, :, np.newaxis]]

run_model = tf.function(lambda x: model(x, training=False))
concrete_func = run_model.get_concrete_function(
    tf.TensorSpec([1, N_MELS, FRAMES, 1], tf.float32))

c = tf.lite.TFLiteConverter.from_concrete_functions([concrete_func])
c.optimizations = [tf.lite.Optimize.DEFAULT]
c.representative_dataset = rep
c.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
c.inference_input_type = tf.int8
c.inference_output_type = tf.int8
tflite = c.convert()
print(f"TFLite: {len(tflite)} bytes")

with open('dscnn_int8.tflite', 'wb') as f:
    f.write(tflite)

# C array → STM32 project
with open('../Core/Src/dscnn_model.cc', 'w') as f:
    f.write('#include <cstdint>\n')
    f.write('extern const unsigned char g_model[] = {\n  ')
    f.write(', '.join(f'0x{b:02x}' for b in tflite))
    f.write(f'\n}};\n')
    f.write(f'extern const int g_model_len = {len(tflite)};\n')
print(f"→ Core/Src/dscnn_model.cc ({len(tflite)} bytes)")
print("Done!")
