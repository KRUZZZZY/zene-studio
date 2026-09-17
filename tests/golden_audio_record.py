#!/usr/bin/env python3
"""The golden-audio RECORD: the measured floor, the golden fingerprint, the provenance.

tests/evidence-gate.sh refuses committed renders (a WAV beside a test is a run's output,
and the durable value of a render is its measurement, not its megabytes - the same owner
decision that produced tests/evidence-manifest.tsv).  So the golden reference this
programme ships is this file: one row per (fixture, render path) carrying

  * the SAME-BUILD run-to-run floor, measured by rendering the fixture N times in one build
    and comparing EVERY pair - max |delta| in LSB and dBFS, the differing frames, the worst
    level and envelope deltas, and whether any pair was byte-identical;
  * the golden render's own fingerprint - the per-window RMS and peak envelopes and the
    whole-file numbers - which is what a later build is compared against;
  * the identity of the build it was measured on (the binary's sha256) and when.

A row's floor IS its tolerance source: `record_tolerance` rebuilds the tolerance dict from
the row's own numbers, so a verify run cannot accidentally use a tolerance that belongs to
another fixture or another build.

CLI (so every number here can be re-derived by hand):

    python3 tests/golden_audio_record.py compare a.wav b.wav [c.wav ...]
    python3 tests/golden_audio_record.py floor a.wav b.wav c.wav      # the run-to-run floor
    python3 tests/golden_audio_record.py fingerprint x.wav
    python3 tests/golden_audio_record.py record                       # what is committed
"""

import argparse
import json
import os
import sys
from datetime import datetime, timezone

import golden_audio_lib as G

RECORD_COLUMNS = (
    "fixture", "path", "command", "runs", "frames", "samplerate", "channels", "bits",
    "floor_max_lsb", "floor_max_dbfs", "floor_differing_frames", "floor_level_db",
    "floor_env_db", "floor_identical_bytes", "rms_dbfs", "peak_dbfs", "window_frames",
    "env_dbfs", "peak_env", "binary_sha256", "measured_utc",
)

RECORD_FILE = "golden-audio-record.tsv"


def default_record_path():
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), RECORD_FILE)


def read_record(path=None):
    """({ (fixture, path): row }, [comment lines]) from the committed record."""
    path = path or default_record_path()
    rows, comments = {}, []
    if not os.path.exists(path):
        return rows, comments
    with open(path) as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line:
                continue
            if line.startswith("#"):
                comments.append(line)
                continue
            fields = line.split("\t")
            if fields[0] == RECORD_COLUMNS[0]:
                continue                       # the column header
            if len(fields) != len(RECORD_COLUMNS):
                raise G.WavError("%s: %d fields, expected %d: %r"
                                 % (path, len(fields), len(RECORD_COLUMNS), line[:120]))
            row = dict(zip(RECORD_COLUMNS, fields))
            rows[(row["fixture"], row["path"])] = row
    return rows, comments


def row_from(fixture, path, command, floor, print_, measured_utc):
    """One record row: the measured floor, the golden fingerprint, the build identity."""
    return {
        "fixture": fixture,
        "path": path,
        "command": command,
        "runs": str(floor["runs"]),
        "frames": str(print_["frames"]),
        "samplerate": str(print_["samplerate"]),
        "channels": str(print_["channels"]),
        "bits": str(print_["bits"]),
        "floor_max_lsb": "%.6f" % floor["max_delta_lsb"],
        "floor_max_dbfs": "%.4f" % floor["max_delta_dbfs"],
        "floor_differing_frames": str(floor["differing_frames"]),
        "floor_level_db": "%.6f" % (floor["level_delta_db"] or 0.0),
        "floor_env_db": "%.6f" % floor["envelope"]["max_delta_db"],
        "floor_identical_bytes": "yes" if floor["identical_bytes"] else "no",
        "rms_dbfs": "%.4f" % print_["rms_dbfs"],
        "peak_dbfs": "%.4f" % print_["peak_dbfs"],
        "window_frames": str(print_["window_frames"]),
        "env_dbfs": ",".join("%.4f" % v for v in print_["env_dbfs"]),
        "peak_env": ",".join("%.6f" % v for v in print_["peak_env"]),
        "binary_sha256": print_.get("binary_sha256", ""),
        "measured_utc": measured_utc,
    }


def write_record(rows, provenance, path=None):
    """Write the record.  Sorted by (fixture, path) so a re-measure diffs as a diff."""
    path = path or default_record_path()
    with open(path, "w") as handle:
        for line in provenance:
            handle.write("# %s\n" % line)
        handle.write("\t".join(RECORD_COLUMNS) + "\n")
        for row in sorted(rows, key=lambda r: (r["fixture"], r["path"])):
            handle.write("\t".join(row[column] for column in RECORD_COLUMNS) + "\n")
    return path


def record_tolerance(row):
    """The tolerance a record row declares, as the same dict `G.tolerance()` builds."""
    return G.tolerance({
        "max_delta_lsb": float(row["floor_max_lsb"]),
        "level_delta_db": float(row["floor_level_db"]),
        "envelope": {"max_delta_db": float(row["floor_env_db"])},
    })


def judge_measurement(check, label, floor, print_, row, echo=print):
    """Judge ONE (fixture, path) measurement against its record row.  Returns [lines].

    Two checks, both stated with their numbers, never one before/after value:

      * the FLOOR - this run's same-build run-to-run spread against the floor the record
        says this fixture has.  The limit is the programme's own rule (twice the recorded
        floor, floored at 1 LSB); a build whose renders are noisier than that is a finding
        about the build or the box, and it is reported rather than absorbed.
      * the GOLDEN - the candidate's fingerprint (envelope, peak envelope, level, frame
        count) against the record's, at the record's own tolerance.
    """
    recorded = float(row["floor_max_lsb"])
    limit = max(G.MARGIN * recorded, G.LSB_FLOOR)
    check("%s: the same-build floor is within the recorded one's tolerance" % label,
          floor["max_delta_lsb"] <= limit,
          "measured %.3f LSB, recorded %.3f LSB (limit %.3f = 2x, floored at 1 LSB)"
          % (floor["max_delta_lsb"], recorded, limit))
    echo("  floor vs record      : measured %.3f LSB (%.2f dBFS), recorded %s LSB on "
         "build %s" % (floor["max_delta_lsb"], floor["max_delta_dbfs"], row["floor_max_lsb"],
                       row["binary_sha256"][:16] or "unrecorded"))
    passed, lines = compare_fingerprints(print_, row)
    echo("  golden vs record:")
    for line in lines:
        echo("  %s" % line)
    check("%s: the render still measures what the record's golden says" % label, passed,
          "recorded rms %.4f dBFS, now %.4f dBFS" % (float(row["rms_dbfs"]),
                                                     print_["rms_dbfs"]))


def measure_floor(paths, label, command, echo=print):
    """The same-build floor over EVERY pair of N renders, printed with its evidence.

    The floor is the worst value any pair reached - not the spread of one before/after pair -
    which is what makes it an estimate of the build's noise rather than a sample of one.  The
    per-pair lines are printed because "the floor was X" is only an honest claim next to the
    values it came from.
    """
    floor = G.measure_floor(paths)
    echo("")
    echo("=== %s (%s): %d runs, floor over %d pairs ==="
         % (label, command, len(paths), floor["pairs"]))
    echo("  renders              : %s"
         % ", ".join(os.path.basename(path) for path in paths))
    echo("  floor max |delta|    : %.3f LSB (%.2f dBFS)"
         % (floor["max_delta_lsb"], floor["max_delta_dbfs"]))
    echo("  floor differing frms : %d (first %s)"
         % (floor["differing_frames"], floor["first_diff_frame"]))
    echo("  floor level delta    : %+.6f dB" % (floor["level_delta_db"] or 0.0))
    echo("  floor envelope delta : %.6f dB" % floor["envelope"]["max_delta_db"])
    echo("  byte-identical pairs : %s of %d" % (floor["identical_bytes"], floor["pairs"]))
    for pair in floor["per_pair"]:
        echo("    runs %s: %.3f LSB, %d frames differ, level %+.6f dB"
             % (pair["pair"], pair["max_delta_lsb"], pair["differing_frames"],
                pair["level_delta_db"] or 0.0))
    return floor


def rows_for(measurements, commands, binary_sha256):
    """The record rows for a run's measurements, keyed as the record keys them.

    `measurements` is a list of the dicts the harness builds per (fixture, path): the runs,
    the floor and the command.  `commands` maps the path's short name to its command id.
    """
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    return [row_from(measured["fixture"], measured["part"],
                     commands[measured["part"]], measured["floor"],
                     G.fingerprint(measured["runs"][0], binary_sha256), now)
            for measured in measurements]


def provenance(binary_sha256, runs, fixture_lines):
    """The record's header: what wrote it, what the fixtures are, and on which build."""
    return [
        "golden-audio record - the measured same-build run-to-run floor and the golden",
        "fingerprint, per fixture and headline path.",
        "Written by tests/control-golden-audio.py --write-record; judged by the ctest",
        "ControlGoldenAudio.  NEVER rewrite a row to make a lane green: this record IS the",
        "baseline the programme compares against, and a floor rewritten to fit a result is",
        "a disabled test.",
    ] + list(fixture_lines) + [
        "binary sha256 %s, %s" % (binary_sha256,
                                  datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")),
        "runs %d per path; each floor is the worst value over every pair of those runs"
        % runs,
    ]


def compare_fingerprints(print_, row):
    """The golden term: an envelope comparison of one render against a record row.

    Terms: the per-window RMS envelope (the level answer at a fixed window length), the
    per-window peak envelope (so a shape change that preserves window energy is not
    invisible) and the whole-file level.  The tolerance is the row's own measured floor.
    A trailing line names the window that reached the worst PEAK envelope delta, with the
    record's value, the measured value and the signed delta (measured - record).  A term
    that fails must say which window it failed on and in which direction.
    """
    tol = record_tolerance(row)
    record_env = [float(v) for v in row["env_dbfs"].split(",")]
    record_peak = [float(v) for v in row["peak_env"].split(",")]
    now_env = [v for v in print_["env_dbfs"]]
    now_peak = [v for v in print_["peak_env"]]
    worst_env = 0.0
    for index in range(min(len(record_env), len(now_env))):
        if record_env[index] == float("-inf") or now_env[index] == float("-inf"):
            continue
        worst_env = max(worst_env, abs(record_env[index] - now_env[index]))
    # The per-window PEAK is compared in LSB, not in dB, and that is a measured correction
    # rather than a preference: on the bundled fixture (floor 14 276 LSB, 98 % of frames
    # differing) the peak of a window can move a whole transient, and the dB of a window
    # whose peak happens to sit near a zero crossing swings by tens of dB - the first
    # version of this file failed its own golden check with a 2.69 dB peak delta against a
    # 1.67 dB limit.  A peak difference is an amplitude difference; the LSB tolerance is the
    # one the floor measured.
    #
    # The window that reaches that worst value is NAMED, not merely measured: on
    # macos-arm64 this term failed with `peak envelope delta 1748.015 LSB` while every other
    # term was inside its own tolerance, and the bare LSB number could not say WHICH of the
    # windows moved or in which direction.  The index and both of the window's values ride
    # in the same `max()` that already picks the worst window, so this adds no second pass,
    # no branch and no CCN (the ratchet for this function is 17).  On an exact tie the
    # higher index wins: it is still a worst window, and every reported value belongs to it.
    worst_peak, worst_peak_at, worst_peak_pair = 0.0, -1, (0.0, 0.0)
    for index, pair in enumerate(zip(record_peak, now_peak)):    # zip stops at the shorter
        worst_peak, worst_peak_at, worst_peak_pair = max(        # list: exactly the overlap
            (worst_peak, worst_peak_at, worst_peak_pair),        # the window-count term
            (abs(pair[0] - pair[1]), index, pair))               # already reports on
    peak_lsb = worst_peak / G.REFERENCE_LSB
    level = None
    if float(row["rms_dbfs"]) != float("-inf") and print_["rms_dbfs"] != float("-inf"):
        level = print_["rms_dbfs"] - float(row["rms_dbfs"])
    checks = [
        ("frames", "%d vs %s" % (print_["frames"], row["frames"]),
         abs(print_["frames"] - int(row["frames"])), 0),
        ("window count", "%d vs %d" % (len(now_env), len(record_env)),
         abs(len(now_env) - len(record_env)), 0),
        ("envelope delta", "%.6f dB" % worst_env, worst_env, tol["envelope_db"]),
        ("peak envelope delta", "%.3f LSB" % peak_lsb, peak_lsb, tol["lsb"]),
        ("level delta", "undefined (silent file)" if level is None else "%+.6f dB" % level,
         None if level is None else abs(level), tol["db"]),
    ]
    lines, passed = [], True
    for name, shown, value, limit in checks:
        if value is None:
            lines.append("  %-20s %-24s limit %-12.6g n/a" % (name, shown, limit))
            continue
        ok = value <= limit
        passed = passed and ok
        lines.append("  %-20s %-24s limit %-12.6g %s"
                     % (name, shown, limit, "ok" if ok else "FAIL"))
    # ... and the window the worst value belongs to, so a FAIL names it and the direction
    # (a positive delta means the measured window's peak sits ABOVE the record's).
    lines.append(
        "  %-20s window %d of %d: record %.6f, measured %.6f, delta %+.3f LSB "
        "(measured - record)"
        % ("worst peak window", worst_peak_at, len(now_peak),
           worst_peak_pair[0], worst_peak_pair[1],
           (worst_peak_pair[1] - worst_peak_pair[0]) / G.REFERENCE_LSB))
    return passed, lines


def main(argv):
    parser = argparse.ArgumentParser(
        description="the golden-audio record: floors, fingerprints and provenance")
    sub = parser.add_subparsers(dest="what", required=True)
    sub.add_parser("compare", help="every measured term of one pair").add_argument(
        "wavs", nargs="+")
    sub.add_parser("fingerprint", help="a committable measurement of one render").add_argument(
        "wav")
    floor = sub.add_parser("floor", help="the same-build run-to-run floor over N renders")
    floor.add_argument("wavs", nargs="+")
    floor.add_argument("--json", action="store_true")
    record = sub.add_parser("record", help="print the committed record, one term per line")
    record.add_argument("--record", default=None)
    args = parser.parse_args(argv)
    try:
        if args.what == "compare":
            for other in args.wavs[1:]:
                show_comparison(G.compare(args.wavs[0], other))
                print("")
            return 0
        if args.what == "fingerprint":
            print(json.dumps(G.fingerprint(args.wav), indent=2))
            return 0
        if args.what == "floor":
            measured = G.measure_floor(args.wavs)
            if measured is None:
                raise G.WavError("the floor needs at least two renders, got %d"
                                 % len(args.wavs))
            if args.json:
                print(json.dumps(measured, indent=2))
            else:
                show_floor(measured)
            return 0
        rows, comments = read_record(args.record)
        for line in comments:
            print(line)
        print("")
        for key in sorted(rows):
            show_row(rows[key])
        return 0
    except (G.WavError, OSError) as error:
        print("ERROR: %s" % error, file=sys.stderr)
        return 1


def show_comparison(measured):
    print("%s  vs  %s" % (measured["wav_a"], measured["wav_b"]))
    print("  frames compared      : %d (x %d ch)" % (measured["frames_compared"],
                                                     measured["channels_compared"]))
    print("  differing frames     : %d (1 LSB counts)" % measured["differing_frames"])
    print("  first differing frame: %s" % measured["first_diff_frame"])
    print("  max |delta|          : %.3f LSB (%.2f dBFS)"
          % (measured["max_delta_lsb"], measured["max_delta_dbfs"]))
    print("  mean |delta|         : %.3f LSB" % measured["mean_abs_delta_lsb"])
    print("  delta RMS            : %.2f dBFS" % measured["delta_rms_dbfs"])
    if measured["level_delta_db"] is None:
        print("  level delta          : undefined (silent file)")
    else:
        print("  level delta          : %+.6f dB (a %.4f vs b %.4f dBFS)"
              % (measured["level_delta_db"], measured["dbfs_a"], measured["dbfs_b"]))
    print("  envelope max delta   : %.6f dB over %d windows of %d frames"
          % (measured["envelope"]["max_delta_db"], measured["envelope"]["windows"],
             measured["envelope"]["window_frames"]))
    print("  data chunks identical: %s" % measured["identical_bytes"])


def show_floor(measured):
    print("floor over %d runs (%d pairs):" % (measured["runs"], measured["pairs"]))
    print("  max |delta|          : %.3f LSB (%.2f dBFS)"
          % (measured["max_delta_lsb"], measured["max_delta_dbfs"]))
    print("  differing frames     : %d" % measured["differing_frames"])
    print("  level delta          : %+.6f dB" % (measured["level_delta_db"] or 0.0))
    print("  envelope max delta   : %.6f dB" % measured["envelope"]["max_delta_db"])
    print("  byte-identical pairs : %s" % measured["identical_bytes"])
    for pair in measured["per_pair"]:
        print("    runs %s: %.3f LSB, %d frames, level %+.6f dB, envelope %.6f dB"
              % (pair["pair"], pair["max_delta_lsb"], pair["differing_frames"],
                 pair["level_delta_db"] or 0.0, pair["envelope_db"]))


def show_row(row):
    print("%s / %s  (%s, %s run(s))" % (row["fixture"], row["path"], row["command"],
                                        row["runs"]))
    print("  floor               : %s LSB, %s frames differ, level %s dB, envelope %s dB, "
          "byte-identical=%s" % (row["floor_max_lsb"], row["floor_differing_frames"],
                                 row["floor_level_db"], row["floor_env_db"],
                                 row["floor_identical_bytes"]))
    print("  golden              : %.4f dBFS rms, %.4f dBFS peak, %s frames, %s window(s)"
          % (float(row["rms_dbfs"]), float(row["peak_dbfs"]), row["frames"],
             len(row["env_dbfs"].split(","))))
    print("  build               : %s at %s" % (row["binary_sha256"][:16] or "unrecorded",
                                               row["measured_utc"]))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
