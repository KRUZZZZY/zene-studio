#!/usr/bin/env python3
"""Measure the A/B/C RNNoise denoiser renders (AI-KOS #559 runtime test).

Compares:
  out_A_denoised.wav   - plugin present, on="1" wet="1"  (active)
  out_B_bypassed.wav   - plugin present, on="0"          (Effect::m_enabledModel bypass)
  out_C_nofx.wav       - plugin absent from the FX chain (control)

Source: speechlike_noise_2s_48k.wav (48 kHz mono, 2.0 s)
  speech segment : 0.00-0.80 s and 1.20-2.00 s
  noise-only     : 0.80-1.20 s (white noise, RMS -45 dBFS)

All numbers are computed from the actual rendered WAVs with numpy + stdlib wave.
"""
import wave
import numpy as np

TD = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata"
SR = 48000


def read_wav(path):
    """Minimal RIFF reader: handles 16-bit PCM (fmt 1) and 32-bit float (fmt 3),
    since Python 3.11's stdlib wave module rejects WAVE_FORMAT_IEEE_FLOAT."""
    import struct
    with open(path, "rb") as f:
        raw = f.read()
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
    assert fmt is not None and data is not None, path
    tag, n_ch, sr, _brate, _align, bits = fmt
    if tag == 3 and bits == 32:
        x = np.frombuffer(data, dtype="<f4")
    elif tag == 1 and bits == 16:
        x = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    elif tag == 1 and bits == 32:
        x = np.frombuffer(data, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"{path}: unsupported WAV fmt tag={tag} bits={bits}")
    return x.reshape(-1, n_ch), sr


def db(x):
    r = np.sqrt(np.mean(x ** 2))
    return -np.inf if r == 0 else 20.0 * np.log10(r)


def seg(x, t0, t1):
    return x[int(t0 * SR):int(t1 * SR)]


files = {
    "A_denoised": f"{TD}/out_A_denoised.wav",
    "B_bypassed": f"{TD}/out_B_bypassed.wav",
    "C_nofx":     f"{TD}/out_C_nofx.wav",
}
data = {}
print("=" * 78)
print("PER-FILE MEASUREMENTS (stereo renders; stats computed on mono mixdown)")
print("=" * 78)
for name, path in files.items():
    x, sr = read_wav(path)
    assert sr == SR, (name, sr)
    m = x.mean(axis=1)          # mono mixdown
    data[name] = m
    peak = np.max(np.abs(m))
    rms_all = db(m)
    rms_sp = db(seg(m, 0.10, 0.70))
    rms_nz = db(seg(m, 0.85, 1.15))
    ch_diff = np.max(np.abs(x[:, 0] - x[:, 1])) if x.shape[1] > 1 else 0.0
    print(f"{name:12s} frames={len(m):7d}  peak={peak:.6f} ({20*np.log10(peak):7.2f} dBFS)  "
          f"RMS(all)={rms_all:8.2f}  RMS(speech 0.1-0.7s)={rms_sp:8.2f}  "
          f"RMS(noise-only 0.85-1.15s)={rms_nz:8.2f}  max|L-R|={ch_diff:.2e}")

print()
print("=" * 78)
print("A-B / A-C / B-C DIFFERENCES (dB, negative = B quieter than A)")
print("=" * 78)
for a, b in [("A_denoised", "B_bypassed"), ("A_denoised", "C_nofx"), ("B_bypassed", "C_nofx")]:
    ma, mb = data[a], data[b]
    n = min(len(ma), len(mb))
    ma, mb = ma[:n], mb[:n]
    d_all = db(ma) - db(mb)
    d_sp = db(seg(ma, 0.10, 0.70)) - db(seg(mb, 0.10, 0.70))
    d_nz = db(seg(ma, 0.85, 1.15)) - db(seg(mb, 0.85, 1.15))
    maxdiff = np.max(np.abs(ma - mb))
    print(f"{a:11s} vs {b:10s}: RMS(all) {d_all:+8.2f} dB | speech {d_sp:+8.2f} dB | "
          f"noise-only {d_nz:+8.2f} dB | max|sample diff| {maxdiff:.6e}")

print()
print("=" * 78)
print("BYPASS FIDELITY: is B sample-identical to C (no plugin)?")
print("=" * 78)
mb, mc = data["B_bypassed"], data["C_nofx"]
n = min(len(mb), len(mc))
print(f"max|B-C| over {n} frames = {np.max(np.abs(mb[:n]-mc[:n])):.6e}")
print(f"frames identical: {np.array_equal(mb[:n], mc[:n])}")

print()
print("=" * 78)
print("PLUGIN LATENCY CHECK: lag of max cross-correlation A vs C")
print("=" * 78)
ma, mc = data["A_denoised"], data["C_nofx"]
n = min(len(ma), len(mc))
a = ma[:n] - ma[:n].mean()
c = mc[:n] - mc[:n].mean()
# search lags 0..1000
lags = np.arange(0, 1001)
xc = np.array([np.dot(a[l:], c[: n - l]) for l in lags])
lag = int(lags[np.argmax(xc)])
print(f"argmax lag (A delayed vs C) = {lag} frames = {1000.0*lag/SR:.2f} ms  "
      f"(480 frames = 10 ms = RNNoise frame)")

print()
print("=" * 78)
print("SOURCE SANITY: does the render contain the source signal?")
print("=" * 78)
src, ssr = read_wav(f"{TD}/speechlike_noise_2s_48k.wav")
src = src.mean(axis=1)
print(f"source: frames={len(src)} sr={ssr} peak={np.max(np.abs(src)):.4f} RMS={db(src):.2f} dBFS")
mc = data["C_nofx"]
# source occupies 0..2.0 s of the render; compare aligned windows
w = src
r = mc[: len(w)]
gain = np.dot(r, w) / np.dot(w, w)
resid = r - gain * w
print(f"best-fit gain of render C vs source = {gain:.6f} (linear), "
      f"residual RMS = {db(resid):.2f} dBFS vs source RMS {db(w):.2f} dBFS")
