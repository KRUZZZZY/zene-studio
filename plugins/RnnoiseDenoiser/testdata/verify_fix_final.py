#!/usr/bin/env python3
"""Final verification gate: re-read the regenerated WAVs from scratch.

Deliberately self-contained (no import of measure.py / measure_fix.py) so the
acceptance number is confirmed a third time, independently.

Usage: python3 verify_fix_final.py [testdata_dir]
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
    tag, nch, sr, _br, _al, bits = fmt
    if tag == 3:
        x = np.frombuffer(data, dtype="<f4").astype(np.float64)
    else:
        x = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, nch).mean(axis=1), tag, bits, sr


def db(x):
    r = float(np.sqrt(np.mean(x ** 2)))
    return r, (20.0 * np.log10(r) if r > 0 else float("-inf"))


def main():
    td = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    ch = {}
    for name in ("A_denoised", "B_bypassed", "C_nofx"):
        ch[name], tag, bits, sr = read_wav_mono(f"{td}/out_{name}.wav")
        print(f"out_{name}.wav: fmt_tag={tag} bits={bits} sr={sr} "
              f"len={len(ch[name])} ({len(ch[name])/SR:.4f} s)")
    print()
    print("segment 0.85-1.15 s (noise-only) and 0.10-0.70 s (speech):")
    stats = {}
    for name, x in ch.items():
        nz = x[int(0.85 * SR):int(1.15 * SR)]
        sp = x[int(0.10 * SR):int(0.70 * SR)]
        rn, dn = db(nz)
        rs, ds = db(sp)
        stats[name] = (rn, dn, rs, ds)
        print(f"  {name:11s} noise RMS={rn:.9e} ({dn:+8.2f} dBFS)   "
              f"speech RMS={rs:.9e} ({ds:+8.2f} dBFS)")
    print()
    for pair in (("A_denoised", "C_nofx"), ("B_bypassed", "C_nofx")):
        a, c = pair
        d_noise = stats[a][1] - stats[c][1]
        d_speech = stats[a][3] - stats[c][3]
        print(f"  {a} vs {c}: noise-only delta={d_noise:+.4f} dB   speech delta={d_speech:+.4f} dB")
    print()
    print("ACCEPTANCE (noise-only A-vs-C <= -10 dB): "
          + ("PASS" if (stats['A_denoised'][1] - stats['C_nofx'][1]) <= -10.0 else "FAIL"))
    n = min(len(ch["B_bypassed"]), len(ch["C_nofx"]))
    s = int(0.05 * SR)
    print(f"regression max|B-C| outside 0.05 s startup = "
          f"{np.max(np.abs(ch['B_bypassed'][s:n] - ch['C_nofx'][s:n])):.12f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
