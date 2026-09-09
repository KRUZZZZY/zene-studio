#!/usr/bin/env python3
"""Definitive measurements for the RNNoise denoiser runtime test (AI-KOS #559).

Reads every WAV produced by run_renders.sh (plus the scale-experiment renders)
and prints every number quoted in RUNTIME-TEST.md. All values are computed
from the actual rendered WAVs; nothing is estimated or hard-coded.

Minimal RIFF reader handles 16-bit PCM (fmt 1) and 32-bit float (fmt 3),
since Python 3.11's stdlib wave module rejects WAVE_FORMAT_IEEE_FLOAT.
"""
import struct
import numpy as np

TD = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata"
SR = 48000


def read_wav(path):
    raw = open(path, "rb").read()
    assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE", path
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(raw):
        cid, size = struct.unpack_from("<4sI", raw, pos)
        chunk = raw[pos + 8: pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", chunk, 0)
        elif cid == b"data":
            data = chunk
        pos += 8 + size + (size & 1)
    tag, n_ch, sr, _brate, _align, bits = fmt
    if tag == 3 and bits == 32:
        x = np.frombuffer(data, dtype="<f4")
    elif tag == 1 and bits == 16:
        x = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    else:
        raise ValueError(f"{path}: fmt tag={tag} bits={bits}")
    return x.reshape(-1, n_ch).mean(axis=1), sr


def rms(x):
    return float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0


def db(x):
    r = rms(x)
    return float(-np.inf if r == 0 else 20.0 * np.log10(r))


def seg(x, t0, t1):
    return x[int(t0 * SR):int(t1 * SR)]


def stats(name, x, sr):
    assert sr == SR, (name, sr)
    finite = bool(np.all(np.isfinite(x)))
    print(f"{name:24s} frames={len(x):7d} finite={finite!s:5s} "
          f"peak={np.max(np.abs(x)):12.6f} ({db(np.array([np.max(np.abs(x))])) if np.max(np.abs(x))>0 else -np.inf:8.2f} dBFS) "
          f"RMS={db(x):9.2f} dB")
    return x


print("#" * 78)
print("# 1. SOURCE SANITY")
print("#" * 78)
src, _ = read_wav(f"{TD}/speechlike_noise_2s_48k.wav")
print(f"source speechlike_noise_2s_48k.wav: frames={len(src)} sr={SR} "
      f"peak={np.max(np.abs(src)):.4f} RMS={db(src):.2f} dBFS")
print(f"  speech seg 0.10-0.70s RMS={db(seg(src,0.10,0.70)):.2f} dBFS | "
      f"noise-only 0.85-1.15s RMS={db(seg(src,0.85,1.15)):.2f} dBFS")
esrc, _ = read_wav(f"{TD}/speechlike_noise_2s_plus100.wav")
print(f"edge source speechlike_noise_2s_plus100.wav: frames={len(esrc)} "
      f"(= 200*480 + {len(esrc) % 480}; 96100 mod 480 = {96100 % 480})")

print()
print("#" * 78)
print("# 2. PER-FILE MEASUREMENTS (all renders, 4.0 s @ 48 kHz stereo, mono mixdown)")
print("#" * 78)
files = [
    ("A_denoised(1.0scale)", "out_A_denoised.wav"),
    ("B_bypassed(on=0)", "out_B_bypassed.wav"),
    ("C_nofx(1.0scale)", "out_C_nofx.wav"),
    ("D_float_nofx(x32768)", "out_D_float_nofx.wav"),
    ("E_float_denoised", "out_E_float_denoised.wav"),
    ("F_bogus_plugin", "out_F_bogus.wav"),
    ("EDGE_denoised", "out_edge_denoised.wav"),
    ("EDGE_nofx", "out_edge_nofx.wav"),
]
data = {}
for name, fn in files:
    x, sr = read_wav(f"{TD}/{fn}")
    data[name] = stats(name, x, sr)

print()
print("per-segment RMS (dBFS):")
hdr = f"{'file':24s} {'speech1 .1-.7':>14s} {'noise .85-1.15':>15s} {'speech2 1.3-1.9':>16s} {'tail 2.1-3.9':>13s}"
print(hdr)
for name, _ in files:
    x = data[name]
    print(f"{name:24s} {db(seg(x,0.10,0.70)):14.2f} {db(seg(x,0.85,1.15)):15.2f} "
          f"{db(seg(x,1.30,1.90)):16.2f} {db(seg(x,2.10,3.90)):13.2f}")

print()
print("#" * 78)
print("# 3. A-B-C DIFFERENCES (int16-scale sample; plugin active vs bypassed vs absent)")
print("#" * 78)
for a, b in [("A_denoised(1.0scale)", "B_bypassed(on=0)"),
             ("A_denoised(1.0scale)", "C_nofx(1.0scale)"),
             ("B_bypassed(on=0)", "C_nofx(1.0scale)"),
             ("F_bogus_plugin", "C_nofx(1.0scale)")]:
    ma, mb = data[a], data[b]
    n = min(len(ma), len(mb))
    ma, mb = ma[:n], mb[:n]
    print(f"{a:22s} vs {b:20s}: RMS(all) {db(ma)-db(mb):+7.2f} dB | "
          f"speech {db(seg(ma,0.10,0.70))-db(seg(mb,0.10,0.70)):+7.2f} dB | "
          f"noise-only {db(seg(ma,0.85,1.15))-db(seg(mb,0.85,1.15)):+7.2f} dB | "
          f"max|diff| {np.max(np.abs(ma-mb)):.6f} | identical={np.array_equal(ma,mb)}")

print()
print("#" * 78)
print("# 4. SCALE EXPERIMENT (same plugin binary, float sample at RNNoise's native scale)")
print("#" * 78)
D, E = data["D_float_nofx(x32768)"], data["E_float_denoised"]
for t0, t1, label in [(0.10, 0.70, "speech1"), (0.85, 1.15, "NOISE-ONLY"),
                      (1.30, 1.90, "speech2"), (2.10, 3.90, "silent tail")]:
    rd, re = rms(seg(D, t0, t1)), rms(seg(E, t0, t1))
    print(f"{label:12s} rmsD={rd:11.4f} rmsE={re:11.4f}  E/D={re/max(rd,1e-30):9.4f}  "
          f"({db(seg(E,t0,t1))-db(seg(D,t0,t1)):+7.2f} dB)")
print(f"overall: rmsD={rms(D):.4f} rmsE={rms(E):.4f} ({db(E)-db(D):+.2f} dB) | "
      f"peakD={np.max(np.abs(D)):.1f} peakE={np.max(np.abs(E)):.1f}")
s, e = int(0.85 * SR), int(1.15 * SR)
wd = np.abs(np.fft.rfft(D[s:e] * np.hanning(e - s)))
we = np.abs(np.fft.rfft(E[s:e] * np.hanning(e - s)))
f = np.fft.rfftfreq(e - s, 1 / SR)
print("noise-only band levels dB(E/D):")
for f0, f1 in [(0, 200), (200, 1000), (1000, 4000), (4000, 8000), (8000, 16000), (16000, 24000)]:
    m = (f >= f0) & (f < f1)
    r = 20 * np.log10(np.sqrt(np.mean(we[m] ** 2)) / max(np.sqrt(np.mean(wd[m] ** 2)), 1e-30))
    print(f"  {f0:6d}-{f1:6d} Hz: {r:+7.2f} dB")

print()
print("#" * 78)
print("# 5. EDGE CASE (96100-frame clip = 200 full frames + 100-sample partial frame)")
print("#" * 78)
ed, en = data["EDGE_denoised"], data["EDGE_nofx"]
print(f"EDGE_denoised finite={bool(np.all(np.isfinite(ed)))}  EDGE_nofx finite={bool(np.all(np.isfinite(en)))}")
nz_d = np.nonzero(np.abs(ed) > 1e-7)[0]
nz_n = np.nonzero(np.abs(en) > 1e-7)[0]
print(f"last non-zero sample: EDGE_denoised={nz_d[-1]} ({nz_d[-1]/SR:.6f} s)  "
      f"EDGE_nofx={nz_n[-1]} ({nz_n[-1]/SR:.6f} s)   [clip ends at frame {len(esrc)} = {len(esrc)/SR:.6f} s]")
for t0, t1 in [(2.00, 2.01), (2.01, 2.05), (2.05, 2.10), (2.10, 2.50)]:
    print(f"  tail {t0:.2f}-{t1:.2f}s: rms EDGE_denoised={rms(seg(ed,t0,t1)):.6f}  "
          f"EDGE_nofx={rms(seg(en,t0,t1)):.6f}")
print(f"max|EDGE_denoised - EDGE_nofx| = {np.max(np.abs(ed-en)):.6f}")

print()
print("#" * 78)
print("# 6. LATENCY ESTIMATE (A vs C, normalised cross-correlation)")
print("#" * 78)
A, C = data["A_denoised(1.0scale)"], data["C_nofx(1.0scale)"]
print("model: A[n] ~= C[n-L]  ->  maximise normalised corr over L")
for lo, hi, name in [(0.10, 0.70, "speech1"), (0.85, 1.15, "noise-only"), (1.30, 1.90, "speech2")]:
    s, e = int(lo * SR), int(hi * SR)
    a, c = A[s:e], C[s:e]
    best = (0, -1.0)
    for l in range(0, 2000):
        aa, cc = (a, c) if l == 0 else (a[l:], c[:-l])
        d = np.sqrt(np.sum(aa ** 2) * np.sum(cc ** 2))
        r = float(np.sum(aa * cc) / d) if d > 0 else 0.0
        if r > best[1]:
            best = (l, r)
    print(f"  {name:11s}: best lag={best[0]:4d} samples = {1000.0*best[0]/SR:6.2f} ms  (normalised corr {best[1]:+.4f})")
print("NOTE: 3 x 480 = 1440 samples is the structural latency (480 dry while the")
print("first frame fills + 2 x 480 for RNNoise's overlap-add analysis/synthesis).")
print("The speech segments also show near-equal correlation aliases at the pitch")
print("period; the noise-only segment (corr > 0.997) pins the delay unambiguously.")
