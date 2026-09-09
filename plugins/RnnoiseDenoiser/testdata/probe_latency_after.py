#!/usr/bin/env python3
"""Confirm the plugin's A-vs-C latency AFTER the scale fix, independent of the
xcorr-argmax probe in measure_fix.py (which weakens once the noise-only
passage is attenuated by ~24 dB).

Method: for each candidate lag L, build the delayed reference
    cd[n] = C[n - L]   (zero-padded)
and compute the relative residual ||A - cd|| / ||A|| over a region.  The
minimum marks the delay.  Reported for the speech passages (where the
denoiser preserves the signal) and for the whole source region.

Usage: python3 probe_latency_after.py [testdata_dir]
"""
import os
import struct
import sys

import numpy as np

SR = 48000


def read_wav_mono(path):
    raw = open(path, "rb").read()
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(raw):
        cid, size = struct.unpack_from("<4sI", raw, pos)
        body = raw[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    tag, nch, _sr, _br, _al, bits = fmt
    x = (np.frombuffer(data, dtype="<f4").astype(np.float64) if tag == 3
         else np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0)
    return x.reshape(-1, nch).mean(axis=1)


def seg(x, t0, t1):
    return x[int(round(t0 * SR)):int(round(t1 * SR))]


def main():
    td = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    A = read_wav_mono(f"{td}/out_A_denoised.wav")
    C = read_wav_mono(f"{td}/out_C_nofx.wav")
    n = min(len(A), len(C))
    A, C = A[:n], C[:n]

    print("probe_latency_after.py -- residual-minimising delay estimate (A vs C)")
    print("model: A[n] ~= C[n-L]; report L minimising ||A - shift(C,L)|| / ||A||")
    for t0, t1, name in [(0.10, 0.70, "speech1 0.10-0.70s"),
                         (1.30, 1.90, "speech2 1.30-1.90s"),
                         (0.07, 2.00, "source  0.07-2.00s")]:
        s, e = int(round(t0 * SR)), int(round(t1 * SR))
        a = A[s:e]
        na = np.sqrt(np.sum(a ** 2))
        best = (None, 1e9)
        for lag in range(1200, 1700):
            cd = np.zeros(e - s)
            src_lo, src_hi = s - lag, e - lag
            lo = max(0, src_lo)
            hi = min(n, src_hi)
            if hi > lo:
                cd[lo - src_lo:hi - src_lo] = C[lo:hi]
            r = np.sqrt(np.sum((a - cd) ** 2)) / (na if na > 0 else 1.0)
            if r < best[1]:
                best = (lag, r)
        r1439 = None
        for lag in (1439,):
            cd = np.zeros(e - s)
            src_lo, src_hi = s - lag, e - lag
            lo, hi = max(0, src_lo), min(n, src_hi)
            cd[lo - src_lo:hi - src_lo] = C[lo:hi]
            r1439 = np.sqrt(np.sum((a - cd) ** 2)) / na
        print(f"  {name}: best lag={best[0]}  rel residual={best[1]:.6f}  "
              f"| at lag 1439: rel residual={r1439:.6f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
