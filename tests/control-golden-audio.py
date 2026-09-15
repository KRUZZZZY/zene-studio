#!/usr/bin/env python3
"""The golden-audio integration programme, end to end through --control-socket.

THE PROBLEM THIS EXISTS FOR.  This tree's renders are not bit-reproducible run to run
(docs/RENDER-DETERMINISM.md: 6 of 9 bundled projects rendered to different bytes on every run
of the same binary before the ProjectRenderer fix, and 2 still do; the worst differed on
98.3 % of its frames).  Every lane that has needed to say "this change altered the render"
has therefore had to build the same instrument: render the SAME fixture N times in the SAME
build, measure the run-to-run FLOOR, and judge a candidate by max |delta| in LSB/dB against
that floor - never sha256 byte-identity, never a single before/after number
(docs/AUTO-MASTERING.md:251 states the rule; docs/RACKS.md, docs/WARP.md and
docs/STEM-EXPORT.md each did it ad hoc).  This is that instrument, once, registered.

WHAT IT DOES, in one run of the real binary:

  1. builds an AUDIBLE fixture through the commands an agent has (one instrument track, two
     clips, one note each) and reads the track's own mixer channel off the wire;
  2. for EACH headline path - `render.render` (a render), `render.stems` (a stem export),
     `bounce.in_place` (a freeze/bounce) - renders the fixture RUNS times in its own temp
     directory and measures the same-build run-to-run floor over EVERY pair of those runs;
  3. judges the measured floor against the floor the committed record
     (tests/golden-audio-record.tsv) says this fixture has, and the candidate render's
     FINGERPRINT against the record's golden envelope - every term against its own tolerance
     (max |delta| LSB and dBFS, the level delta, the per-window envelope), never one number;
  4. NEGATIVE CONTROL: moves the fixture track's own mixer fader by a stated dB through
     `mixer.set_volume` - a deliberate gain change made by the PRODUCT, not by this script -
     re-renders, and requires the comparison to FAIL it.  A comparison that cannot fail is
     not evidence, so this is a check, not a demonstration;
  5. BOUND: sweeps the same fader from far below the floor to far above it and reports which
     changes this programme distinguishes - the honest limit of what it can claim.

`--write-record` re-measures the floors with --runs (at least DEEP_RUNS) and rewrites
tests/golden-audio-record.tsv.  That is a deliberate, reviewable act: the record is the
baseline, and a lane that rewrites it to make itself green has disabled this programme.

Usage: QT_QPA_PLATFORM=offscreen python3 control-golden-audio.py <lmms> [--runs N]
       [--write-record] [--record PATH] [--skip-sweep]
Exit code 0 only when every check held; 77 (ctest Skipped, never Passed) when this build has
no loadable instrument, because an inaudible fixture cannot prove any of it.
"""

import hashlib
import math
import os
import sys
import time
from datetime import datetime, timezone

import control_socket_harness as H
import golden_audio_lib as G
import golden_audio_record as R
from freeze_bounce_evidence import (RENDER_TIMEOUT, Recorder, SILENT_DBFS,
                                    load_audible_instrument, report_on_abort,
                                    report_results, wav_measure)

REQUEST_IDS = iter(range(1, 100000))

# The fixture's name in the record.  The fixture is the command sequence in build_fixture()
# - no committed project file - so its identity is reviewable in this file.
FIXTURE = "socket-1track-2clips"
CLIP_TICKS = 192                 # one 4/4 bar
PARTS = ("render", "stems", "bounce")
COMMANDS = {"render": "render.render", "stems": "render.stems",
            "bounce": "bounce.in_place"}

RUNS = 3                         # the ctest's floor runs (C(3,2) = 3 pairs per path)
DEEP_RUNS = 5                    # --write-record: C(5,2) = 10 pairs per path

# The one deliberate gain change the negative control applies, in dB.  Far above anything
# the jitter has ever produced (recorded floors are 0 to a few LSB, i.e. below -70 dBFS) and
# small enough that only an honest comparison would catch it.
CONTROL_DB = -0.5

# The bound sweep, in dB on the same fader.  -0.0001 dB is a ten-thousandth of a dB and
# should vanish into the 16-bit quantisation; -2 dB is unmistakable.  Reported, never
# asserted: which of these the programme distinguishes IS the bound this programme records.
SWEEP_DB = (-0.0001, -0.001, -0.01, -0.1, -0.5, -2.0)

# The commands that RUN A RENDER: this test bounds each one with the declared per-command
# budget the four rendering commands already use (tests/freeze_bounce_evidence.py:57),
# rather than raising the harness's own 30 s socket timeout and calling a slow box a hang.
RENDER_COMMANDS = tuple(COMMANDS.values())


class Session:
    """The socket client with the render budget, plus the check recorder."""

    def __init__(self, client, transcript, recorder):
        self.client = client
        self.transcript = transcript
        self.recorder = recorder

    def call(self, command, args=None):
        budget = RENDER_TIMEOUT if command in RENDER_COMMANDS else None
        started = time.time()
        reply = self.client.call(next(REQUEST_IDS), command, args, timeout=budget,
                                 transcript=self.transcript)
        elapsed = time.time() - started
        if command in RENDER_COMMANDS and elapsed >= 5.0:
            print("slow render: %s took %.1fs (one engine start + the audio; the declared "
                  "budget is %.0fs)" % (command, elapsed, RENDER_TIMEOUT))
        return reply

    def result(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def check(self, name, passed, evidence):
        self.recorder.check(name, passed, evidence)


def build_fixture(session):
    """The audible fixture, and the mixer channel whose fader the control moves."""
    added = session.result("track.add", {"type": "instrument", "name": "Golden Target"})
    track = added.get("track")
    if not track:
        H.fail("track.add returned no track id (%r)" % added)
    if not load_audible_instrument(session, track):
        print("no audible instrument in this build: the golden-audio programme cannot run")
        print("kinds: %r" % session.result("plugin.list").get("counts_by_kind"))
        return None
    clips = []
    for position in (0, CLIP_TICKS):
        clip = session.result("clip.add", {"track": track, "position": position,
                                           "length": CLIP_TICKS})
        if not clip.get("clip"):
            H.fail("clip.add at %d returned no clip id (%r)" % (position, clip))
        session.result("note.add", {"clip": clip.get("clip"), "key": 60, "position": 0,
                                    "length": CLIP_TICKS // 2, "velocity": 120})
        clips.append(clip.get("clip"))
    state = session.result("track.get_state", {"track": track})
    index = state.get("mixer_channel")
    mixer = session.result("mixer.get_state")
    channels = {c.get("id"): c for c in (mixer.get("channels") or [])}
    channel = "ch-%s" % index if index is not None else None
    session.check("the fixture track has its own mixer channel on the wire",
                  channel in channels,
                  "track.get_state mixer_channel=%r, mixer.get_state ids=%s"
                  % (index, sorted(channels)))
    if channel not in channels:
        H.fail("the fixture's channel is not addressable: %r" % channel)
    volume = float(channels[channel]["volume"])
    print("fixture: track=%s clips=%s channel=%s (fader %.4f)" % (track, clips, channel,
                                                                 volume))
    return {"track": track, "clips": clips, "channel": channel, "volume": volume}


def stem_files(directory):
    """The stem WAVs render.stems wrote, sorted."""
    return sorted(os.path.join(directory, name) for name in os.listdir(directory)
                  if name.lower().endswith(".wav"))


def render_once(session, part, fixture, run_dir):
    """One render of the fixture through one headline path.  Returns the files it wrote."""
    os.makedirs(run_dir, exist_ok=True)
    if part == "render":
        out = os.path.join(run_dir, "render.wav")
        reply = session.result("render.render", {"out": out, "format": "wav"})
        written = [out] if reply.get("path") and os.path.exists(out) else []
    elif part == "stems":
        reply = session.result("render.stems", {"out": run_dir, "format": "wav"})
        written = stem_files(run_dir) if reply.get("stems") else []
    else:
        out = os.path.join(run_dir, "bounce.wav")
        reply = session.result("bounce.in_place", {"track": fixture["track"], "out": out})
        written = [out] if reply.get("path") and os.path.exists(out) else []
    if len(written) != 1:
        session.check("%s: one render writes exactly one measured file" % part, False,
                      "reply=%r files=%r" % (reply, written))
    return written


def measure_path(session, part, fixture, outdir, runs):
    """RUNS renders of one path, the same-build run-to-run floor, and the candidate."""
    renders, levels = [], []
    for index in range(runs):
        produced = render_once(session, part, fixture,
                               os.path.join(outdir, part, "run-%d" % index))
        if len(produced) != 1:
            return None
        renders.append(produced[0])
        frames, measured = wav_measure(produced[0])
        levels.append(measured)
        if measured <= SILENT_DBFS:
            session.check("%s: run %d is AUDIO, not silence" % (part, index), False,
                          "%.2f dBFS over %d frames" % (measured, frames))
            return None
    floor = G.measure_floor(renders)
    print("")
    print("=== %s (%s): %d runs, floor over %d pairs ==="
          % (part, COMMANDS[part], runs, floor["pairs"]))
    print("  renders              : %s" % ", ".join(os.path.basename(p) for p in renders))
    print("  levels               : %s dBFS" % ", ".join("%.3f" % v for v in levels))
    print("  floor max |delta|    : %.3f LSB (%.2f dBFS)"
          % (floor["max_delta_lsb"], floor["max_delta_dbfs"]))
    print("  floor differing frms : %d (first %s)"
          % (floor["differing_frames"], floor["first_diff_frame"]))
    print("  floor level delta    : %+.6f dB" % (floor["level_delta_db"] or 0.0))
    print("  floor envelope delta : %.6f dB" % floor["envelope"]["max_delta_db"])
    print("  byte-identical pairs : %s of %d" % (floor["identical_bytes"], floor["pairs"]))
    for pair in floor["per_pair"]:
        print("    runs %s: %.3f LSB, %d frames differ, level %+.6f dB"
              % (pair["pair"], pair["max_delta_lsb"], pair["differing_frames"],
                 pair["level_delta_db"] or 0.0))
    return {"part": part, "runs": renders, "floor": floor, "levels": levels}


def judge_floor(session, part, floor, row):
    """The measured floor against the floor the record says this fixture has."""
    recorded = float(row["floor_max_lsb"])
    limit = max(G.MARGIN * recorded, G.LSB_FLOOR)
    session.check("%s: the same-build floor is within the recorded one's tolerance" % part,
                  floor["max_delta_lsb"] <= limit,
                  "measured %.3f LSB, recorded %.3f LSB (limit %.3f = 2x, floored at 1 LSB)"
                  % (floor["max_delta_lsb"], recorded, limit))
    print("  floor vs record      : measured %.3f LSB, recorded %s LSB on build %s"
          % (floor["max_delta_lsb"], row["floor_max_lsb"],
             row["binary_sha256"][:16] or "unrecorded"))


def judge_golden(session, part, print_, row):
    """The candidate's fingerprint against the record's golden envelope, term by term."""
    passed, lines = R.compare_fingerprints(print_, row)
    print("  golden vs record:")
    for line in lines:
        print("  %s" % line)
    session.check("%s: the render still measures what the record's golden says" % part,
                  passed, "recorded rms %.4f dBFS, now %.4f dBFS"
                  % (float(row["rms_dbfs"]), print_["rms_dbfs"]))


def set_gain(session, channel, base, delta_db):
    """Move the fixture track's fader by `delta_db` - the product's own gain change."""
    reply = session.result("mixer.set_volume",
                           {"channel": channel,
                            "volume": base * (10.0 ** (delta_db / 20.0))})
    return float(reply.get("volume", 0.0))


def negative_control(session, fixture, measured, outdir):
    """A deliberate gain change the comparison MUST fail.  Returns the measurements."""
    part, row = measured["part"], measured["row"]
    base, channel = fixture["volume"], fixture["channel"]
    applied = set_gain(session, channel, base, CONTROL_DB)
    print("")
    print("=== %s: NEGATIVE CONTROL, fader %.6f -> %.6f (%+.4f dB) ==="
          % (part, base, applied, 20.0 * math.log10(applied / base)))
    produced = render_once(session, part, fixture, os.path.join(outdir, "%s-control" % part))
    set_gain(session, channel, base, 0.0)              # the fader goes back either way
    if len(produced) != 1:
        return None
    control = G.compare(measured["runs"][0], produced[0])
    tol = G.tolerance(measured["floor"])
    caught, lines = G.verdict(control, tol)
    print("  control render       : %s" % os.path.basename(produced[0]))
    print("  measured             : %.3f LSB max |delta|, %d differing frames, level "
          "%+.6f dB, envelope %.6f dB"
          % (control["max_delta_lsb"], control["differing_frames"],
             control["level_delta_db"] or 0.0, control["envelope"]["max_delta_db"]))
    for line in lines:
        print("  %s" % line)
    session.check("%s: THE NEGATIVE CONTROL - a %+.2f dB gain change is CAUGHT" % (part,
                                                                                   CONTROL_DB),
                  caught is False,
                  "measured %.3f LSB against a %.3f LSB tolerance - a comparison that "
                  "cannot fail is not evidence" % (control["max_delta_lsb"], tol["lsb"]))
    golden_passed, golden_lines = R.compare_fingerprints(G.fingerprint(produced[0]), row)
    print("  the record's golden term on the same control render:")
    for line in golden_lines:
        print("  %s" % line)
    session.check("%s: the record's golden term catches the control too" % part,
                  golden_passed is False,
                  "the record's own tolerance, %s"
                  % ("failed the control" if not golden_passed
                     else "PASSED it - that term is blind to a %+.2f dB change" % CONTROL_DB))
    return {"delta_db": CONTROL_DB, "measured": control, "tolerance": tol,
            "caught": not caught}


def bound_sweep(session, fixture, measured, outdir):
    """Which gain changes this programme distinguishes - the bound, measured not asserted."""
    part = measured["part"]
    base, channel = fixture["volume"], fixture["channel"]
    reference = measured["runs"][0]
    tol = G.tolerance(measured["floor"])
    print("")
    print("=== %s: THE BOUND - which fader changes are distinguishable ===" % part)
    print("  tolerance: %.3f LSB / %.6f dB (twice the measured floor, floored at 1 LSB)"
          % (tol["lsb"], tol["db"]))
    print("  %-11s %-13s %-14s %-11s %s"
          % ("delta dB", "max |delta|", "level dB", "verdict", "note"))
    rows = []
    for delta in SWEEP_DB:
        set_gain(session, channel, base, delta)
        produced = render_once(session, part, fixture,
                               os.path.join(outdir, "%s-sweep-%s" % (part, delta)))
        set_gain(session, channel, base, 0.0)
        if len(produced) != 1:
            continue
        result = G.compare(reference, produced[0])
        caught, _ = G.verdict(result, tol)
        rows.append({"delta_db": delta, "measured": result, "caught": not caught})
        print("  %+-11.4f %-13.3f %-14.6f %-11s %s"
              % (delta, result["max_delta_lsb"], result["level_delta_db"] or 0.0,
                 "caught" if not caught else "NOT caught",
                 "identical bytes" if result["identical_bytes"] else ""))
    caught = [row for row in rows if row["caught"]]
    missed = [row for row in rows if not row["caught"]]
    print("  caught               : %s"
          % (", ".join("%+.4f dB (%.3f LSB)" % (row["delta_db"],
                                                row["measured"]["max_delta_lsb"])
                       for row in caught) or "none"))
    print("  NOT distinguished    : %s"
          % (", ".join("%+.4f dB" % row["delta_db"] for row in missed) or "none"))
    return {"caught": caught, "missed": missed}


def record_rows(measured_by_part, binary_sha256):
    """The record rows for this run's measurements, keyed as the record keys them."""
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    rows = []
    for measured in measured_by_part:
        part = measured["part"]
        print_ = G.fingerprint(measured["runs"][0], binary_sha256)
        rows.append(R.row_from(FIXTURE, part, COMMANDS[part], measured["floor"], print_,
                               now))
    return rows


def provenance(binary_sha256, runs):
    return [
        "golden-audio record - the measured same-build run-to-run floor and the golden",
        "fingerprint, per fixture and headline path.",
        "Written by tests/control-golden-audio.py --write-record; judged by the ctest",
        "ControlGoldenAudio.  NEVER rewrite a row to make a lane green: this record IS the",
        "baseline the programme compares against, and a floor rewritten to fit a result is",
        "a disabled test.",
        "fixture %s = the socket-built session in control-golden-audio.py's build_fixture()"
        % FIXTURE,
        "binary sha256 %s, %s" % (binary_sha256,
                                  datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")),
        "runs %d per path; each floor is the worst value over every pair of those runs"
        % runs,
    ]


def try_quit(session, instance, recorder):
    """control.quit, and the instance must actually stop."""
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    binary = argv[1]
    runs = RUNS
    write_record = "--write-record" in argv
    record_path = argv[argv.index("--record") + 1] if "--record" in argv else None
    if "--runs" in argv:
        runs = int(argv[argv.index("--runs") + 1])
    if write_record:
        runs = max(runs, DEEP_RUNS)
    rows, _ = R.read_record(record_path)
    binary_sha256 = hashlib.sha256(open(binary, "rb").read()).hexdigest()

    recorder = Recorder()
    transcript = H.Transcript()
    parts, fixture = {}, None
    with H.start_instance(binary) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript, recorder)
        session.result("control.version")
        outdir = os.path.join(instance.tmp, "golden")
        print("instance : %s" % binary)
        print("binary   : sha256 %s" % binary_sha256)
        print("mode     : %s, %d run(s) per path"
              % ("WRITE RECORD" if write_record else "verify", runs))
        try:
            fixture = build_fixture(session)
            if fixture is not None:
                for part in PARTS:
                    measured = measure_path(session, part, fixture, outdir, runs)
                    if measured is None:
                        continue
                    row = rows.get((FIXTURE, part))
                    if write_record:
                        parts[part] = measured
                        continue
                    if row is None:
                        recorder.check("%s: the committed record has a golden row" % part,
                                       False, "no row for (%s, %s) in %s; run --write-record "
                                       "and commit the record"
                                       % (FIXTURE, part, R.RECORD_FILE))
                        continue
                    measured["row"] = row
                    parts[part] = measured
                    judge_floor(session, part, measured["floor"], row)
                    judge_golden(session, part, G.fingerprint(measured["runs"][0]), row)
                if write_record and parts:
                    new_rows = record_rows(list(parts.values()), binary_sha256)
                    path = R.write_record(new_rows, provenance(binary_sha256, runs),
                                          record_path)
                    print("\nrecord written: %s (%d rows)" % (path, len(new_rows)))
                elif not write_record:
                    for part in PARTS:
                        if part in parts:
                            negative_control(session, fixture, parts[part], outdir)
                    if not ("--skip-sweep" in argv) and "render" in parts:
                        sweep = bound_sweep(session, fixture, parts["render"], outdir)
                        recorder.check("render: the -2 dB sweep point is caught (so the "
                                       "sweep has a working end)",
                                       any(row["delta_db"] == -2.0 for row in sweep["caught"]),
                                       "caught: %s" % [row["delta_db"]
                                                       for row in sweep["caught"]])
                    try_quit(session, instance, recorder)
        except BaseException:
            report_on_abort(recorder, transcript, instance)
            raise
    report_results(recorder)
    if fixture is None:
        H.ok("no audible instrument in this build: Skipped, never Passed")
        return 77
    if recorder.problems:
        print("")
        recorder.problems.report("golden-audio integration programme")
        return 1
    H.ok("golden-audio integration programme (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
