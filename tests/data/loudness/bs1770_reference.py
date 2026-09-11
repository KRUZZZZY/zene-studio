#!/usr/bin/env python3
"""Independent BS.1770-4 / EBU R128 measurement of a WAV file.

Used as the cross-check for the loudness report Zene Studio writes: it is a
separate implementation of the same recommendation (numpy, written from the
published text), so when both agree on the same rendered file the value is not
one implementation agreeing with itself.

Differences from the C++ meter, deliberately (so the two are not the same
algorithm twice):
  * K-weighting uses the *published* 48 kHz coefficient rows of BS.1770-4
    Table 1 verbatim (the C++ meter derives them from the analogue prototype
    and asserts the derivation reproduces the table).
  * Gating keeps every 400 ms block loudness in a list and gates over that list
    (the C++ meter accumulates a 0.05 LU histogram, FFmpeg-style).
  * Blocks are computed by a strided sum over a full-length energy array, not
    by a ring of 100 ms sub-blocks.

Usage:
  python3 bs1770_reference.py <file.wav> [--target -23.0] [--tolerance 0.5]
"""

import argparse
import struct
import sys

import numpy as np

# BS.1770-4 Table 1, 48 kHz, as published.
PRE_B = np.array([1.53512485958697, -2.69169618940638, 1.19839281085285])
PRE_A = np.array([1.0, -1.69065929318241, 0.73248077421585])
RLB_B = np.array([1.0, -2.0, 1.0])
RLB_A = np.array([1.0, -1.99004745483398, 0.99007225036621])

# BS.1770-4 Annex 2, the published order-48 / 4-phase interpolation filter.
TRUE_PEAK_COEFFS = np.array([
    [0.0017089843750, -0.0291748046875, -0.0189208984375, -0.0083007812500],
    [0.0109863281250, 0.0292968750000, 0.0330810546875, 0.0148925781250],
    [-0.0196533203125, -0.0517578125000, -0.0582275390625, -0.0266113281250],
    [0.0332031250000, 0.0891113281250, 0.1015625000000, 0.0476074218750],
    [-0.0594482421875, -0.1665039062500, -0.2003173828125, -0.1022949218750],
    [0.1373291015625, 0.4650878906250, 0.7797851562500, 0.9721679687500],
    [0.9721679687500, 0.7797851562500, 0.4650878906250, 0.1373291015625],
    [-0.1022949218750, -0.2003173828125, -0.1665039062500, -0.0594482421875],
    [0.0476074218750, 0.1015625000000, 0.0891113281250, 0.0332031250000],
    [-0.0266113281250, -0.0582275390625, -0.0517578125000, -0.0196533203125],
    [0.0148925781250, 0.0330810546875, 0.0292968750000, 0.0109863281250],
    [-0.0083007812500, -0.0189208984375, -0.0291748046875, 0.0017089843750],
]).T  # shape (4 phases, 12 taps)


def read_wav(path):
    """IEEE-float32 WAV -> (samples[frames, channels], sample_rate)."""
    with open(path, "rb") as fh:
        raw = fh.read()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a RIFF/WAVE file")
    offset, fmt, data = 12, None, None
    while offset + 8 <= len(raw):
        chunk_id = raw[offset:offset + 4]
        size = struct.unpack_from("<I", raw, offset + 4)[0]
        body = raw[offset + 8:offset + 8 + size]
        if chunk_id == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif chunk_id == b"data":
            data = body
        offset += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise SystemExit(f"{path}: missing fmt or data chunk")
    audio_format, channels, sample_rate, _, _, bits = fmt
    if audio_format == 3 and bits == 32:
        values = np.frombuffer(data, dtype="<f4").astype(np.float64)
    elif audio_format == 1 and bits == 16:
        values = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
    else:
        raise SystemExit(f"{path}: unsupported format {audio_format} / {bits} bit")
    return values.reshape(-1, channels), sample_rate


def biquad(x, b, a):
    y = np.empty_like(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, sample in enumerate(x):
        out = b[0] * sample + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1 = x1, sample
        y2, y1 = y1, out
        y[i] = out
    return y


def block_loudness(weighted, sample_rate, block_s=0.4, hop_s=0.1, mono_weight=False):
    """Ungated loudness of every 400 ms block on the 100 ms grid (LUFS)."""
    block = int(block_s * sample_rate)
    hop = int(hop_s * sample_rate)
    energy = np.sum(weighted ** 2, axis=1)          # per frame, channels summed
    cumulative = np.concatenate([[0.0], np.cumsum(energy)])
    starts = np.arange(0, len(energy) - block + 1, hop)
    powers = (cumulative[starts + block] - cumulative[starts]) / block
    with np.errstate(divide="ignore"):
        return -0.691 + 10.0 * np.log10(powers, where=powers > 0.0,
                                        out=np.full_like(powers, -np.inf))


def integrated_lufs(blocks):
    """Absolute gate at -70 LUFS, then the relative gate 10 LU below the mean."""
    above = blocks[blocks > -70.0]
    if above.size == 0:
        return float("-inf")
    threshold = -0.691 + 10.0 * np.log10(np.mean(10.0 ** ((above + 0.691) / 10.0))) - 10.0
    kept = above[above > threshold]
    if kept.size == 0:
        return float("-inf")
    return -0.691 + 10.0 * np.log10(np.mean(10.0 ** ((kept + 0.691) / 10.0)))


def true_peak_dbtp(samples):
    """4x oversampled peak, Annex 2 filter; includes the raw samples."""
    peak = float(np.max(np.abs(samples))) if samples.size else 0.0
    for channel in range(samples.shape[1]):
        x = samples[:, channel]
        upsampled = np.zeros(x.size * 4)
        upsampled[::4] = x
        for phase in range(4):
            y = np.convolve(upsampled, TRUE_PEAK_COEFFS[phase], mode="full")
            peak = max(peak, float(np.max(np.abs(y))))
    return 20.0 * np.log10(peak) if peak > 0.0 else float("-inf")


def measure(path):
    samples, sample_rate = read_wav(path)
    if sample_rate != 48000:
        print(f"warning: {sample_rate} Hz input; the published K-weighting rows "
              f"are for 48 kHz", file=sys.stderr)
    weighted = np.empty_like(samples)
    for channel in range(samples.shape[1]):
        stage1 = biquad(samples[:, channel], PRE_B, PRE_A)
        weighted[:, channel] = biquad(stage1, RLB_B, RLB_A)

    blocks = block_loudness(weighted, sample_rate)
    short_term = block_loudness(weighted, sample_rate, block_s=3.0)
    return {
        "integrated_lufs": integrated_lufs(blocks),
        "short_term_max_lufs": float(np.max(short_term)) if short_term.size else float("-inf"),
        "sample_peak_dbfs": (20.0 * np.log10(np.max(np.abs(samples)))
                             if np.max(np.abs(samples)) > 0.0 else float("-inf")),
        "true_peak_dbtp": true_peak_dbtp(samples),
        "frames": samples.shape[0],
        "channels": samples.shape[1],
        "sample_rate": sample_rate,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav")
    parser.add_argument("--target", type=float, default=-23.0)
    parser.add_argument("--tolerance", type=float, default=0.5)
    args = parser.parse_args()

    measured = measure(args.wav)
    print(f"file                 = {args.wav}")
    print(f"frames               = {measured['frames']} "
          f"({measured['sample_rate']} Hz, {measured['channels']} ch)")
    print(f"integrated_lufs      = {measured['integrated_lufs']:.4f}")
    print(f"short_term_max_lufs  = {measured['short_term_max_lufs']:.4f}")
    print(f"sample_peak_dbfs     = {measured['sample_peak_dbfs']:.4f}")
    print(f"true_peak_dbtp       = {measured['true_peak_dbtp']:.4f}")
    integrated = measured["integrated_lufs"]
    if np.isfinite(integrated):
        deviation = integrated - args.target
        verdict = "PASS" if abs(deviation) <= args.tolerance else "WARN"
        print(f"deviation_lu         = {deviation:+.4f}")
        print(f"verdict              = {verdict}")
    else:
        print("deviation_lu         = n/a")
        print("verdict              = NOT MEASURED (no measurable signal)")


if __name__ == "__main__":
    main()
