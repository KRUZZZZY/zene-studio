#!/usr/bin/env python3
"""Independent A-B-C measurement for the RNNoise denoiser scale fix (AI-KOS #559).

Self-contained and deliberately independent of testdata/measure.py: parses the
float32 WAVs byte-for-byte with struct (Python's wave module rejects
WAVE_FORMAT_IEEE_FLOAT), mixes to mono, and reports:
  * RMS/dBFS per passage (speech 0.10-0.70 s, noise-only 0.85-1.15 s,
    speech2 1.30-1.90 s) for A (effect active), B (effect bypassed), C (no FX)
  * the A-vs-C deltas (the acceptance metric is the noise-only one)
  * the B-vs-C regression (max|diff| outside the host startup window)
  * the A-vs-C delay via normalised cross-correlation

Usage: python3 measure_fix.py [label] [testdata_dir]
"""
import os
import struct
import sys

import numpy as np

SR = 48000
STARTUP_FRAMES = 3360  # 70 ms host startup window (RUNTIME-TEST.md section 4.2)
SEGMENTS = [
    ("speech1  0.10-0.70s", 0.10, 0.70),
    ("NOISE-ONLY 0.85-1.15s", 0.85, 1.15),
    ("speech2  1.30-1.90s", 1.30, 1.90),
]
BANDS = [(0, 200), (200, 1000), (1000, 4000), (4000, 8000),
         (8000, 16000), (16000, 24000)]


def read_wav_mono(path):
    """Parse RIFF/WAVE directly; return mono float64 samples + sample rate."""
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"{path}: not RIFF/WAVE")
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(raw):
        cid, size = struct.unpack_from("<4sI", raw, pos)
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError(f"{path}: missing fmt/data chunk")
    tag, nch, sr, _brate, _align, bits = fmt
    if tag == 3 and bits == 32:
        x = np.frombuffer(data, dtype="<f4").astype(np.float64)
    elif tag == 1 and bits == 16:
        x = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    else:
        raise ValueError(f"{path}: unsupported fmt tag={tag} bits={bits}")
    return x.reshape(-1, nch).mean(axis=1), sr


def rms(x):
    return float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0


def dbfs(x):
    r = rms(x)
    return float("-inf" if r == 0.0 else 20.0 * np.log10(r))


def seg(x, t0, t1):
    return x[int(round(t0 * SR)):int(round(t1 * SR))]


def best_lag(a, b, max_lag=2000):
    """Normalised xcorr: maximise corr(a[n], b[n-lag]) over lag in [0,max_lag)."""
    best = (0, -2.0)
    for lag in range(max_lag):
        aa = a if lag == 0 else a[lag:]
        bb = b[:len(b) - lag]
        den = np.sqrt(np.sum(aa ** 2) * np.sum(bb ** 2))
        r = float(np.sum(aa * bb) / den) if den > 0 else 0.0
        if r > best[1]:
            best = (lag, r)
    return best


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else "run"
    td = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(__file__))
    A, sr = read_wav_mono(f"{td}/out_A_denoised.wav")
    B, _ = read_wav_mono(f"{td}/out_B_bypassed.wav")
    C, _ = read_wav_mono(f"{td}/out_C_nofx.wav")
    if sr != SR:
        raise SystemExit(f"unexpected sample rate {sr}")
    n = min(len(A), len(B), len(C))

    print(f"### measure_fix.py -- independent RIFF parse -- {label}")
    print(f"files: out_A_denoised.wav out_B_bypassed.wav out_C_nofx.wav "
          f"({n} frames @ {sr} Hz, mono mixdown)")
    print()
    print(f"{'file':10s} {'peak':>10s} {'RMS dBFS':>10s}")
    for name, x in (("A", A), ("B", B), ("C", C)):
        print(f"{name:10s} {np.max(np.abs(x)):10.6f} {dbfs(x):10.2f}")
    print()
    print(f"{'segment':26s} {'A dBFS':>9s} {'B dBFS':>9s} {'C dBFS':>9s} "
          f"{'A-C':>8s} {'A-B':>8s}")
    for name, t0, t1 in SEGMENTS:
        a, b, c = dbfs(seg(A, t0, t1)), dbfs(seg(B, t0, t1)), dbfs(seg(C, t0, t1))
        print(f"{name:26s} {a:9.2f} {b:9.2f} {c:9.2f} {a - c:+8.2f} {a - b:+8.2f}")

    print()
    print("ACCEPTANCE (noise-only A-vs-C delta <= -10 dB):")
    a_n, c_n = dbfs(seg(A, 0.85, 1.15)), dbfs(seg(C, 0.85, 1.15))
    delta = a_n - c_n
    verdict = "PASS" if delta <= -10.0 else "FAIL"
    print(f"  A noise-only={a_n:.2f} dBFS  C noise-only={c_n:.2f} dBFS  "
          f"delta={delta:+.2f} dB  -> {verdict}")

    print()
    print("B-vs-C regression (bypass must equal no-FX outside the startup window):")
    m = min(len(B), len(C))
    d_all = np.max(np.abs(B[:m] - C[:m]))
    d_tail = np.max(np.abs(B[STARTUP_FRAMES:m] - C[STARTUP_FRAMES:m]))
    print(f"  whole file      max|B-C| = {d_all:.10f}")
    print(f"  frames>={STARTUP_FRAMES}    max|B-C| = {d_tail:.10f}")

    print()
    print("A-vs-C delay (normalised xcorr, best lag over 0..1999 samples):")
    for name, t0, t1 in SEGMENTS:
        lag, r = best_lag(seg(A, t0, t1), seg(C, t0, t1))
        print(f"  {name:26s} best lag={lag:4d} ({1000.0 * lag / SR:6.2f} ms) "
              f"corr={r:+.4f}")

    print()
    print("noise-only band levels (dB A/C):")
    sa, sc = seg(A, 0.85, 1.15), seg(C, 0.85, 1.15)
    win = np.hanning(len(sa))
    wa = np.abs(np.fft.rfft(sa * win))
    wc = np.abs(np.fft.rfft(sc * win))
    f = np.fft.rfftfreq(len(sa), 1 / SR)
    for f0, f1 in BANDS:
        mask = (f >= f0) & (f < f1)
        r = 20 * np.log10(np.sqrt(np.mean(wa[mask] ** 2)) /
                          max(np.sqrt(np.mean(wc[mask] ** 2)), 1e-30))
        print(f"  {f0:6d}-{f1:6d} Hz: {r:+8.2f} dB")
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
