#!/usr/bin/env python3
"""Compare two WAV renders: sha256, sample-level max |delta|, RMS/peak in dBFS.

Usage: compare-renders.py a.wav b.wav [label]

Handles the two encodings `lmms render` produces: IEEE float32 (format tag 3,
what `-a` writes through libsndfile) and 16-bit PCM (tag 1). Python's stdlib
`wave` module refuses tag 3, hence the RIFF walk here.
"""
import hashlib
import struct
import sys

import numpy as np

DB_AMIN = 1e-20


def dbfs(x: float) -> float:
    if x <= 0.0:
        return float("-inf")
    return 20.0 * np.log10(x)


def read_wav(path):
    blob = open(path, "rb").read()
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a RIFF/WAVE file")
    pos, fmt, payload = 12, None, None
    while pos + 8 <= len(blob):
        cid = blob[pos:pos + 4]
        size = struct.unpack("<I", blob[pos + 4:pos + 8])[0]
        body = blob[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            payload = body
        pos += 8 + size + (size & 1)
    if fmt is None or payload is None:
        raise SystemExit(f"{path}: missing fmt or data chunk")
    tag, channels, rate, _byte_rate, _align, bits = fmt
    if tag == 3 and bits == 32:
        samples = np.frombuffer(payload, "<f4").astype(np.float64)
    elif tag == 1 and bits == 16:
        samples = np.frombuffer(payload, "<i2").astype(np.float64) / 32768.0
    elif tag == 1 and bits == 32:
        samples = np.frombuffer(payload, "<i4").astype(np.float64) / 2147483648.0
    else:
        raise SystemExit(f"{path}: unsupported format tag {tag}, {bits} bits")
    return samples, channels, rate, bits


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    a, b = sys.argv[1], sys.argv[2]
    label = sys.argv[3] if len(sys.argv) > 3 else "compare"
    sa, ca, ra, ba = read_wav(a)
    sb, cb, rb, bb = read_wav(b)
    ha, hb = sha256(a), sha256(b)

    print(f"== {label} ==")
    print(f"a: {a}\n   sha256={ha} frames={len(sa)} ch={ca} rate={ra} bits={ba}")
    print(f"b: {b}\n   sha256={hb} frames={len(sb)} ch={cb} rate={rb} bits={bb}")
    print(f"sha256 equal: {ha == hb}")
    if len(sa) != len(sb) or ca != cb:
        print(f"LENGTH MISMATCH: {len(sa)}/{ca} vs {len(sb)}/{cb}")
        return 1

    frame_a = sa.reshape(-1, ca)
    frame_b = sb.reshape(-1, cb)
    peak_a, peak_b = float(np.abs(frame_a).max()), float(np.abs(frame_b).max())
    rms_a = float(np.sqrt(np.mean(frame_a ** 2)))
    rms_b = float(np.sqrt(np.mean(frame_b ** 2)))
    delta = np.abs(frame_a - frame_b)
    peak_delta = float(delta.max())
    differing = int(np.count_nonzero(delta))

    print(f"a: peak={peak_a:.10f} ({dbfs(peak_a):.2f} dBFS) rms={rms_a:.10f} ({dbfs(rms_a):.2f} dBFS)")
    print(f"b: peak={peak_b:.10f} ({dbfs(peak_b):.2f} dBFS) rms={rms_b:.10f} ({dbfs(rms_b):.2f} dBFS)")
    print(f"max |a-b| = {peak_delta:.12e}"
          f"  (rel. to a peak: {dbfs(peak_delta / max(peak_a, DB_AMIN)):.2f} dB,"
          f" rel. to a rms: {dbfs(peak_delta / max(rms_a, DB_AMIN)):.2f} dB)")
    print(f"samples differing: {differing} of {sa.size} "
          f"({100.0 * differing / sa.size:.6f}%)")
    if differing:
        idx = int(np.argmax(delta))
        print(f"worst sample index {idx} (channel {idx % ca}, frame {idx // ca}): "
              f"a={sa[idx]:.12e} b={sb[idx]:.12e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
