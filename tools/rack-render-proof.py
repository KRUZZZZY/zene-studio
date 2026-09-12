#!/usr/bin/env python3
"""Measure and compare rendered WAVs, in the form this repo's render proofs use.

    python3 tools/rack-render-proof.py measure     <file>
    python3 tools/rack-render-proof.py compare     <fileA> <fileB>
    python3 tools/rack-render-proof.py sumcompare  <fileSum> <fileA> <fileB>

`measure` prints channels, rate, bit depth, frames, seconds, RMS (dBFS), peak,
and the sha256 of the payload.

`compare` prints, for two renders of the same length:

    frames=<n> samples=<n> differing=<n> max_delta_lsb=<n> difference_rms=<x>
    rms_a=<x> rms_b=<x> level_delta_db=<+x>

`sumcompare` compares one render with the *sum* of two others, which is the form
the parallel-chain claim takes: two chains process the same input, so their sum
is the sum of their two renders.

Why not sha256 equality: LMMS's renderer is **not bit-reproducible run to run**
(measured three times, docs/STEM-EXPORT.md section 4.4), so an unchanged render
still moves by an LSB on a frame or two, and occasionally by a dropout episode
of thousands of LSB. Every claim about "the render did not change" is therefore
stated as a max|delta| in LSB against a same-build run-to-run floor, and any
claim that something *did* change needs a delta far above that floor.

LSB means one step of the file's own integer representation: 2**15 for 16-bit
PCM, 2**23 for 24-bit PCM (both relative to full scale). Float WAVs are already
full scale and their LSB is 2**23, the 24-bit grid they are quoted on.
"""

import hashlib
import math
import struct
import sys

#! (unpack format, bytes per sample, divisor to full scale) per (format, depth).
DECODERS = {
    (1, 16): ("<h", 2, 2 ** 15),
    (1, 24): (None, 3, 2 ** 23),
    (1, 32): ("<i", 4, 2 ** 31),
    (3, 32): ("<f", 4, 1.0),
}


def parse_chunks(blob, path):
    if blob[0:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("%s is not a RIFF/WAVE file" % path)
    pos = 12
    fmt = None
    data = None
    while pos + 8 <= len(blob):
        chunk_id = blob[pos:pos + 4]
        size = struct.unpack_from("<I", blob, pos + 4)[0]
        if chunk_id == b"fmt ":
            fmt = blob[pos + 8:pos + 8 + size]
        elif chunk_id == b"data":
            data = blob[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    if fmt is None or data is None:
        raise ValueError("%s has no fmt/data chunk" % path)
    return fmt, data


def decode_samples(data, channels, fmt, frames, path):
    audio_format, rate, bits = fmt
    decoder = DECODERS.get((audio_format, bits))
    if decoder is None:
        raise ValueError("%s: unsupported format %d/%d" % (path, audio_format, bits))
    unpack, count, divisor = decoder

    total = frames * channels
    if unpack is None:  # 24-bit PCM has no struct code
        values = []
        for i in range(total):
            offset = i * 3
            value = data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16)
            values.append((value - 0x1000000 if value & 0x800000 else value) / divisor)
        return values
    unpacked = struct.unpack_from("<%d%s" % (total, unpack[1]), data, 0)
    return [value / divisor for value in unpacked]


def header_facts(fmt):
    audio_format, channels, rate, _byte_rate, _align, bits = struct.unpack_from("<HHIIHH", fmt, 0)
    return audio_format, channels, rate, bits


def stats(samples):
    squares = sum(value * value for value in samples)
    rms = math.sqrt(squares / len(samples)) if samples else 0.0
    dbfs = 20 * math.log10(rms) if rms > 0 else float("-inf")
    return rms, dbfs, max((abs(value) for value in samples), default=0.0)


def read(path):
    """Returns the header facts, the samples as floats in [-1, 1), and the
    payload's sha256."""
    with open(path, "rb") as handle:
        blob = handle.read()
    raw_fmt, data = parse_chunks(blob, path)
    audio_format, channels, rate, bits = header_facts(raw_fmt)

    count = DECODERS.get((audio_format, bits), (None, 0, 1.0))[1]
    frames = len(data) // (count * channels) if count else 0
    samples = decode_samples(data, channels, (audio_format, rate, bits), frames, path)
    rms, dbfs, peak = stats(samples)

    return {
        "path": path,
        "channels": channels,
        "rate": rate,
        "bits": bits,
        "format": audio_format,
        "frames": frames,
        "seconds": frames / rate if rate else 0.0,
        # Float payloads are already full scale; integer ones are quoted on the
        # 24-bit grid the render proofs use.
        "lsb_per_unit": 2 ** (bits - 1) if audio_format == 1 else 2 ** 23,
        "rms": rms,
        "dbfs": dbfs,
        "peak": peak,
        "sha": hashlib.sha256(data).hexdigest(),
        "samples": samples,
    }


def delta_stats(got, want):
    """Worst |difference| in LSB, how many samples differ at all, and the
    difference's RMS."""
    worst = 0.0
    differing = 0
    acc = 0.0
    for value, reference in zip(got, want):
        difference = value - reference
        if difference != 0.0:
            differing += 1
        acc += difference * difference
        worst = max(worst, abs(difference))
    rms = math.sqrt(acc / len(got)) if got else 0.0
    return differing, worst, rms


def level_delta_db(a, b):
    if a["rms"] <= 0 or b["rms"] <= 0:
        return float("nan")
    return 20 * math.log10(b["rms"] / a["rms"])


def measure(path):
    m = read(path)
    print("%s ch=%d rate=%d bits=%d frames=%d %.3fs RMS=%.6f (%.3f dBFS) peak=%.6f sha=%s"
          % (path, m["channels"], m["rate"], m["bits"], m["frames"], m["seconds"],
             m["rms"], m["dbfs"], m["peak"], m["sha"][:32]))
    return m


def compare(path_a, path_b):
    a = read(path_a)
    b = read(path_b)
    if a["channels"] != b["channels"] or a["rate"] != b["rate"] or a["frames"] != b["frames"]:
        print("MISMATCH: %s is %dch/%d/%d frames, %s is %dch/%d/%d frames"
              % (path_a, a["channels"], a["rate"], a["frames"],
                 path_b, b["channels"], b["rate"], b["frames"]))
        return 1

    differing, worst, difference_rms = delta_stats(b["samples"], a["samples"])
    samples = len(a["samples"])
    below = 20 * math.log10(difference_rms / a["rms"]) if a["rms"] > 0 and difference_rms > 0 else float("-inf")
    print("A=%s\nB=%s" % (path_a, path_b))
    print("frames=%d samples=%d differing=%d max_delta_lsb=%d difference_rms=%.6f (%.2f dB below A)"
          % (a["frames"], samples, differing,
             int(worst * max(a["lsb_per_unit"], b["lsb_per_unit"]) + 0.5), difference_rms, below))
    print("rms_a=%.6f (%.3f dBFS) rms_b=%.6f (%.3f dBFS) level_delta_db=%+.5f"
          % (a["rms"], a["dbfs"], b["rms"], b["dbfs"], level_delta_db(a, b)))
    return 0


def sumcompare(path_sum, path_a, path_b):
    total = read(path_sum)
    a = read(path_a)
    b = read(path_b)
    if not (len(total["samples"]) == len(a["samples"]) == len(b["samples"])):
        print("MISMATCH: %d vs %d and %d samples"
              % (len(total["samples"]), len(a["samples"]), len(b["samples"])))
        return 1

    expected = [x + y for x, y in zip(a["samples"], b["samples"])]
    differing, worst, difference_rms = delta_stats(total["samples"], expected)
    lsb = max(a["lsb_per_unit"], b["lsb_per_unit"], total["lsb_per_unit"])
    rms = math.sqrt(sum(value * value for value in expected) / len(expected))
    rms_db = 20 * math.log10(rms) if rms > 0 else float("-inf")

    print("SUM=%s\nA=%s\nB=%s" % (path_sum, path_a, path_b))
    print("frames=%d samples=%d differing=%d max_delta_lsb=%d difference_rms=%.6f"
          % (total["frames"], len(total["samples"]), differing,
             int(worst * lsb + 0.5), difference_rms))
    print("rms_sum=%.6f (%.3f dBFS) rms_a+rms_b=%.6f (%.3f dBFS)"
          % (total["rms"], total["dbfs"], rms, rms_db))
    return 0


def main(argv):
    if len(argv) == 2 and argv[0] == "measure":
        measure(argv[1])
        return 0
    if len(argv) == 3 and argv[0] == "compare":
        return compare(argv[1], argv[2])
    if len(argv) == 4 and argv[0] == "sumcompare":
        return sumcompare(argv[1], argv[2], argv[3])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
