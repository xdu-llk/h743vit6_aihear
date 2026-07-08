"""Train DS-CNN for 2-class audio classification: baby_cry vs other.

Optimizations:
  - MixUp (α=0.2) — sample blending for small-dataset generalization
  - Label smoothing (0.1) — prevents overconfidence
  - Stronger SpecAugment (2× freq + 2× time masks)
  - Gradient clipping (max_norm=5.0)
  - WeightedRandomSampler + class weights for imbalance
  - CosineAnnealingWarmRestarts + early stopping
"""
import torch, torch.nn as nn, torch.optim as optim
import torch.nn.functional as F
import numpy as np, librosa, os, glob, random
from torch.utils.data import Dataset, DataLoader, WeightedRandomSampler
from sklearn.model_selection import train_test_split
from tqdm import tqdm

# ========== Config (matching STM32 preproc exactly) ==========
SR, N_MELS, FRAMES, N_FFT, HOP = 16000, 40, 96, 512, 160
CLASSES   = ['baby_cry', 'other']
BATCH, EP = 64, 100
DATA_ROOT = 'feature_cache'
DEVICE    = torch.device('cuda')

# ========== Audio Augmentation ==========
class AudioAugment:
    def __init__(self, sr=16000):
        self.sr = sr

    def __call__(self, y):
        if random.random() < 0.3:
            steps = random.uniform(-3, 3)
            y = librosa.effects.pitch_shift(y=y, sr=self.sr, n_steps=steps)
        if random.random() < 0.3:
            rate = random.uniform(0.85, 1.15)
            y = librosa.effects.time_stretch(y=y, rate=rate)
            if len(y) < self.sr: y = np.pad(y, (0, self.sr - len(y)))
            else: y = y[:self.sr]
        if random.random() < 0.4:
            snr = random.uniform(15, 30)
            rms = np.sqrt(np.mean(y**2) + 1e-10)
            y = y + np.random.randn(len(y)).astype(np.float32) * (rms / (10**(snr/20)))
        if random.random() < 0.3:
            y = y * random.uniform(0.6, 1.4)
        return np.clip(y, -1.0, 1.0)

# ========== Stronger SpecAugment ==========
class SpecAugment:
    def __init__(self, freq_mask=10, time_mask=10, n_freq=2, n_time=2):
        self.freq_mask = freq_mask
        self.time_mask = time_mask
        self.n_freq = n_freq
        self.n_time = n_time

    def __call__(self, mel):
        F, T = mel.shape
        for _ in range(self.n_freq):
            f = random.randint(1, self.freq_mask)
            if f > 0:
                f0 = random.randint(0, F - f)
                mel[f0:f0+f, :] = mel.mean()
        for _ in range(self.n_time):
            t = random.randint(1, self.time_mask)
            if t > 0:
                t0 = random.randint(0, T - t)
                mel[:, t0:t0+t] = mel.mean()
        return mel

# ========== Fast Npy Dataset (pre-computed features) ==========
class NpyDataset(Dataset):
    def __init__(self, paths, labels, augment=False):
        self.paths = paths
        self.labels = labels
        self.augment = augment
        self.spec_aug = SpecAugment() if augment else None
    def __len__(self): return len(self.paths)
    def __getitem__(self, i):
        feat = torch.from_numpy(np.load(self.paths[i])).unsqueeze(0).float()
        if self.augment and self.spec_aug is not None:
            mel = feat.squeeze(0).numpy()
            mel = self.spec_aug(mel)
            feat = torch.from_numpy(mel).unsqueeze(0)
        return feat, torch.tensor(self.labels[i], dtype=torch.long)

# ========== MixUp ==========
def mixup_data(x, y, alpha=0.2):
    lam = np.random.beta(alpha, alpha) if alpha > 0 else 1.0
    lam = max(lam, 1 - lam)  # symmetric: keep lam >= 0.5
    batch_size = x.size(0)
    index = torch.randperm(batch_size, device=x.device)
    mixed_x = lam * x + (1 - lam) * x[index]
    y_a, y_b = y, y[index]
    return mixed_x, y_a, y_b, lam

def mixup_loss(criterion, pred, y_a, y_b, lam):
    return lam * criterion(pred, y_a) + (1 - lam) * criterion(pred, y_b)

# ========== Dataset ==========
class AudioDataset(Dataset):
    def __init__(self, paths, labels, augment=False):
        self.paths = paths
        self.labels = labels
        self.augment = augment
        self.audio_aug = AudioAugment(SR) if augment else None
        self.spec_aug = SpecAugment() if augment else None

    def __len__(self):
        return len(self.paths)

    def __getitem__(self, i):
        y, _ = librosa.load(self.paths[i], sr=SR, mono=True)
        if len(y) < SR: y = np.pad(y, (0, SR - len(y)))
        else: y = y[:SR]

        if self.augment and self.audio_aug is not None:
            y = self.audio_aug(y)

        mel = librosa.feature.melspectrogram(
            y=y, sr=SR, n_fft=N_FFT, hop_length=HOP,
            n_mels=N_MELS, fmin=20, fmax=8000, window='hann')
        lm = librosa.power_to_db(mel, ref=np.max)

        if lm.shape[1] < FRAMES: lm = np.pad(lm, ((0, 0), (0, FRAMES - lm.shape[1])))
        else: lm = lm[:, :FRAMES]

        lm = (lm - lm.mean()) / (lm.std() + 1e-6)

        if self.augment and self.spec_aug is not None:
            lm = self.spec_aug(lm)

        return (torch.tensor(lm, dtype=torch.float32).unsqueeze(0),
                torch.tensor(self.labels[i], dtype=torch.long))

# ========== DS-CNN Model ==========
class DSConv(nn.Module):
    def __init__(self, i, o, s=1):
        super().__init__()
        self.dw = nn.Conv2d(i, i, 3, s, 1, groups=i, bias=False)
        self.bn1 = nn.BatchNorm2d(i)
        self.pw = nn.Conv2d(i, o, 1, bias=False)
        self.bn2 = nn.BatchNorm2d(o)
        self.r = nn.ReLU(inplace=True)

    def forward(self, x):
        return self.r(self.bn2(self.pw(self.r(self.bn1(self.dw(x))))))

class DSCNN(nn.Module):
    def __init__(self, nc=2):
        super().__init__()
        self.c1 = nn.Conv2d(1, 64, 3, 1, 1, bias=False)
        self.b1 = nn.BatchNorm2d(64)
        self.r = nn.ReLU(inplace=True)
        self.d1 = DSConv(64, 64)
        self.d2 = DSConv(64, 128, 2)
        self.d3 = DSConv(128, 128)
        self.d4 = DSConv(128, 256, 2)
        self.p = nn.AdaptiveAvgPool2d(1)
        self.fc = nn.Linear(256, nc)

    def forward(self, x):
        x = self.r(self.b1(self.c1(x)))
        x = self.d1(x); x = self.d2(x); x = self.d3(x); x = self.d4(x)
        x = self.p(x).flatten(1)
        return self.fc(x)

# ========== Train ==========
def train():
    print('Loading data...')
    paths, labels_list = [], []
    for idx, cls in enumerate(CLASSES):
        files = sorted(glob.glob(f'{DATA_ROOT}/{cls}/*.npy'))
        for f in files:
            paths.append(f)
            labels_list.append(idx)
        print(f'  {cls}: {len(files)}')

    total = len(paths)
    print(f'Total: {total} samples')

    tp, vp, tl, vl = train_test_split(
        paths, labels_list, test_size=0.2, stratify=labels_list, random_state=42)

    # Class weights
    counts = np.bincount(tl, minlength=len(CLASSES))
    weights = 1.0 / (counts + 1)
    weights = weights / weights.sum() * len(CLASSES)
    class_weights = torch.tensor(weights, dtype=torch.float32).to(DEVICE)
    print(f'Class counts:   {dict(zip(CLASSES, counts))}')
    print(f'Class weights:  {dict(zip(CLASSES, weights.round(3)))}')
    # How many baby_cry per epoch with WeightedRandomSampler
    baby_cry_per_epoch = sum(1 for l in tl if l == 0)
    print(f'baby_cry/epoch: ~{baby_cry_per_epoch} (+ augmentation)')

    train_ds = NpyDataset(tp, tl, augment=True)
    val_ds = NpyDataset(vp, vl, augment=False)

    sample_weights = [1.0 / (counts[l] + 1) for l in tl]
    sampler = WeightedRandomSampler(sample_weights, len(sample_weights), replacement=True)

    tr = DataLoader(train_ds, BATCH, sampler=sampler, num_workers=4, persistent_workers=True)
    va = DataLoader(val_ds, BATCH, shuffle=False, num_workers=4, persistent_workers=True)

    model = DSCNN(nc=len(CLASSES)).to(DEVICE)
    n_params = sum(p.numel() for p in model.parameters())
    # Label smoothing: prevents overconfidence, critical for small minority class
    crit = nn.CrossEntropyLoss(weight=class_weights, label_smoothing=0.1)
    opt = optim.AdamW(model.parameters(), lr=0.001, weight_decay=1e-4)
    sch = optim.lr_scheduler.CosineAnnealingWarmRestarts(
        opt, T_0=15, T_mult=2, eta_min=1e-6)

    print(f'\nTraining on {DEVICE} | {len(CLASSES)} classes | {n_params:,} params')
    print(f'MixUp α=0.2 | LabelSmooth=0.1 | SpecAug 2×2 | GradClip=5.0')
    print('=' * 65)

    best_acc, best_epoch, no_improve = 0.0, 0, 0
    patience = 20

    for ep in range(EP):
        model.train()
        train_loss, train_correct, train_total = 0.0, 0, 0

        for x, y in tqdm(tr, desc=f'Ep {ep+1}/{EP} [train]', leave=False):
            x, y = x.to(DEVICE), y.to(DEVICE)
            opt.zero_grad()

            # MixUp — 50% probability per batch
            if random.random() < 0.5:
                mx, ya, yb, lam = mixup_data(x, y, alpha=0.2)
                logits = model(mx)
                loss = mixup_loss(crit, logits, ya, yb, lam)
            else:
                logits = model(x)
                loss = crit(logits, y)

            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            opt.step()

            train_loss += loss.item()
            # Use raw logits on x for accuracy (not MixUp logits)
            with torch.no_grad():
                train_correct += (model(x).argmax(1) == y).sum().item()
            train_total += y.size(0)

        sch.step()

        # Validate
        model.eval()
        val_loss, val_correct, val_total = 0.0, 0, 0
        class_correct = np.zeros(len(CLASSES))
        class_total = np.zeros(len(CLASSES))

        with torch.no_grad():
            for x, y in va:
                x, y = x.to(DEVICE), y.to(DEVICE)
                logits = model(x)
                val_loss += crit(logits, y).item()
                preds = logits.argmax(1)
                val_correct += (preds == y).sum().item()
                val_total += y.size(0)
                for c in range(len(CLASSES)):
                    mask = (y == c)
                    class_correct[c] += (preds[mask] == c).sum().item()
                    class_total[c] += mask.sum().item()

        train_acc = train_correct / train_total
        val_acc = val_correct / val_total
        lr = opt.param_groups[0]['lr']

        print(f'Ep {ep+1:3d}/{EP} | '
              f'tr_loss={train_loss/len(tr):.4f} tr_acc={train_acc:.3f} | '
              f'va_loss={val_loss/len(va):.4f} va_acc={val_acc:.3f} | '
              f'lr={lr:.2e}')

        cls_str = ' | '.join(
            f'{CLASSES[c]:>9s}:{class_correct[c]/class_total[c]:.3f}' if class_total[c] > 0
            else f'{CLASSES[c]:>9s}:N/A'
            for c in range(len(CLASSES)))
        print(f'       per-class → {cls_str}')

        if val_acc > best_acc:
            best_acc, best_epoch, no_improve = val_acc, ep + 1, 0
            torch.save(model.state_dict(), 'best_dscnn.pth')
            print(f'       [best] saved')
        else:
            no_improve += 1

        if no_improve >= patience:
            print(f'\nEarly stopping at epoch {ep+1}')
            break

    print(f'\n{"=" * 65}')
    print(f'Best: epoch {best_epoch} | val_acc={best_acc:.4f}')

    # Load best model for final eval
    model.load_state_dict(torch.load('best_dscnn.pth', map_location=DEVICE, weights_only=True))
    model.eval()
    print('\nFinal per-class evaluation:')
    class_correct = np.zeros(len(CLASSES))
    class_total = np.zeros(len(CLASSES))
    conf_matrix = np.zeros((len(CLASSES), len(CLASSES)))
    with torch.no_grad():
        for x, y in va:
            x, y = x.to(DEVICE), y.to(DEVICE)
            preds = model(x).argmax(1)
            for c in range(len(CLASSES)):
                mask = (y == c)
                class_correct[c] += (preds[mask] == c).sum().item()
                class_total[c] += mask.sum().item()
            for i in range(len(y)):
                conf_matrix[y[i].item()][preds[i].item()] += 1

    for c in range(len(CLASSES)):
        acc = class_correct[c]/class_total[c] if class_total[c] > 0 else 0
        print(f'  {CLASSES[c]:15s}: {class_correct[c]:.0f}/{class_total[c]:.0f} = {acc:.3f}')

    print('\nConfusion Matrix (rows=true, cols=pred):')
    header = '         ' + ' '.join(f'{CLASSES[c][:7]:>7s}' for c in range(len(CLASSES)))
    print(header)
    for r in range(len(CLASSES)):
        row = f'{CLASSES[r]:>9s} ' + ' '.join(f'{conf_matrix[r][c]:7.0f}' for c in range(len(CLASSES)))
        print(row)

    return model


if __name__ == '__main__':
    model = train()
    print('\nTraining complete! Run reexport_v3.py to export TFLite → C array.')
