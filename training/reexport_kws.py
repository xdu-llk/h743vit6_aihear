"""Re-export KWS model: PyTorch→Keras→TFLite INT8→C array."""
import torch, numpy as np, tensorflow as tf, librosa, glob, sys, os

sys.path.insert(0, '.')
from train_kws import KWS_CNN, CLASSES, N_MELS, FRAMES, N_FFT, HOP, SR

NC = len(CLASSES)
print(f"Classes: {CLASSES} ({NC})")

state = torch.load('best_kws.pth', map_location='cpu', weights_only=True)
print("Weights loaded")

def keras_kws():
    inp = tf.keras.Input(shape=(N_MELS, FRAMES, 1), name='in'); x = inp
    # Conv2d(1, 32, 5, 2, 2) -> ZeroPadding2D(2)+valid
    c1w = np.transpose(state['c1.weight'].numpy(), (2, 3, 1, 0))
    x = tf.keras.layers.ZeroPadding2D(padding=2)(x)
    x = tf.keras.layers.Conv2D(32, 5, strides=2, padding='valid', use_bias=False,
        kernel_initializer=tf.keras.initializers.Constant(c1w))(x)
    x = tf.keras.layers.BatchNormalization(name='b1', epsilon=1e-5)(x); x = tf.keras.layers.ReLU()(x)

    BN = {'epsilon': 1e-5}
    def ds_block(x, prefix, out_ch, stride):
        inch = x.shape[-1]
        dw_w = np.transpose(state[f'{prefix}.dw.weight'].numpy(), (2, 3, 0, 1))
        dw_w = dw_w.reshape(dw_w.shape[0], dw_w.shape[1], inch, 1)
        if stride > 1:
            x = tf.keras.layers.ZeroPadding2D(padding=1)(x)
            x = tf.keras.layers.DepthwiseConv2D(3, stride, padding='valid', use_bias=False,
                depthwise_initializer=tf.keras.initializers.Constant(dw_w))(x)
        else:
            x = tf.keras.layers.DepthwiseConv2D(3, stride, padding='same', use_bias=False,
                depthwise_initializer=tf.keras.initializers.Constant(dw_w))(x)
        x = tf.keras.layers.BatchNormalization(name=f'{prefix}.dw_bn', **BN)(x); x = tf.keras.layers.ReLU()(x)
        pw_w = np.transpose(state[f'{prefix}.pw.weight'].numpy(), (2, 3, 1, 0))
        x = tf.keras.layers.Conv2D(out_ch, 1, use_bias=False,
            kernel_initializer=tf.keras.initializers.Constant(pw_w))(x)
        x = tf.keras.layers.BatchNormalization(name=f'{prefix}.pw_bn', **BN)(x); x = tf.keras.layers.ReLU()(x)
        return x

    x = ds_block(x, 'd1', 64, 2); x = ds_block(x, 'd2', 128, 2)
    x = tf.keras.layers.GlobalAveragePooling2D()(x)
    fc_w = np.transpose(state['fc.weight'].numpy()); fc_b = state['fc.bias'].numpy()
    x = tf.keras.layers.Dense(NC, kernel_initializer=tf.keras.initializers.Constant(fc_w),
        bias_initializer=tf.keras.initializers.Constant(fc_b))(x)
    return tf.keras.Model(inp, x, name='KWS')

model = keras_kws(); print("Keras model built")

bn_list = ['b1', 'd1.dw_bn', 'd1.pw_bn', 'd2.dw_bn', 'd2.pw_bn']
for bn_name in bn_list:
    bn = model.get_layer(bn_name)
    gamma = state[f'{bn_name}.weight'].numpy(); beta = state[f'{bn_name}.bias'].numpy()
    mean = state[f'{bn_name}.running_mean'].numpy(); var = state[f'{bn_name}.running_var'].numpy()
    bn.set_weights([gamma, beta, mean, var])
print("BN weights set")

# Verify
test_file = sorted(glob.glob('feature_cache_kws/help_call/*.npy'))[0]
lm_norm = np.load(test_file)
pt_m = KWS_CNN(nc=NC); pt_m.load_state_dict(state); pt_m.eval()
with torch.no_grad(): pt_out = pt_m(torch.tensor(lm_norm).unsqueeze(0).unsqueeze(0)).numpy().ravel()
tf_out = model(lm_norm[np.newaxis,:,:,np.newaxis], training=False).numpy().ravel()
print(f"PyTorch: {[f'{x:.4f}' for x in pt_out]}")
print(f"Keras:   {[f'{x:.4f}' for x in tf_out]}")
match = np.allclose(pt_out, tf_out, atol=1e-3)
print(f"Match: {match}")
if not match: print("ERROR: Mismatch!"); sys.exit(1)

# Calibration
print("\nCalibration data...")
calib = []
def extract_mel(wav_path):
    y, _ = librosa.load(wav_path, sr=SR, mono=True)
    if len(y) < SR: y = np.pad(y, (0, SR - len(y)))
    else: y = y[:SR]
    mel = librosa.feature.melspectrogram(y=y, sr=SR, n_fft=N_FFT, hop_length=HOP, n_mels=N_MELS, fmin=20, fmax=8000, window='hann', center=False)
    lm = librosa.power_to_db(mel, ref=np.max)
    if lm.shape[1] < FRAMES: lm = np.pad(lm, ((0,0),(0,FRAMES-lm.shape[1])))
    else: lm = lm[:,:FRAMES]
    return ((lm - lm.mean())/(lm.std()+1e-6)).astype(np.float32)

from tqdm import tqdm
for cls_dir, count in [('data/help_kws/**/', 60), ('data/other/', 60), ('data/other/', 60)]:
    files = sorted(glob.glob(os.path.join(cls_dir, '*.wav')))[:count]
    for f in tqdm(files, desc=f'calib {cls_dir[:20]}'):
        try: calib.append(extract_mel(f))
        except: pass
print(f"  {len(calib)} samples")

# TFLite INT8
def rep():
    for f in calib: yield [f[np.newaxis, :, :, np.newaxis]]

run_model = tf.function(lambda x: model(x, training=False))
concrete_func = run_model.get_concrete_function(tf.TensorSpec([1, N_MELS, FRAMES, 1], tf.float32))
c = tf.lite.TFLiteConverter.from_concrete_functions([concrete_func])
c.optimizations = [tf.lite.Optimize.DEFAULT]; c.representative_dataset = rep
c.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
c.inference_input_type = tf.int8; c.inference_output_type = tf.int8
tflite = c.convert()
print(f"TFLite INT8: {len(tflite)} bytes")

with open('kws_int8.tflite', 'wb') as f: f.write(tflite)

with open('../Core/Src/kws_model.cc', 'w') as f:
    f.write('// KWS model: 3-class (background/other_speech/help_call)\n')
    f.write('// Auto-generated by reexport_kws.py\n')
    f.write('#include <cstdint>\n\n')
    f.write('extern const unsigned char g_kws_model[] = {\n')
    for i in range(0, len(tflite), 20):
        chunk = tflite[i:i+20]; f.write('  '+', '.join(f'0x{b:02x}' for b in chunk)+',\n')
    f.write('};\n')
    f.write(f'extern const int g_kws_model_len = {len(tflite)};\n')
print(f"-> Core/Src/kws_model.cc ({len(tflite)} bytes)")
print("Done!")
