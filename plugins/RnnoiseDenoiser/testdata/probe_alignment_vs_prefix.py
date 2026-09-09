#!/usr/bin/env python3
"""Alignment check: post-fix out_A vs the backed-up pre-fix out_A.

The pre-fix render's absolute delay vs C was measured at 1439 samples
(normalised xcorr +0.9978 on the noise-only passage).  If the fixed render
aligns with the pre-fix render at lag 0, the absolute latency is unchanged by
the scale fix (the fix only rescales values, it does not touch the
accumulation/output indexing).

Usage: python3 probe_alignment_vs_prefix.py [prefix_dir] [testdata_dir]
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
    pdir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/rnnoise-prefix-wavs"
    td = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(__file__))
    post = read_wav_mono(f"{td}/out_A_denoised.wav")
    pre = read_wav_mono(f"{pdir}/out_A_denoised.wav")
    n = min(len(post), len(pre))
    post, pre = post[:n], pre[:n]

    print("probe_alignment_vs_prefix.py -- post-fix A vs backed-up pre-fix A")
    print(f"  post: {td}/out_A_denoised.wav")
    print(f"  pre : {pdir}/out_A_denoised.wav")
    print("model: post[n] ~= pre[n-L]; report L maximising normalised xcorr")
    for t0, t1, name in [(0.10, 0.70, "speech1 0.10-0.70s"),
                         (0.07, 2.00, "source  0.07-2.00s"),
                         (1.30, 1.90, "speech2 1.30-1.90s")]:
        a, b = seg(post, t0, t1), seg(pre, t0, t1)
        best = (0, -2.0)
        for lag in range(-100, 101):
            if lag >= 0:
                aa, bb = a[lag:], b[:len(b) - lag]
            else:
                aa, bb = a[:len(a) + lag], b[-lag:]
            den = np.sqrt(np.sum(aa ** 2) * np.sum(bb ** 2))
            r = float(np.sum(aa * bb) / den) if den > 0 else 0.0
            if r > best[1]:
                best = (lag, r)
        print(f"  {name}: best lag={best[0]:+4d}  normalised corr={best[1]:+.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
