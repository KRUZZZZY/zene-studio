#!/usr/bin/env python3
"""The golden-audio programme's measurement core: LSB/dB deltas against a measured floor.

WHY A MEASURED FLOOR AND NOT A HASH (docs/RENDER-DETERMINISM.md, docs/AUTO-MASTERING.md:251):

    the renders this tree produces are NOT bit-reproducible run to run.  Two renders of the
    same project by the same binary differ on up to 98.3 % of their frames, by one LSB in the
    mildest case.  So `sha256(a) == sha256(b)` is not a test - it fails on a correct build -
    and "before/after by one number" is not a test either: a loudness or RMS measurement
    cannot see the recorded non-determinism at all ("loudness never moves",
    RENDER-DETERMINISM.md section 4), while a difference a change really made can sit below
    one whole-file number's resolution.

WHAT THIS MODULE MEASURES, in both units, for every pair:

    max |delta|      the worst per-sample difference, in LSB *and* in dBFS
    differing frames how many frames differ at all (1 LSB counts), and the first one
    delta RMS        the RMS of the difference signal, in dBFS
    level delta      whole-file dBFS(a) - dBFS(b) - the "did the level move" question
    envelope delta   the worst per-window |dBFS(a) - dBFS(b)| at a fixed window length

and the same for a FINGERPRINT (one render, no second file): the per-window RMS and peak
envelopes plus the whole-file numbers.  The fingerprint is what a committed record can hold
- tests/evidence-gate.sh refuses committed renders - so the golden reference is a
MEASUREMENT of a render with its provenance, never the bytes.

THE TOLERANCE MODEL.  Every term's tolerance is twice that term's own SAME-BUILD run-to-run
spread - the floor, measured by rendering the same fixture N times in this build and
comparing EVERY pair - floored at a stated resolution (1 LSB, 0.01 dB).  The multiplier is a
policy choice and is declared HERE, not implied: with N runs the measured spread is a
left-censored estimate of the distribution's tail, so the verdict uses a margin over it.
The floor is the measurement; the margin is a decision.

The instrument's own control lives in tests/golden_audio_selftest.py; the committed baseline
and the CLI live in tests/golden_audio_record.py (the split tests/freeze_bounce_evidence.py
made, for the same Gate 7 reason).
"""

import array
import hashlib
import math
import struct
import sys

PCM, FLOAT, EXTENSIBLE = 1, 3, 0xFFFE

# One LSB of a 16-bit render as a fraction of full scale: -90.31 dBFS.  This is the
# programme's reference LSB.  For a PCM render it is the file's own LSB (1/2^(bits-1)), and
# for an IEEE-float render - which has no LSB - it is the 16-bit one, stated here so a float
# and a 16-bit render of the same fixture report comparable numbers.
REFERENCE_LSB = 1.0 / 32768.0

# The verdict's own resolution floors: below these, a measured delta is not a finding.
LSB_FLOOR = 1.0            # one LSB of a 16-bit render
DB_FLOOR = 0.01            # one hundredth of a dB

# The margin over the measured floor (see the tolerance model above).
MARGIN = 2.0

# The envelope: at least 50 ms per window, at most 128 windows per file, so a two-second
# fixture and a two-minute one both record a comparable, small vector.
MIN_WINDOW_SECONDS = 0.05
MAX_WINDOWS = 128


class WavError(Exception):
    """Raised for a file this programme cannot measure."""


def _numpy():
    """numpy when importable (a multi-million-frame render is impractical without it)."""
    try:
        import numpy
        return numpy
    except ImportError:  # pragma: no cover - the stdlib fallback must work without it
        return None


def _chunks(blob, path):
    if len(blob) < 12 or blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise WavError("%s: not a RIFF/WAVE file" % path)
    fmt, data, ids = None, [], []
    pos = 12
    while pos + 8 <= len(blob):
        cid = blob[pos:pos + 4]
        size = struct.unpack_from("<I", blob, pos + 4)[0]
        body = blob[pos + 8:pos + 8 + size]
        ids.append(cid.decode("latin1"))
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data.append(body)
        pos += 8 + size + (size & 1)
    if fmt is None or not data:
        raise WavError("%s: no fmt and/or data chunk (%s)" % (path, "".join(ids)))
    return fmt, b"".join(data)


def _parse_fmt(fmt, path):
    if len(fmt) < 16:
        raise WavError("%s: fmt chunk shorter than 16 bytes" % path)
    tag, channels, rate, _, _, bits = struct.unpack_from("<HHIIHH", fmt, 0)
    if tag == EXTENSIBLE:
        if len(fmt) < 26:
            raise WavError("%s: WAVE_FORMAT_EXTENSIBLE fmt chunk too short" % path)
        tag = struct.unpack_from("<H", fmt, 24)[0]
    return tag, channels, rate, bits


_NUMPY_DTYPE = {(FLOAT, 32): "float32", (FLOAT, 64): "float64", (PCM, 8): "u1",
                (PCM, 16): "i2", (PCM, 32): "i4"}
_ARRAY_TYPECODE = {(FLOAT, 32): "f", (FLOAT, 64): "d", (PCM, 8): "B", (PCM, 16): "h",
                   (PCM, 32): "i"}


def read_wav(path):
    """Metadata plus one flat, interleaved, full-scale float sample sequence."""
    with open(path, "rb") as handle:
        blob = handle.read()
    fmt, data = _chunks(blob, path)
    tag, channels, rate, bits = _parse_fmt(fmt, path)
    if channels < 1:
        raise WavError("%s: fmt declares %d channels" % (path, channels))
    frames = len(data) // ((bits // 8) * channels)
    scale = 1.0 if tag == FLOAT else float(1 << (bits - 1))
    return {
        "path": path,
        "fmt_tag": tag,
        "bits": bits,
        "channels": channels,
        "samplerate": rate,
        "frames": frames,
        "lsb": (1.0 / float(1 << (bits - 1))) if tag == PCM else REFERENCE_LSB,
        "data_sha256": hashlib.sha256(data).hexdigest(),
        "flat": _decode(data, (tag, bits), frames * channels, path, scale),
    }


def _decode(data, key, count, path, scale):
    """The data chunk as full-scale floats.  numpy first; array.array is the fallback."""
    numpy = _numpy()
    if key in _NUMPY_DTYPE:
        if numpy is not None:
            return (numpy.frombuffer(data, dtype=numpy.dtype(_NUMPY_DTYPE[key]),
                                     count=count).astype(numpy.float64) / scale)
        values = array.array(_ARRAY_TYPECODE[key])
        values.frombytes(data[:count * values.itemsize])
        if sys.byteorder == "big":
            values.byteswap()
        return array.array("d", [float(v) / scale for v in values])
    if key == (PCM, 24):
        # 24-bit PCM has no array.array typecode and no numpy dtype: unpack by hand.
        out = array.array("d", bytes(8 * count))
        for i in range(count):
            out[i] = int.from_bytes(data[i * 3:i * 3 + 3], "little", signed=True) / scale
        return out
    raise WavError("%s: unsupported WAVE format (fmt tag=%s, %s bits); this programme reads "
                   "PCM 8/16/24/32 and IEEE float 32/64" % (path, key[0], key[1]))


def _channel(wav, index):
    """One channel's samples, strided out of the interleaved flat sequence."""
    return wav["flat"][index::wav["channels"]]


def _rms(values):
    if not len(values):
        return 0.0
    if hasattr(values, "mean"):                     # numpy
        return float(math.sqrt(float((values * values).mean())))
    return math.sqrt(sum(float(v) * float(v) for v in values) / len(values))


def dbfs(value):
    return float("-inf") if value <= 0.0 else 20.0 * math.log10(value)


def dbfs_of_lsb(lsb_value):
    """The dB equivalent of an LSB tolerance, so the LSB and dB terms agree."""
    return dbfs(lsb_value * REFERENCE_LSB)


def window_frames_for(samplerate, frames):
    """>= MIN_WINDOW_SECONDS per window, at most MAX_WINDOWS windows."""
    return max(int(samplerate * MIN_WINDOW_SECONDS) or 1, -(-frames // MAX_WINDOWS))


def envelope(wav):
    """(per-window RMS in dBFS, per-window peak in full scale) of the whole file."""
    window_frames = window_frames_for(wav["samplerate"], wav["frames"])
    levels, peaks = [], []
    for start in range(0, wav["frames"], window_frames):
        chunk = wav["flat"][start * wav["channels"]:
                            min(start + window_frames, wav["frames"]) * wav["channels"]]
        levels.append(dbfs(_rms(chunk)))
        peaks.append(abs(float(chunk.max())) if hasattr(chunk, "max")
                     else max((abs(float(v)) for v in chunk), default=0.0))
    return levels, peaks


def _delta_db(level_a, level_b):
    """level_a - level_b in dB, or None when either side is silent (-inf).

    A silent file has no level, and -inf - -inf is nan: the honest answer is "undefined",
    which the verdict reports as undefined rather than as agreement or as a failure.
    """
    if level_a == float("-inf") or level_b == float("-inf"):
        return None
    return level_a - level_b


def compare(path_a, path_b):
    """Every measured term of one pair of renders.  Never raises on a difference."""
    a, b = read_wav(path_a), read_wav(path_b)
    numpy = _numpy()
    frames = min(a["frames"], b["frames"])
    channels = min(a["channels"], b["channels"])
    peak, differing_samples, first = 0.0, 0, None
    # Two different counts, because they answer different questions and this programme has
    # been bitten by the difference: `differing_samples` counts every channel's every sample,
    # `differing_frames` counts a frame ONCE when any channel differs (the frame count is what
    # the render-determinism table in docs/RENDER-DETERMINISM.md reports, and a stereo file
    # makes the two differ by up to 2x).
    different_frames = (numpy.zeros(frames, dtype=bool) if numpy is not None
                        else bytearray(frames))
    sum_abs = sum_sq = 0.0
    for channel in range(channels):
        sa = _channel(a, channel)[:frames]
        sb = _channel(b, channel)[:frames]
        if numpy is not None and hasattr(sa, "size"):
            delta = numpy.abs(sa - sb)
            if delta.size:
                peak = max(peak, float(delta.max()))
            dirty = numpy.flatnonzero(delta)
            differing_samples += int(dirty.size)
            if dirty.size:
                different_frames[dirty] = True
            sum_abs += float(delta.sum())
            sum_sq += float((delta * delta).sum())
        else:
            for i in range(frames):
                value = abs(float(sa[i]) - float(sb[i]))
                if value > 0.0:
                    differing_samples += 1
                    different_frames[i] = 1
                    if first is None or i < first:
                        first = i
                sum_abs += value
                sum_sq += value * value
                if value > peak:
                    peak = value
    differing_frames = (int(different_frames.sum()) if numpy is not None
                        else sum(different_frames))
    if differing_frames and first is None:
        first = int(numpy.flatnonzero(different_frames)[0]) if numpy is not None \
            else different_frames.index(1)
    a_levels, _ = envelope(a)
    b_levels, _ = envelope(b)
    windows = min(len(a_levels), len(b_levels))
    worst_env = 0.0
    for index in range(windows):
        if a_levels[index] == float("-inf") or b_levels[index] == float("-inf"):
            continue
        worst_env = max(worst_env, abs(a_levels[index] - b_levels[index]))
    lsb = a["lsb"]
    total = channels * frames
    level_a, level_b = dbfs(_rms(a["flat"])), dbfs(_rms(b["flat"]))
    return {
        "wav_a": path_a,
        "wav_b": path_b,
        "frames_compared": frames,
        "channels_compared": channels,
        "differing_samples": differing_samples,
        "differing_frames": differing_frames,
        "first_diff_frame": first,
        "max_abs_delta": peak,
        "max_delta_lsb": peak / lsb,
        "max_delta_dbfs": dbfs(peak),
        "mean_abs_delta_lsb": (sum_abs / total / lsb) if total else 0.0,
        "delta_rms_dbfs": dbfs(math.sqrt(sum_sq / total) if total else 0.0),
        "level_delta_db": _delta_db(level_a, level_b),
        "dbfs_a": level_a,
        "dbfs_b": level_b,
        "identical_bytes": (a["data_sha256"] == b["data_sha256"]
                            and a["frames"] == b["frames"]),
        "envelope": {"window_frames": window_frames_for(a["samplerate"], frames),
                     "windows": windows, "max_delta_db": worst_env},
    }


def file_dbfs(path):
    """(frames, whole-file RMS in dBFS) for one render, through THIS module's reader.

    The stdlib `wave` module refuses an IEEE-float WAV ("unknown format: 3") and mis-scales
    anything that is not 8/16/32-bit PCM, and the export presets can change both.  Reading
    through `read_wav` means a fixture measured here is measured the same way whatever the
    export settings were.
    """
    wav = read_wav(path)
    return wav["frames"], dbfs(_rms(wav["flat"]))


def fingerprint(path, binary_sha256=""):
    """A committable MEASUREMENT of one render - the golden reference is this, not bytes."""
    wav = read_wav(path)
    levels, peaks = envelope(wav)
    return {
        "path": path,
        "frames": wav["frames"],
        "samplerate": wav["samplerate"],
        "channels": wav["channels"],
        "bits": wav["bits"],
        "fmt_tag": wav["fmt_tag"],
        "data_sha256": wav["data_sha256"],
        "window_frames": window_frames_for(wav["samplerate"], wav["frames"]),
        "env_dbfs": levels,
        "peak_env": peaks,
        "rms_dbfs": dbfs(_rms(wav["flat"])),
        "peak_dbfs": dbfs(max(peaks) if peaks else 0.0),
        "binary_sha256": binary_sha256,
    }


# ---------------------------------------------------------------------------
# the tolerance model
# ---------------------------------------------------------------------------
def tolerance(floor):
    """The verdict's tolerances, one per term, each from that term's own measured floor.

    `floor` carries the same-build run-to-run spread of every term, measured by rendering
    the fixture N times in ONE build and comparing every pair (`measure_floor`).
    """
    return {
        "lsb": max(MARGIN * abs(float(floor.get("max_delta_lsb", 0.0))), LSB_FLOOR),
        "db": max(MARGIN * abs(float(floor.get("level_delta_db") or 0.0)), DB_FLOOR),
        "envelope_db": max(
            MARGIN * abs(float((floor.get("envelope") or {}).get("max_delta_db", 0.0))),
            DB_FLOOR),
    }


def verdict(measured, tol):
    """Every term judged against its own tolerance.  Returns (passed, [term lines]).

    The dBFS term is compared in dBFS, not in magnitude: both sides are negative (a
    tolerance of 1 LSB is -90.31 dBFS), so `measured <= limit` is the direction that holds
    for a silent pair (-inf) and for a real difference alike.  A silent pair has NO level
    delta - None - which is printed as undefined and cannot fail the run: a programme that
    called "undefined" agreement would be lying.
    """
    level = measured["level_delta_db"]
    checks = [
        ("max |delta|", "%+.3f LSB" % measured["max_delta_lsb"], measured["max_delta_lsb"],
         tol["lsb"]),
        ("max |delta|", "%.2f dBFS" % measured["max_delta_dbfs"],
         measured["max_delta_dbfs"], dbfs_of_lsb(tol["lsb"])),
        ("level delta", "undefined (silent file)" if level is None else "%+.6f dB" % level,
         None if level is None else abs(level), tol["db"]),
        ("envelope delta", "%.6f dB" % measured["envelope"]["max_delta_db"],
         measured["envelope"]["max_delta_db"], tol["envelope_db"]),
    ]
    lines, passed = [], True
    for name, shown, value, limit in checks:
        if value is None:
            lines.append("  %-15s %-24s limit %-12.6g n/a" % (name, shown, limit))
            continue
        ok = value <= limit
        passed = passed and ok
        lines.append("  %-15s %-24s limit %-12.6g %s"
                     % (name, shown, limit, "ok" if ok else "FAIL"))
    return passed, lines


def measure_floor(paths):
    """The same-build run-to-run floor: EVERY pair of N renders, worst case per term.

    N runs give C(N,2) pairs.  The floor is the worst value any pair reached - not the
    spread of one before/after pair, which is what makes it an estimate of the build's
    noise rather than a sample of one.
    """
    pairs = []
    for i in range(len(paths)):
        for j in range(i + 1, len(paths)):
            measured = compare(paths[i], paths[j])
            measured["pair"] = (i, j)
            pairs.append(measured)
    if not pairs:
        return None
    worst_delta = max(pairs, key=lambda p: p["max_delta_lsb"])
    levelled = [p for p in pairs if p["level_delta_db"] is not None]
    worst_level = max(levelled, key=lambda p: abs(p["level_delta_db"])) if levelled else None
    worst_env = max(pairs, key=lambda p: p["envelope"]["max_delta_db"])
    return {
        "runs": len(paths),
        "pairs": len(pairs),
        "max_delta_lsb": worst_delta["max_delta_lsb"],
        "max_delta_dbfs": worst_delta["max_delta_dbfs"],
        "differing_frames": worst_delta["differing_frames"],
        "first_diff_frame": worst_delta["first_diff_frame"],
        "level_delta_db": worst_level["level_delta_db"] if worst_level else None,
        "envelope": {"max_delta_db": worst_env["envelope"]["max_delta_db"],
                     "window_frames": worst_delta["envelope"]["window_frames"],
                     "windows": worst_delta["envelope"]["windows"]},
        "identical_bytes": all(p["identical_bytes"] for p in pairs),
        "worst_pair": worst_delta["pair"],
        "per_pair": [{"pair": p["pair"], "max_delta_lsb": p["max_delta_lsb"],
                      "differing_frames": p["differing_frames"],
                      "level_delta_db": p["level_delta_db"],
                      "envelope_db": p["envelope"]["max_delta_db"]} for p in pairs],
    }


if __name__ == "__main__":
    print("golden_audio_lib is the measurement core; its CLI is")
    print("  python3 tests/golden_audio_record.py {compare|floor|fingerprint|record} ...")
    print("and its own control is")
    print("  python3 tests/golden_audio_selftest.py")
    sys.exit(2)
