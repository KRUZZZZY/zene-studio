#!/usr/bin/env python3
"""Final characterization probes for RUNTIME-TEST.md."""
import numpy as np, struct, os

TD = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata"
CC = os.environ.get("RNNOISE_REPEAT_DIR", "/tmp/crashcheck")


def read_wav(path):
    raw = open(path, "rb").read(); pos = 12; fmt = None; data = None
    while pos + 8 <= len(raw):
        cid, size = struct.unpack_from("<4sI", raw, pos)
        ch = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ": fmt = struct.unpack_from("<HHIIHH", ch, 0)
        elif cid == b"data": data = ch
        pos += 8 + size + (size & 1)
    tag, nch, sr, _, _, bits = fmt
    x = np.frombuffer(data, dtype="<f4" if tag == 3 else "<i2").astype(np.float64)
    if tag == 1: x /= 32768.0
    return x.reshape(-1, nch)


def mono(x): return (x[:, 0] + x[:, 1]) * 0.5
def db(v): return 20 * np.log10(v) if v > 0 else float("-inf")


A = mono(read_wav(f"{TD}/out_A_denoised.wav"))
C = mono(read_wav(f"{TD}/out_C_nofx.wav"))

print("=== 1. where is the A-vs-C difference? ===")
d = np.abs(A - C)
i = int(np.argmax(d))
print(f"max|diff| = {d[i]:.6f} at frame {i} ({i/48000:.6f} s)")
for lo, hi, name in [(0, 3360, "startup 0-70ms"), (3360, 96000, "source 70ms-2.0s"), (96000, 192000, "after 2.0s")]:
    seg = d[lo:hi]
    print(f"  {name:22s}: max|diff|={seg.max():.6f}  mean|diff|={seg.mean():.8f}  frac>0.01={np.mean(seg > 0.01)*100:.3f}%")

print()
print("=== 2. tail burst stability across A runs ===")
for run in ["A_1", "A_2", "A_3"]:
    a = mono(read_wav(f"{CC}/{run}.wav"))
    nz = a[int(0.85 * 48000):int(1.15 * 48000)]
    nz_db = 20 * np.log10(np.sqrt(np.mean(nz ** 2))) if np.sqrt(np.mean(nz ** 2)) > 0 else float("-inf")
    vals = []
    for t0 in [2.00, 2.02, 2.05, 2.10, 2.15]:
        s, e = int(t0 * 48000), int((t0 + 0.02) * 48000)
        vals.append(f"{t0:.2f}-{t0+0.02:.2f}s rms={np.sqrt(np.mean(a[s:e]**2)):.6f}")
    print(f"  {run}: noise-only rms={np.sqrt(np.mean(nz**2)):.4f} ({nz_db:+.2f} dB) | " + " | ".join(vals))

print()
print("=== 3. noise-only passage: is A a delayed copy of C? (normalised xcorr) ===")
for lo_s, hi_s, name in [(0.10, 0.70, "speech1 0.10-0.70s"), (0.85, 1.15, "noise 0.85-1.15s")]:
    a = A[int(lo_s * 48000):int(hi_s * 48000)]
    c = C[int(lo_s * 48000):int(hi_s * 48000)]
    best = (0, -1)
    for lag in range(0, 1600):
        if lag == 0:
            aa, cc = a, c
        else:
            aa, cc = a[lag:], c[:-lag]
        denom = np.sqrt(np.sum(aa ** 2) * np.sum(cc ** 2))
        r = float(np.sum(aa * cc) / denom) if denom > 0 else 0.0
        if r > best[1]: best = (lag, r)
    print(f"  {name:20s}: best lag={best[0]:4d} corr={best[1]:+.4f}  (lag 480: {np.sum(a[480:]*c[:-480])/np.sqrt(np.sum(a[480:]**2)*np.sum(c[:-480]**2)):+.4f}, lag 960: {np.sum(a[960:]*c[:-960])/np.sqrt(np.sum(a[960:]**2)*np.sum(c[:-960]**2)):+.4f})")

print()
print("=== 4. band levels A vs C (noise-only passage, dB A/C) ===")
s, e = int(0.85 * 48000), int(1.15 * 48000)
W = np.hanning(e - s)
for lo, hi in [(0, 200), (200, 1000), (1000, 4000), (4000, 8000), (8000, 16000), (16000, 24000)]:
    fa = np.fft.rfft(A[s:e] * W); fc = np.fft.rfft(C[s:e] * W)
    fr = np.fft.rfftfreq(e - s, 1 / 48000)
    m = (fr >= lo) & (fr < hi)
    pa = np.sum(np.abs(fa[m]) ** 2); pc = np.sum(np.abs(fc[m]) ** 2)
    print(f"  {lo:5d}-{hi:5d} Hz: {db(np.sqrt(pa/pc)):+7.2f} dB")
