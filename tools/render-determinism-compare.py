#!/usr/bin/env python3
"""Compare rendered WAVs sample by sample - the instrument for RENDER-DETERMINISM.md.

Two renders of the *same* project by the *same* binary are not reliably byte-identical
(see docs/RENDER-DETERMINISM.md). sha256 tells you that; it does not tell you how big the
difference is, where it starts, or whether one file is the other shifted by a few frames.
This tool answers those, so a "did this change the render?" question can be stated as a
measured delta against a control render instead of an equality the renderer itself cannot
satisfy.

    python3 tools/render-determinism-compare.py a.wav b.wav [--period 256] [--tsv]

Reports, per pair:

  frames            frame count on both sides
  differing frames  frames where any channel differs at all (bit level)
  first differing   index of the first such frame
  max |delta|       worst per-sample difference, in LSB of the files' own bit depth and
                    in dB relative to full scale
  delta RMS         RMS of the difference signal
  level delta       RMS(b) vs RMS(a) - the quantity the stem-export lane reports
  best lag          frame shift of b that maximises the cross-correlation, and the
                    differing-frame count *with that shift applied*. If the aligned
                    count collapses, the two files are one signal at two start offsets;
                    if it does not, the difference is in the processing.
  period histogram  how the differing frames spread over engine periods
                    (framesPerPeriod defaults to 256, include/AudioEngine.h:54). A few
                    dirty periods with everything else clean is a period-boundary
                    artefact; a long run of dirty periods is not.

numpy is used when importable (FFT alignment, vectorised diffs - it is what makes a
four-million-frame project practical to inspect) with a pure-stdlib fallback, so the tool
still runs on a machine without it, just slower.

Exit status is 0 when compared and 1 when a file cannot be read - *never* a verdict on
whether differing is acceptable. Deciding that is the caller's job, on purpose.
"""

import argparse
import array
import math
import sys
from typing import Any

try:
    import numpy as _np_module

    _np: Any = _np_module
except ImportError:  # pragma: no cover - exercised by the tool's own fallback self-test
    _np = None

PCM, FLOAT = 1, 3


def read_wav(path):
    """Minimal RIFF reader: returns (channels, rate, channels_count, bits, is_float).

    `channels` is a sequence of per-channel sample sequences (numpy arrays when numpy is
    available). Chunk walking rather than the stdlib `wave` module, which refuses
    fmt-tag 3 (32-bit float) files that `lmms render -a` and the lmms-lab MCP's default
    sampler settings both produce.
    """
    with open(path, "rb") as handle:
        blob = handle.read()
    if blob[0:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")
    fmt, data = _chunks(blob)
    if fmt is None or data is None:
        raise ValueError(f"{path}: no fmt/data chunk")
    tag, count, rate, bits = _parse_fmt(fmt, path)
    if tag not in (PCM, FLOAT):
        raise ValueError(f"{path}: unsupported format tag {tag} (need PCM or IEEE float)")
    if count < 1:
        raise ValueError(f"{path}: channel count {count}")
    return _decode(data, count, bits, tag), rate, count, bits, tag == FLOAT


def _chunks(blob):
    """Walk the chunk list; return the fmt and data chunk bodies."""
    fmt = data = None
    pos = 12
    while pos + 8 <= len(blob):
        cid = blob[pos:pos + 4]
        size = int.from_bytes(blob[pos + 4:pos + 8], "little")
        body = blob[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data = body
        pos += 8 + size + (size & 1)
    return fmt, data


def _parse_fmt(fmt, path):
    if len(fmt) < 16:
        raise ValueError(f"{path}: truncated fmt chunk")
    return (int.from_bytes(fmt[0:2], "little"), int.from_bytes(fmt[2:4], "little"),
            int.from_bytes(fmt[4:8], "little"), int.from_bytes(fmt[14:16], "little"))


def _decode(data, channels, bits, tag):
    """De-interleave into one sequence of floats per channel."""
    width = bits // 8
    samples = len(data) // width
    usable = samples - (samples % channels)
    if _np is not None:
        out = _decode_numpy(data, usable, channels, width, bits, tag)
        if out is not None:
            return out
    return _decode_plain(data, usable, channels, width, bits, tag)


def _decode_numpy(data, samples, channels, width, bits, tag):
    dtype = _numpy_dtype(bits, tag)
    if dtype is None:
        return None
    flat = _np.frombuffer(data, dtype=dtype, count=samples)
    if tag == FLOAT:
        flat = flat.astype(_np.float64)
    else:
        flat = flat.astype(_np.float64) / float(1 << (bits - 1))
    return [flat[ch::channels] for ch in range(channels)]


def _numpy_dtype(bits, tag):
    if tag == FLOAT:
        return {32: _np.float32, 64: _np.float64}.get(bits)
    return {16: _np.int16, 32: _np.int32}.get(bits)


def _decode_plain(data, samples, channels, width, bits, tag):
    """Dependency-free path, and the one that handles 24-bit PCM."""
    unpack = _float_decoder(bits) if tag == FLOAT else _pcm_decoder(bits)
    out = []
    for _ in range(channels):
        out.append(array.array("d"))
    stride = width * channels
    for i in range(samples):
        base = i * stride
        for ch in range(channels):
            out[ch].append(unpack(data, base + ch * width))
    return out


def _float_decoder(bits):
    import struct
    if bits == 32:
        packer = struct.Struct("<f")
        return lambda d, o: packer.unpack_from(d, o)[0]
    if bits == 64:
        packer = struct.Struct("<d")
        return lambda d, o: packer.unpack_from(d, o)[0]
    raise ValueError(f"unsupported float bit depth {bits}")


def _pcm_decoder(bits):
    if bits == 16:
        scale = 1.0 / 32768.0
        return lambda d, o: int.from_bytes(d[o:o + 2], "little", signed=True) * scale
    if bits == 24:
        scale = 1.0 / 8388608.0
        return lambda d, o: int.from_bytes(d[o:o + 3], "little", signed=True) * scale
    if bits == 32:
        scale = 1.0 / 2147483648.0
        return lambda d, o: int.from_bytes(d[o:o + 4], "little", signed=True) * scale
    raise ValueError(f"unsupported PCM bit depth {bits}")


def frame_count(channels):
    return int(min(len(ch) for ch in channels))


def lsb(bits):
    return 1.0 / float(1 << (bits - 1))


def db(x):
    return -math.inf if x <= 0 else 20.0 * math.log10(float(x))


def rms(channels):
    if _np is not None:
        total = float(sum(float(_np.sum(_np.square(_np.asarray(ch)))) for ch in channels))
        n = frame_count(channels) * len(channels)
        return math.sqrt(total / n) if n else 0.0
    total, n = 0.0, 0
    for ch in channels:
        for v in ch:
            total += float(v) * float(v)
            n += 1
    return math.sqrt(total / n) if n else 0.0


def diff_stats(a, b):
    """(differing frames, max |delta|, sum of squares, first differing frame)."""
    n = min(frame_count(a), frame_count(b))
    if _np is None:
        return _diff_stats_plain(a, b, n)
    return _diff_stats_numpy(a, b, n)


def _diff_stats_numpy(a, b, n):
    worst = _np.zeros(n)
    for ch in range(min(len(a), len(b))):
        _np.maximum(worst, _np.abs(_np.asarray(a[ch][:n]) - _np.asarray(b[ch][:n])),
                    out=worst)
    dirty = _np.flatnonzero(worst)
    peak = float(worst.max()) if n else 0.0
    first = int(dirty[0]) if dirty.size else -1
    return int(dirty.size), peak, float(_np.sum(_np.square(worst))), first


def _diff_stats_plain(a, b, n):
    differing, peak, energy, first = 0, 0.0, 0.0, -1
    for i in range(n):
        worst = _frame_worst(a, b, i)
        if worst > 0.0:
            differing += 1
            if first < 0:
                first = i
        energy += worst * worst
        if worst > peak:
            peak = worst
    return differing, peak, energy, first


def _frame_worst(a, b, index):
    worst = 0.0
    for ch in range(min(len(a), len(b))):
        delta = abs(float(a[ch][index]) - float(b[ch][index]))
        if delta > worst:
            worst = delta
    return worst


def period_histogram(a, b, period):
    """(count of dirty periods, frames in them, [(period index, frames), ...] worst first)."""
    n = min(frame_count(a), frame_count(b))
    buckets = {}
    for start in range(0, n, period):
        differing, _, _, _ = diff_stats(
            [ch[start:start + period] for ch in a],
            [ch[start:start + period] for ch in b])
        if differing:
            buckets[start // period] = differing
    top = sorted(buckets.items(), key=lambda kv: -kv[1])[:5]
    return len(buckets), sum(buckets.values()), top


def aligned_differing(a, b, lag):
    """Frames still differing once b is shifted by `lag` (b[i+lag] against a[i]).

    This is the number the clip-model lane reports as "best-aligned differing samples":
    if it collapses to ~0 the two renders are the same signal at two start offsets, and
    the difference is a start-offset jitter rather than a processing difference.
    """
    n = min(frame_count(a), frame_count(b))
    lo, hi = max(0, -lag), min(n, n - lag)
    if hi <= lo:
        return 0
    if _np is not None:
        worst = _np.zeros(hi - lo, dtype=bool)
        for ch in range(min(len(a), len(b))):
            worst |= (_np.asarray(a[ch][lo:hi]) != _np.asarray(b[ch][lo + lag:hi + lag]))
        return int(worst.sum())
    count = 0
    for i in range(lo, hi):
        for ch in range(min(len(a), len(b))):
            if a[ch][i] != b[ch][i + lag]:
                count += 1
                break
    return count


def best_lag(a, b, span=16384):
    """Frame shift of b that maximises the correlation with a, and the aligned residue."""
    if _np is not None:
        lag = _best_lag_fft(a[0], b[0], span)
    else:
        lag = _best_lag_plain(a, b, span)
    return lag, aligned_differing(a, b, lag)


def _best_lag_fft(left, right, span):
    x = _np.asarray(left, dtype=_np.float64)
    y = _np.asarray(right, dtype=_np.float64)
    n = int(min(x.size, y.size))
    if n == 0:
        return 0
    size = 1 << max(1, (2 * n - 1)).bit_length()
    # irfft(X * conj(Y))[k] == sum_i x[i] y[i-k], so index k is lag -k in the
    # b[i+lag] convention aligned_differing uses. Checked against the no-numpy path on a
    # synthetic 3-frame shift: both must report +3.
    corr = _np.fft.irfft(_np.fft.rfft(x[:n], size) * _np.conj(_np.fft.rfft(y[:n], size)), size)
    lags = _np.arange(-min(span, n - 1), min(span, n - 1) + 1)
    return -int(lags[int(_np.argmax(_np.abs(corr[lags % size]))) ])


def _best_lag_plain(a, b, span):
    """No-numpy fallback: scan shifts at `step` granularity, then refine to the frame."""
    n = min(frame_count(a), frame_count(b))
    step = max(1, n // 16384)
    coarse = min(span, n - 1) // step
    best = min(range(-coarse, coarse + 1),
               key=lambda k: _absdiff(a[0], b[0], k * step, n))
    return min(range(best * step - step, best * step + step + 1),
               key=lambda k: _absdiff(a[0], b[0], k, n))


def _absdiff(x, y, lag, n):
    """Mean |x[i] - y[i+lag]| over the probed frames; a large sentinel with no overlap.

    The half-overlap floor matters: without it a lag so extreme that one single sample
    happens to agree scores a perfect 0 and wins the scan (that is how this returned
    -8191 for a 3-frame shift in the fallback's own self-test).
    """
    stride = max(1, n // 8192)
    total, count, probed = 0.0, 0, 0
    for i in range(0, n, stride):
        probed += 1
        j = i + lag
        if 0 <= j < n:
            total += abs(float(x[i]) - float(y[j]))
            count += 1
    return 1e30 if count < max(1, probed // 2) else total / count


TSV_COLUMNS = ("left", "right", "frames", "differing", "first", "max_lsb", "max_dbfs",
               "delta_rms_dbfs", "level_delta_db", "lag", "aligned_differing",
               "dirty_periods", "periods", "worst_period")


def compare(left, right, period):
    """Measure one pair; returns a dict of the numbers both printers use."""
    a, rate_a, ch_a, bits_a, _ = read_wav(left)
    b, rate_b, ch_b, bits_b, _ = read_wav(right)
    n = min(frame_count(a), frame_count(b))
    differing, peak, energy, first = diff_stats(a, b)
    lag, aligned = best_lag(a, b)
    dirty, _, top = period_histogram(a, b, period)
    return {
        "left": left, "right": right, "path": left.rsplit("/", 1)[-1],
        "other": right.rsplit("/", 1)[-1], "frames": n,
        "header_ok": (rate_a, ch_a, bits_a) == (rate_b, ch_b, bits_b),
        "rate": rate_a, "channels": ch_a, "bits": bits_a,
        "differing": differing, "first": first,
        "max_lsb": peak / lsb(bits_a), "max_dbfs": db(peak),
        "delta_rms_dbfs": db(math.sqrt(energy / max(1, n))),
        "level_delta_db": db(rms(b)) - db(rms(a)),
        "lag": lag, "aligned_differing": aligned,
        "dirty_periods": dirty, "periods": (n + period - 1) // period,
        "worst_period": top[0][1] if top else 0, "period": period,
    }


def print_report(stats):
    print(f"{stats['path']}  vs  {stats['other']}")
    print(f"  frames              : {stats['frames']} ({stats['rate']} Hz, "
          f"{stats['channels']} ch, {stats['bits']} bit, header match={stats['header_ok']})")
    print(f"  differing frames    : {stats['differing']}  "
          f"({100.0 * stats['differing'] / max(1, stats['frames']):.6f} %)")
    print(f"  first differing at  : {stats['first']}")
    print(f"  max |delta|         : {stats['max_lsb']:.0f} LSB   ({stats['max_dbfs']:+.2f} dBFS)")
    print(f"  delta RMS           : {stats['delta_rms_dbfs']:+.2f} dBFS")
    print(f"  level delta         : {stats['level_delta_db']:+.6f} dB")
    print(f"  best lag            : {stats['lag']} frames -> {stats['aligned_differing']} "
          f"differing frames (unshifted {stats['differing']})")
    print(f"  periods ({stats['period']}f) dirty : {stats['dirty_periods']} of "
          f"{stats['periods']}, worst period {stats['worst_period']} frames")


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("wavs", nargs="+")
    parser.add_argument("--period", type=int, default=256,
                        help="engine period in frames (framesPerPeriod, default 256)")
    parser.add_argument("--tsv", action="store_true",
                        help="print one machine-readable row per pair instead of a report")
    args = parser.parse_args(argv)
    if len(args.wavs) < 2:
        parser.error("need at least two WAVs")
    status = 0
    if args.tsv:
        print("\t".join(TSV_COLUMNS))
    for other in args.wavs[1:]:
        try:
            stats = compare(args.wavs[0], other, args.period)
        except ValueError as exc:
            print(f"ERROR: {exc}")
            status = 1
            continue
        if args.tsv:
            print("\t".join(str(stats[key]) for key in TSV_COLUMNS))
        else:
            print_report(stats)
            print()
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
