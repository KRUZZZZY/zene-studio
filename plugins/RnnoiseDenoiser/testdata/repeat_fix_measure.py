#!/usr/bin/env python3
"""Measure the noise-only passage of repeated A/C renders (post-fix spread).

Usage: python3 repeat_fix_measure.py /tmp/rnnoise-fix-repeat
"""
import glob
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
    tag, nch, _sr, _br, _al, _bits = fmt
    x = (np.frombuffer(data, dtype="<f4").astype(np.float64) if tag == 3
         else np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0)
    return x.reshape(-1, nch).mean(axis=1)


def noise_db(x):
    s = x[int(0.85 * SR):int(1.15 * SR)]
    r = float(np.sqrt(np.mean(s ** 2)))
    return 20.0 * np.log10(r) if r > 0 else float("-inf")


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "/tmp/rnnoise-fix-repeat"
    a_files = sorted(glob.glob(f"{d}/A_*.wav"), key=lambda p: int(p.rsplit("_", 1)[1][:-4]))
    c_files = sorted(glob.glob(f"{d}/C_*.wav"), key=lambda p: int(p.rsplit("_", 1)[1][:-4]))
    if not a_files or not c_files:
        print("no renders found"); return 1
    c_db = [noise_db(read_wav_mono(p)) for p in c_files]
    c_ref = float(np.mean(c_db))
    print(f"C (no-FX) noise-only dBFS per run: {['%+.2f' % v for v in c_db]}  "
          f"(spread {max(c_db) - min(c_db):.2f} dB, mean {c_ref:+.2f})")
    print()
    print("A (plugin) noise-only dBFS and delta vs C mean:")
    deltas = []
    for p in a_files:
        d_db = noise_db(read_wav_mono(p))
        delta = d_db - c_ref
        deltas.append(delta)
        print(f"  {os.path.basename(p)}: {d_db:+8.2f} dBFS   A-vs-C {delta:+7.2f} dB")
    print()
    print(f"A-vs-C delta across {len(deltas)} runs: min {min(deltas):+.2f} dB, "
          f"max {max(deltas):+.2f} dB, mean {np.mean(deltas):+.2f} dB, "
          f"spread {max(deltas) - min(deltas):.2f} dB")
    print(f"all runs pass acceptance (<= -10 dB): "
          f"{'YES' if max(deltas) <= -10.0 else 'NO'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
