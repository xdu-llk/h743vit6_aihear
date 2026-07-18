"""Train lightweight KWS model for Chinese help-call keyword spotting. 3-class."""
import torch, torch.nn as nn, torch.optim as optim
import numpy as np, os, glob, random, re
from torch.utils.data import Dataset, DataLoader, WeightedRandomSampler
from sklearn.model_selection import train_test_split
from collections import defaultdict
from tqdm import tqdm

SR, N_MELS, FRAMES, N_FFT, HOP = 16000, 40, 96, 512, 160
CLASSES = ['background', 'help_call']
BATCH, EP = 64, 80
DATA_ROOT = 'feature_cache_kws'
DEVICE = torch.device('cuda')

class SpecAugment:
    def __init__(self, f=8, t=8, nf=1, nt=1):
        self.f, self.t, self.nf, self.nt = f, t, nf, nt
    def __call__(self, mel):
        F, T = mel.shape
        for _ in range(self.nf):
            fw = random.randint(1, self.f)
            if fw > 0: f0 = random.randint(0, F-fw); mel[f0:f0+fw, :] = mel.mean()
        for _ in range(self.nt):
            tw = random.randint(1, self.t)
            if tw > 0: t0 = random.randint(0, T-tw); mel[:, t0:t0+tw] = mel.mean()
        return mel

class NpyDataset(Dataset):
    def __init__(self, paths, labels, augment=False):
        self.paths, self.labels, self.augment = paths, labels, augment
        self.spec_aug = SpecAugment() if augment else None
    def __len__(self): return len(self.paths)
    def __getitem__(self, i):
        feat = torch.from_numpy(np.load(self.paths[i])).unsqueeze(0).float()
        if self.augment and self.spec_aug is not None:
            mel = feat.squeeze(0).numpy(); mel = self.spec_aug(mel); feat = torch.from_numpy(mel).unsqueeze(0)
        return feat, torch.tensor(self.labels[i], dtype=torch.long)

def mixup_data(x, y, alpha=0.2):
    lam = np.random.beta(alpha, alpha) if alpha > 0 else 1.0
    lam = max(lam, 1 - lam); idx = torch.randperm(x.size(0), device=x.device)
    return lam*x + (1-lam)*x[idx], y, y[idx], lam

def mixup_loss(crit, pred, ya, yb, lam):
    return lam*crit(pred, ya) + (1-lam)*crit(pred, yb)

class DSBlock(nn.Module):
    def __init__(self, i, o, s=1):
        super().__init__()
        self.dw = nn.Conv2d(i, i, 3, s, 1, groups=i, bias=False); self.dw_bn = nn.BatchNorm2d(i)
        self.pw = nn.Conv2d(i, o, 1, bias=False); self.pw_bn = nn.BatchNorm2d(o)
        self.r = nn.ReLU(inplace=True)
    def forward(self, x): return self.r(self.pw_bn(self.pw(self.r(self.dw_bn(self.dw(x))))))

class KWS_CNN(nn.Module):
    def __init__(self, nc=3):
        super().__init__()
        self.c1 = nn.Conv2d(1, 32, 5, 2, 2, bias=False); self.b1 = nn.BatchNorm2d(32); self.r = nn.ReLU(inplace=True)
        self.d1 = DSBlock(32, 64, 2); self.d2 = DSBlock(64, 128, 2)
        self.p = nn.AdaptiveAvgPool2d(1); self.fc = nn.Linear(128, nc)
    def forward(self, x):
        x = self.r(self.b1(self.c1(x))); x = self.d1(x); x = self.d2(x)
        return self.fc(self.p(x).flatten(1))

def train():
    print('Loading data...')
    paths, labels_list = [], []
    for idx, cls in enumerate(CLASSES):
        files = sorted(glob.glob(f'{DATA_ROOT}/{cls}/*.npy'))
        for f in files: paths.append(f); labels_list.append(idx)
        print(f'  {cls}: {len(files)}')
    total = len(paths); print(f'Total: {total} samples')

    def extract_source(path):
        m = re.match(r'(.+)_(\d{3})$', os.path.splitext(os.path.basename(path))[0])
        return m.group(1) if m else os.path.basename(path)
    groups = defaultdict(list)
    for p, l in zip(paths, labels_list): groups[extract_source(p)].append((p, l))
    group_ids = list(groups.keys())
    print(f'  Sources: {len(group_ids)} (avg {total/len(group_ids):.1f} chunks/source)')
    train_groups, val_groups = train_test_split(group_ids, test_size=0.2, random_state=42)
    tp, tl = [], []; vp, vl = [], []
    for g in train_groups:
        for p, l in groups[g]: tp.append(p); tl.append(l)
    for g in val_groups:
        for p, l in groups[g]: vp.append(p); vl.append(l)
    print(f'  Train: {len(tp)} chunks from {len(train_groups)} sources')
    print(f'  Val:   {len(vp)} chunks from {len(val_groups)} sources')

    counts = np.bincount(tl, minlength=len(CLASSES))
    weights = 1.0/(counts+1); weights = weights/weights.sum()*len(CLASSES)
    class_weights = torch.tensor(weights, dtype=torch.float32).to(DEVICE)
    print(f'Class counts:   {dict(zip(CLASSES, counts))}')
    print(f'Class weights:  {dict(zip(CLASSES, weights.round(3)))}')

    train_ds = NpyDataset(tp, tl, augment=True); val_ds = NpyDataset(vp, vl, augment=False)
    sample_weights = [1.0/(counts[l]+1) for l in tl]
    sampler = WeightedRandomSampler(sample_weights, len(sample_weights), replacement=True)
    tr = DataLoader(train_ds, BATCH, sampler=sampler, num_workers=4, persistent_workers=True)
    va = DataLoader(val_ds, BATCH, shuffle=False, num_workers=4, persistent_workers=True)

    model = KWS_CNN(nc=len(CLASSES)).to(DEVICE)
    n_params = sum(p.numel() for p in model.parameters())
    crit = nn.CrossEntropyLoss(weight=class_weights, label_smoothing=0.1)
    opt = optim.AdamW(model.parameters(), lr=0.001, weight_decay=1e-4)
    sch = optim.lr_scheduler.CosineAnnealingWarmRestarts(opt, T_0=15, T_mult=2, eta_min=1e-6)
    print(f'\nTraining on {DEVICE} | {len(CLASSES)} classes | {n_params:,} params')
    print(f'MixUp a=0.2 | LabelSmooth=0.1 | GradClip=5.0\n{"="*65}')

    best_acc, best_epoch, no_improve = 0.0, 0, 0
    for ep in range(EP):
        model.train()
        train_loss, train_correct, train_total = 0.0, 0, 0
        for x, y in tqdm(tr, desc=f'Ep {ep+1}/{EP} [train]', leave=False):
            x, y = x.to(DEVICE), y.to(DEVICE); opt.zero_grad()
            if random.random() < 0.5:
                mx, ya, yb, lam = mixup_data(x, y, alpha=0.2); loss = mixup_loss(crit, model(mx), ya, yb, lam)
            else: loss = crit(model(x), y)
            loss.backward(); torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0); opt.step()
            train_loss += loss.item()
            with torch.no_grad(): train_correct += (model(x).argmax(1) == y).sum().item()
            train_total += y.size(0)
        sch.step()

        model.eval()
        val_loss, val_correct, val_total = 0.0, 0, 0
        class_correct = np.zeros(len(CLASSES)); class_total = np.zeros(len(CLASSES))
        with torch.no_grad():
            for x, y in va:
                x, y = x.to(DEVICE), y.to(DEVICE); logits = model(x)
                val_loss += crit(logits, y).item(); preds = logits.argmax(1)
                val_correct += (preds == y).sum().item(); val_total += y.size(0)
                for c in range(len(CLASSES)):
                    mask = (y == c); class_correct[c] += (preds[mask] == c).sum().item(); class_total[c] += mask.sum().item()

        train_acc = train_correct/train_total; val_acc = val_correct/val_total
        lr = opt.param_groups[0]['lr']
        print(f'Ep {ep+1:3d}/{EP} | tr_loss={train_loss/len(tr):.4f} tr_acc={train_acc:.3f} | va_loss={val_loss/len(va):.4f} va_acc={val_acc:.3f} | lr={lr:.2e}')
        cls_str = ' | '.join(f'{CLASSES[c]:>12s}:{class_correct[c]/class_total[c]:.3f}' if class_total[c]>0 else f'{CLASSES[c]:>12s}:N/A' for c in range(len(CLASSES)))
        print(f'       per-class -> {cls_str}')

        if val_acc > best_acc:
            best_acc, best_epoch, no_improve = val_acc, ep+1, 0
            torch.save(model.state_dict(), 'best_kws.pth'); print(f'       [best] saved')
        else:
            no_improve += 1
            if no_improve >= 20: print(f'\nEarly stopping at epoch {ep+1}'); break

    print(f'\n{"="*65}\nBest: epoch {best_epoch} | val_acc={best_acc:.4f}')
    model.load_state_dict(torch.load('best_kws.pth', map_location=DEVICE, weights_only=True)); model.eval()
    print('\nFinal per-class evaluation:')
    class_correct = np.zeros(len(CLASSES)); class_total = np.zeros(len(CLASSES))
    conf_matrix = np.zeros((len(CLASSES), len(CLASSES)))
    with torch.no_grad():
        for x, y in va:
            x, y = x.to(DEVICE), y.to(DEVICE); preds = model(x).argmax(1)
            for c in range(len(CLASSES)):
                mask = (y == c); class_correct[c] += (preds[mask] == c).sum().item(); class_total[c] += mask.sum().item()
            for i in range(len(y)): conf_matrix[y[i].item()][preds[i].item()] += 1
    for c in range(len(CLASSES)):
        acc = class_correct[c]/class_total[c] if class_total[c] > 0 else 0
        print(f'  {CLASSES[c]:15s}: {class_correct[c]:.0f}/{class_total[c]:.0f} = {acc:.3f}')
    print('\nConfusion Matrix:'); header = '         '+' '.join(f'{CLASSES[c][:7]:>7s}' for c in range(len(CLASSES)))
    print(header)
    for r in range(len(CLASSES)): print(f'{CLASSES[r]:>9s} '+' '.join(f'{conf_matrix[r][c]:7.0f}' for c in range(len(CLASSES))))
    return model

if __name__ == '__main__': train()
print('\nTraining complete! Run reexport_kws.py to export TFLite -> C array.')
