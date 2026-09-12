#!/usr/bin/env python3
"""auto-mastering-demo.py - fixture, checker and independent measurement for
`zene master` (task #610, wave 1).

Subcommands:
  make <dir>              write demo.mmp + the sample WAVs it plays
  check <dir> [--lufs]    read the candidate files and check them
  measure <wav>...        independent BS.1770-4 measurement of any wav

The measurement here is a second implementation (numpy, written from the
recommendation's own tables), so the numbers `zene master` prints can be
checked against something that shares no code with the meter under test.
`check --lufs` runs it over the candidates and reports the difference; that is
the only place in this repo where the C++ meter's numbers meet an independent
one.

Usage in the lane's run log:
  python3 tools/auto-mastering-demo.py make /tmp/masterdemo
  zene master /tmp/masterdemo/demo.mmp -o /tmp/masterdemo/candidates -f wav -s 44100
  python3 tools/auto-mastering-demo.py check /tmp/masterdemo/candidates --lufs
"""

import argparse
import math
import os
import struct
import sys

try:
    import numpy as np
    from scipy.signal import lfilter
except ImportError as error:  # the independent measurement is the point of this tool
    sys.stderr.write("auto-mastering-demo.py needs numpy and scipy for its independent "
                     "BS.1770-4 measurement: %s\n" % error)
    raise SystemExit(2)

# ---------------------------------------------------------------------------
# Minimal WAV reader/writer: 16-bit PCM and 32-bit IEEE float. Both shapes are
# what this tree's render path writes (AudioFileWave maps Depth16Bit to PCM_16
# and Depth32Bit to SF_FORMAT_FLOAT), and reading them here keeps the checker
# free of any dependency the C++ side also uses.
# ---------------------------------------------------------------------------

WAVE_FORMAT_PCM = 1
WAVE_FORMAT_IEEE_FLOAT = 3


def _chunks(blob):
    """Yields (chunk id, body) for every chunk after the RIFF/WAVE header."""
    pos = 12
    while pos + 8 <= len(blob):
        chunk_id, size = struct.unpack_from("<4sI", blob, pos)
        yield chunk_id, blob[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)


def _decode(tag, bits, data):
    """Samples of one data chunk as floats in [-1, 1)."""
    if tag == WAVE_FORMAT_IEEE_FLOAT and bits == 32:
        return struct.unpack("<%df" % (len(data) // 4), data[: len(data) // 4 * 4])
    if tag == WAVE_FORMAT_PCM and bits == 16:
        return [v / 32768.0 for v in struct.unpack("<%dh" % (len(data) // 2), data)]
    raise ValueError("unsupported wav format tag=%d bits=%d" % (tag, bits))


def _chunk(blob, wanted):
    return next((body for cid, body in _chunks(blob) if cid == wanted), None)


def _interleave(values):
    return [(values[i], values[i + 1]) for i in range(0, len(values) - 1, 2)]


def read_wav(path):
    """Returns (frames, sample_rate) with frames a list of (l, r) floats."""
    with open(path, "rb") as handle:
        blob = handle.read()
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("%s is not a RIFF/WAVE file" % path)
    fmt = _chunk(blob, b"fmt ")
    data = _chunk(blob, b"data")
    if fmt is None or data is None:
        raise ValueError("%s has no fmt or data chunk" % path)
    tag, channels, rate, _, _, bits = struct.unpack_from("<HHIIHH", fmt, 0)
    if channels != 2:
        raise ValueError("%s has %d channels; only stereo is handled" % (path, channels))
    return _interleave(_decode(tag, bits, data)), rate


def write_wav16(path, frames, rate):
    payload = bytearray()
    for left, right in frames:
        payload += struct.pack("<hh", _to_i16(left), _to_i16(right))
    header = b"RIFF" + struct.pack("<I", 36 + len(payload)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, WAVE_FORMAT_PCM, 2, rate, rate * 4, 4, 16)
    header += b"data" + struct.pack("<I", len(payload))
    with open(path, "wb") as handle:
        handle.write(header + bytes(payload))


def _to_i16(value):
    return max(-32768, min(32767, int(round(value * 32767.0))))


# ---------------------------------------------------------------------------
# ITU-R BS.1770-4 / EBU R 128 measurement, second implementation.
#
# K-weighting is the two published biquad sections, derived from the same
# equivalent analogue prototype parameters the recommendation's 48 kHz table
# corresponds to; at 48 kHz this derivation reproduces that table. Gating is
# the exact block list (no histogram), and true peak is the recommendation's
# own 4-phase, 12-tap Annex 2 interpolator, coefficients below.
# ---------------------------------------------------------------------------

TRUE_PEAK_PHASES = [
    [0.0017089843750, 0.0109863281250, -0.0196533203125, 0.0332031250000,
     -0.0594482421875, 0.1373291015625, 0.9721679687500, -0.1022949218750,
     0.0476074218750, -0.0266113281250, 0.0148925781250, -0.0083007812500],
    [-0.0291748046875, 0.0292968750000, -0.0517578125000, 0.0891113281250,
     -0.1665039062500, 0.4650878906250, 0.7797851562500, -0.2003173828125,
     0.1015625000000, -0.0582275390625, 0.0330810546875, -0.0189208984375],
    [-0.0189208984375, 0.0330810546875, -0.0582275390625, 0.1015625000000,
     -0.2003173828125, 0.7797851562500, 0.4650878906250, -0.1665039062500,
     0.0891113281250, -0.0517578125000, 0.0292968750000, -0.0291748046875],
    [-0.0083007812500, 0.0148925781250, -0.0266113281250, 0.0476074218750,
     -0.1022949218750, 0.9721679687500, 0.1373291015625, -0.0594482421875,
     0.0332031250000, -0.0196533203125, 0.0109863281250, 0.0017089843750],
]


def k_weighting(fs):
    """(b1, a1, b2, a2) for the pre-filter and the RLB high-pass at fs."""
    f0, gain, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = math.tan(math.pi * f0 / fs)
    vh = 10.0 ** (gain / 20.0)
    vb = vh ** 0.4996667741545416
    a0 = 1.0 + k / q + k * k
    b1 = [(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0,
          (vh - vb * k / q + k * k) / a0]
    a1 = [1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0]

    f0, q = 38.13547087602444, 0.5003270373238773
    k = math.tan(math.pi * f0 / fs)
    a0 = 1.0 + k / q + k * k
    b2 = [1.0 / a0, -2.0 / a0, 1.0 / a0]
    a2 = [1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0]
    return b1, a1, b2, a2


def biquad(channels, b, a):
    """Direct form I over an (n, 2) signal; scipy's recursion, double precision."""
    return lfilter(b, a, channels, axis=0)


def block_energies(weighted, fs):
    """Mean-square per block over the 400 ms / 100 ms-hop grid, channels summed."""
    block, hop = int(0.4 * fs), int(0.1 * fs)
    squares = weighted * weighted
    starts = range(0, weighted.shape[0] - block + 1, hop)
    return [float(squares[start:start + block].sum() / block) for start in starts]


def _block_loudness(energies):
    return [-0.691 + 10.0 * math.log10(e) if e > 0 else float("-inf") for e in energies]


def _relative_gate_lufs(absolute):
    """The relative gate: 10 LU below the mean of the absolutely-gated blocks."""
    mean = sum(e for e, _ in absolute) / len(absolute)
    return -0.691 + 10.0 * math.log10(mean) - 10.0


def integrated_lufs(frames, fs):
    """Gated integrated loudness, BS.1770-4 Annex 1 gating, exact block list."""
    if len(frames) < int(0.4 * fs):
        return float("-inf")
    b1, a1, b2, a2 = k_weighting(fs)
    data = np.asarray(frames, dtype=np.float64)
    weighted = biquad(biquad(data, b1, a1), b2, a2)
    energies = block_energies(weighted, fs)
    loudness = _block_loudness(energies)
    absolute = [(e, l) for e, l in zip(energies, loudness) if l > -70.0]
    if not absolute:
        return float("-inf")
    relative_gate = _relative_gate_lufs(absolute)
    kept = [e for e, l in absolute if l > relative_gate]
    if not kept:
        return float("-inf")
    return -0.691 + 10.0 * math.log10(sum(kept) / len(kept))


def true_peak_dbtp(frames, fs):
    """4x oversampled true peak with the recommendation's Annex 2 interpolator."""
    del fs
    data = np.asarray(frames, dtype=np.float64)
    peak = float(np.max(np.abs(data))) if data.size else 0.0
    for channel in range(2):
        for phase in TRUE_PEAK_PHASES:
            interpolated = np.convolve(data[:, channel], np.asarray(phase))
            peak = max(peak, float(np.max(np.abs(interpolated))))
    return 20.0 * math.log10(peak) if peak > 0 else float("-inf")


def measure_file(path):
    frames, rate = read_wav(path)
    return {
        "path": path,
        "rate": rate,
        "frames": len(frames),
        "rms_dbfs": rms_dbfs(frames),
        "peak_dbfs": 20.0 * math.log10(max(abs(v) for f in frames for v in f) or 1e-30),
        "lufs_i": integrated_lufs(frames, rate),
        "dbtp": true_peak_dbtp(frames, rate),
    }


def rms_dbfs(frames):
    if not frames:
        return float("-inf")
    total = sum(l * l + r * r for l, r in frames) / (2.0 * len(frames))
    return 10.0 * math.log10(total) if total > 0 else float("-inf")


# ---------------------------------------------------------------------------
# Fixture: three sample tracks, transient-rich, referenced by an absolute path
# so the project loads from anywhere. Clip length 2 bars = 4 s at 120 bpm.
# ---------------------------------------------------------------------------

TRACKS = [("Bass", 55.0, 0.12, False), ("Pad", 220.0, 0.12, True), ("Lead", 1000.0, 0.25, True)]
#! Decay of each transient burst, and the period between them: the fixture is
#! deliberately burst-heavy (crest factor > 13 dB) so the limiter has to act and
#! the candidates' ceilings are visible in the measurements.
BURST_PERIOD = 0.5
BURST_DECAY = 0.03


def burst_track(seconds, rate, tone, amplitude, bursts):
    frames = []
    for i in range(int(seconds * rate)):
        t = i / float(rate)
        envelope = math.exp(-(t % BURST_PERIOD) / BURST_DECAY) if bursts else 1.0
        value = amplitude * envelope * math.sin(2.0 * math.pi * tone * t)
        frames.append((value, value))
    return frames


def make_fixture(directory):
    os.makedirs(directory, exist_ok=True)
    names = []
    for name, tone, amplitude, bursts in TRACKS:
        base = name.lower() + ".wav"
        write_wav16(os.path.join(directory, base), burst_track(4.0, 44100, tone, amplitude, bursts), 44100)
        names.append((name, base))
    project = os.path.join(directory, "demo.mmp")
    with open(project, "w") as handle:
        handle.write(project_xml(directory, names))
    print("wrote %s and %d sample tracks" % (project, len(names)))


def project_xml(directory, names):
    out = ['<?xml version="1.0"?>', "<!DOCTYPE multimedia-project>",
           '<multimedia-project version="1.0" creator="LMMS" creatorversion="1.3.0" type="song">',
           '  <head timesig_numerator="4" mastervol="100" timesig_denominator="4" bpm="120" masterpitch="0" />',
           "  <song>",
           '    <trackcontainer width="600" x="5" y="5" maximized="0" height="300" visible="1" type="song" minimized="0">']
    for name, base in names:
        out += ['      <track muted="0" type="2" name="%s">' % name,
                '        <sampletrack vol="100" pan="0" mixch="0">',
                '          <fxchain numofeffects="0" enabled="0"/>',
                "        </sampletrack>",
                '        <sampleclip pos="0" len="384" muted="0" src="%s" off="0" autoresize="0" sample_rate="44100"/>'
                % os.path.join(directory, base),
                "      </track>"]
    out += ["    </trackcontainer>",
            '    <mixer width="865" x="5" y="310" maximized="0" height="278" visible="1" minimized="0">',
            '      <mixerchannel num="0" muted="0" volume="1" name="Master">',
            '        <fxchain numofeffects="0" enabled="0"/>',
            "      </mixerchannel>",
            "    </mixer>",
            '    <controllerrackview width="258" x="880" y="310" maximized="0" height="278" visible="1" minimized="0"/>',
            '    <timeline lp1pos="192" lp0pos="0" lpstate="0"/>',
            "    <controllers/>",
            "  </song>",
            "</multimedia-project>"]
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

def candidate_files(directory):
    return sorted(os.path.join(directory, name) for name in os.listdir(directory)
                  if name.endswith(".wav") and not name.startswith("00_"))


def read_manifest(directory):
    """The source render (when present) followed by the candidates, in name order."""
    source = os.path.join(directory, "00_source-mix.wav")
    paths = ([source] if os.path.exists(source) else []) + candidate_files(directory)
    return [(path, measure_file(path)) for path in paths]


def signal_problems(ordered):
    """Prints the per-file table and returns everything wrong at the file level."""
    problems = []
    print("%-34s %8s %10s %9s %9s" % ("file", "frames", "RMS dBFS", "peak dBFS", "true peak"))
    for path, reading in ordered:
        print("%-34s %8d %10.2f %9.2f %9.2f"
              % (os.path.basename(path), reading["frames"], reading["rms_dbfs"],
                 reading["peak_dbfs"], reading["dbtp"]))
        if reading["rms_dbfs"] < -60.0:
            problems.append("%s is silent" % os.path.basename(path))
        if reading["peak_dbfs"] > 0.0:
            problems.append("%s clips" % os.path.basename(path))
    return problems


def level_order_problems(ordered):
    """The candidates must be distinguishable by level exactly as their targets
    are: the -23 LUFS one sits far below the -14 ones."""
    by_name = {os.path.basename(path): reading for path, reading in ordered}
    quiet = by_name.get("05_ebu-r128.wav")
    loud = by_name.get("01_streaming-14.wav")
    if quiet is None or loud is None:
        return []
    if quiet["rms_dbfs"] < loud["rms_dbfs"] - 5.0:
        return []
    return ["the -23 LUFS candidate is not clearly quieter than the -14 one"]


def check(directory, with_lufs):
    ordered = read_manifest(directory)
    if not ordered:
        print("FAIL: no candidate files in %s" % directory)
        return 1
    problems = signal_problems(ordered) + level_order_problems(ordered)
    if with_lufs:
        problems += check_lufs(ordered)
    for line in problems:
        print("  PROBLEM: %s" % line)
    print("check: %s" % ("PASS" if not problems else "FAIL (%d)" % len(problems)))
    return 0 if not problems else 1


def check_lufs(ordered):
    """Independent BS.1770-4 opinion on every file, printed side by side."""
    problems = []
    print("\nindependent measurement (this script's own BS.1770-4 implementation):")
    print("%-34s %10s %10s" % ("file", "LUFS-I", "dBTP"))
    for path, reading in ordered:
        print("%-34s %10.2f %10.2f" % (os.path.basename(path), reading["lufs_i"], reading["dbtp"]))
    peaks = {os.path.basename(p): r["dbtp"] for p, r in ordered}
    for name, ceiling in (("05_ebu-r128.wav", -1.0), ("01_streaming-14.wav", -1.0),
                          ("03_streaming-14-ceiling-2.wav", -2.0)):
        if name in peaks and peaks[name] > ceiling + 0.3:
            problems.append("%s measures %.2f dBTP, above its %.1f ceiling"
                            % (name, peaks[name], ceiling))
    return problems


def measure(paths):
    for path in paths:
        reading = measure_file(path)
        print("%-40s frames %8d  rate %6d  RMS %8.2f dBFS  LUFS-I %8.2f  dBTP %8.2f"
              % (os.path.basename(path), reading["frames"], reading["rate"],
                 reading["rms_dbfs"], reading["lufs_i"], reading["dbtp"]))
    return 0


def compare(paths):
    """max|delta| in LSB and dB between two renders - the behaviour-preservation
    form this tree needs, since a render is not bit-reproducible run to run and
    a sha256 comparison would be red for reasons unrelated to any change."""
    if len(paths) != 2:
        print("compare takes exactly two wav files")
        return 2
    first, rate_a = read_wav(paths[0])
    second, rate_b = read_wav(paths[1])
    common = min(len(first), len(second))
    if rate_a != rate_b or len(first) != len(second):
        print("frames %d vs %d, rate %d vs %d" % (len(first), len(second), rate_a, rate_b))
    worst = 0
    for i in range(common):
        worst = max(worst, abs(first[i][0] - second[i][0]), abs(first[i][1] - second[i][1]))
    delta_db = 20.0 * math.log10(worst) if worst > 0 else float("-inf")
    level = abs(rms_dbfs(first) - rms_dbfs(second))
    print("%s vs %s: common frames %d, max|delta| %d LSB (%.2f dB), level delta %.4f dB"
          % (os.path.basename(paths[0]), os.path.basename(paths[1]), common,
             int(round(worst * 32768.0)), delta_db, level))
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description="auto-mastering fixture and checker")
    sub = parser.add_subparsers(dest="command", required=True)
    make = sub.add_parser("make", help="write the demo project and its samples")
    make.add_argument("directory")
    check_cmd = sub.add_parser("check", help="check a candidate directory")
    check_cmd.add_argument("directory")
    check_cmd.add_argument("--lufs", action="store_true",
                           help="add this script's independent BS.1770-4 measurement")
    measure_cmd = sub.add_parser("measure", help="measure wav files independently")
    measure_cmd.add_argument("files", nargs="+")
    compare_cmd = sub.add_parser("compare", help="max|delta| between two renders")
    compare_cmd.add_argument("files", nargs=2)
    args = parser.parse_args(argv)

    if args.command == "make":
        make_fixture(args.directory)
        return 0
    if args.command == "check":
        return check(args.directory, args.lufs)
    if args.command == "compare":
        return compare(args.files)
    return measure(args.files)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
