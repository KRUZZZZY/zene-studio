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
docs/STEM-EXPORT.md each did it ad hoc).  This is that instrument, once, registered, with the
negative control that proves it can still fail.

WHAT IT DOES, in one run of the real binary:

  1. builds an AUDIBLE fixture through the commands an agent has (one instrument track on
     the shipped default template, two clips, one note each) and reads the mixer channel the
     control will move off the wire;
  2. for EACH headline path - `render.render` (a render), `render.stems` (a stem export),
     `bounce.in_place` (a freeze/bounce) - renders the fixture RUNS times in its own temp
     directory and measures the same-build run-to-run floor over EVERY pair of those runs.
     A SECOND fixture, the bundled project the record names as still non-reproducible,
     carries the render path through a fixture whose floor is NOT zero;
  3. judges the measured floor against the floor the committed record
     (tests/golden-audio-record.tsv) says this fixture has, and the candidate render's
     FINGERPRINT against the record's golden envelope - every term against its own tolerance
     (max |delta| LSB and dBFS, the level delta, the per-window envelope), never one number;
  4. NEGATIVE CONTROL, then BOUND: moves a fader by a stated dB through `mixer.set_volume` -
     a deliberate gain change made by the PRODUCT, not by this script - re-renders and
     requires the comparison to FAIL it (a comparison that cannot fail is not evidence, so
     this is a check and not a demonstration), then sweeps the same fader from far below the
     floor to far above it and reports which changes this programme distinguishes.

`--write-record` re-measures the floors with --runs (at least DEEP_RUNS) and rewrites
tests/golden-audio-record.tsv - a deliberate, reviewable act: the record IS the baseline,
and a lane that rewrites it to make itself green has disabled this programme.

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

import control_socket_harness as H
import golden_audio_lib as G
import golden_audio_record as R
from freeze_bounce_evidence import (RENDER_TIMEOUT, Recorder, SILENT_DBFS,
                                    load_audible_instrument, report_on_abort,
                                    report_results)

REQUEST_IDS = iter(range(1, 100000))

# The fixture's name in the record.  The fixture is the command sequence in build_fixture()
# - no committed project file - so its identity is reviewable in this file.
FIXTURE = "socket-1track-2clips"
TRACK_NAME = "Golden Target"     # the fixture track's name, and the stem file's name
CLIP_TICKS = 192                 # one 4/4 bar
PARTS = ("render", "stems", "bounce")
COMMANDS = {"render": "render.render", "stems": "render.stems",
            "bounce": "bounce.in_place"}

# The SECOND fixture, and the reason there are two: bundled product content (no new file)
# and the project docs/RENDER-DETERMINISM.md records as still not bit-reproducible after the
# renderer fix (3/3 runs distinct, 98.3 % of frames, up to 13 758 LSB).  It carries the
# render path through a fixture that really does jitter - which is what a tolerance model is
# for.
DEMO = "bundled-Root84-TrancyLoop"
DEMO_PROJECT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                            "data", "projects", "shorties", "Root84-TrancyLoop.mmpz")

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

    def __init__(self, client, transcript, recorder):
        self.client = client
        self.transcript = transcript
        self.recorder = recorder

    def call(self, command, args=None):
        budget = RENDER_TIMEOUT if command in RENDER_COMMANDS else None
        started = time.time()
        reply = self.client.call(next(REQUEST_IDS), command, args, timeout=budget,
                                 transcript=self.transcript)
        if command in RENDER_COMMANDS and time.time() - started >= 5.0:
            print("slow render: %s took %.1fs (one engine start + the audio)"
                  % (command, time.time() - started))
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
    added = session.result("track.add", {"type": "instrument", "name": TRACK_NAME})
    track = added.get("track")
    if not track:
        H.fail("track.add returned no track id (%r)" % added)
    if not load_audible_instrument(session, track):
        print("no audible instrument in this build: the golden-audio programme cannot run")
        print("kinds: %r" % session.result("plugin.list").get("counts_by_kind"))
        return None
    # The stem export names each file after the TRACK, and `track.add`'s name did not survive
    # loading the instrument (measured: the track added as "Golden Target" exported as
    # 03_TripleOscillator.wav), so the fixture renames it after the load and reads the name
    # back - without that, the stem selection could measure another track's file, or nothing.
    session.result("track.rename", {"track": track, "name": TRACK_NAME})
    named = session.result("track.get_state", {"track": track})
    session.check("the fixture track carries the name its stem file is found by",
                  named.get("name") == TRACK_NAME,
                  "track.get_state name=%r, wanted %r" % (named.get("name"), TRACK_NAME))
    clips = []
    for position in (0, CLIP_TICKS):
        clip = session.result("clip.add", {"track": track, "position": position,
                                           "length": CLIP_TICKS})
        if not clip.get("clip"):
            H.fail("clip.add at %d returned no clip id (%r)" % (position, clip))
        session.result("note.add", {"clip": clip.get("clip"), "key": 60, "position": 0,
                                    "length": CLIP_TICKS // 2, "velocity": 120})
        clips.append(clip.get("clip"))
    mixer = session.result("mixer.get_state")
    channels = {c.get("id"): c for c in (mixer.get("channels") or [])}
    # The track's OWN channel, picked off the wire rather than assumed: ch-0 is the master
    # (control-pdc-commands.py's check reads the same `is_master` flag) and the track added
    # above creates the newest channel, so it is the highest-index non-master one.  The
    # choice is PRINTED and, better, VERIFIED by measurement - the negative control moves
    # this fader and requires the render to move; a fader that is not in the fixture's path
    # makes that check fail with the numbers rather than pass quietly.
    own = sorted((cid for cid, c in channels.items() if not c.get("is_master")),
                 key=lambda cid: channels[cid].get("index", 0))
    channel = own[-1] if own else next((cid for cid, c in channels.items()
                                        if c.get("is_master")), None)
    if channel is None:
        H.fail("no addressable mixer channel: %r" % sorted(channels))
    if not own:
        # MEASURED, not assumed: a headless instance's mixer holds the MASTER channel alone
        # (Mixer's constructor creates one; MixerView creates the rest), so a track added
        # over the socket sums into the master and has no fader of its own.  The control
        # moves the master here, and its own check - the gain change is CAUGHT - is what
        # proves that fader is in the fixture's path.
        print("note: the mixer holds one channel (%s, the master): the fixture track sums "
              "into it, so the control moves the master" % channel)
    volume = float(channels[channel]["volume"])
    print("fixture: track=%s clips=%s channel=%s (%s) fader %.4f; mixer has %d channel(s)"
          % (track, clips, channel, channels[channel].get("name"), volume, len(channels)))
    return {"track": track, "clips": clips, "channel": channel, "volume": volume,
            "name": TRACK_NAME}


def stem_files(directory, track_name=None):
    """The stem WAVs render.stems wrote, sorted - the fixture's own when named.

    `render.stems` exports EVERY unmuted track of the session, and a headless instance
    starts on the shipped default template (data/projects/templates/default.mpt: three
    tracks), so the export writes more than the fixture's stem.  The fixture measures ITS
    OWN - the `<index>_<name>.wav` whose name carries the fixture track's name, the contract
    tests/control-stem-export-verb.py reads - and a rename cannot quietly measure nothing,
    because the count is checked.
    """
    files = sorted(os.path.join(directory, name) for name in os.listdir(directory)
                  if name.lower().endswith(".wav"))
    if track_name is None:
        return files
    return [path for path in files if track_name in os.path.basename(path)]


def render_once(session, part, fixture, run_dir):
    """One render of the fixture through one headline path.  Returns the files it wrote."""
    os.makedirs(run_dir, exist_ok=True)
    if part == "render":
        out = os.path.join(run_dir, "render.wav")
        reply = session.result("render.render", {"out": out, "format": "wav"})
        written = [out] if reply.get("path") and os.path.exists(out) else []
    elif part == "stems":
        reply = session.result("render.stems", {"out": run_dir, "format": "wav"})
        written = (stem_files(run_dir, fixture["name"]) if reply.get("stems") else [])
    else:
        out = os.path.join(run_dir, "bounce.wav")
        reply = session.result("bounce.in_place", {"track": fixture["track"], "out": out})
        written = [out] if reply.get("path") and os.path.exists(out) else []
    if len(written) != 1:
        session.check("%s: one render writes exactly one measured file" % part, False,
                      "reply=%r files=%r" % (reply, written))
    return written


def measure_path(session, fixture_name, part, fixture, outdir, runs):
    """RUNS renders of one path, the same-build run-to-run floor, and the candidate."""
    renders, levels = [], []
    for index in range(runs):
        produced = render_once(session, part, fixture,
                               os.path.join(outdir, fixture_name, part, "run-%d" % index))
        if len(produced) != 1:
            return None
        renders.append(produced[0])
        frames, measured = G.file_dbfs(produced[0])
        levels.append(measured)
        if measured <= SILENT_DBFS:
            session.check("%s %s: run %d is AUDIO, not silence"
                          % (fixture_name, part, index), False,
                          "%.2f dBFS over %d frames" % (measured, frames))
            return None
    print("  levels               : %s dBFS" % ", ".join("%.3f" % v for v in levels))
    floor = R.measure_floor(renders, "%s / %s" % (fixture_name, part), COMMANDS[part])
    return {"fixture": fixture_name, "part": part, "runs": renders, "floor": floor,
            "levels": levels}


def set_gain(session, channel, base, delta_db):
    """Move the fixture track's fader by `delta_db` - the product's own gain change."""
    reply = session.result("mixer.set_volume",
                           {"channel": channel,
                            "volume": base * (10.0 ** (delta_db / 20.0))})
    return float(reply.get("volume", 0.0))


def negative_control(session, fixture, measured, outdir):
    """A deliberate gain change the comparison MUST fail.  Returns the measurements."""
    part, row = measured["part"], measured["row"]
    label = "%s / %s" % (measured["fixture"], part)
    base, channel = fixture["volume"], fixture["channel"]
    applied = set_gain(session, channel, base, CONTROL_DB)
    print("")
    print("=== %s: NEGATIVE CONTROL, fader %s %.6f -> %.6f (%+.4f dB) ==="
          % (label, channel, base, applied, 20.0 * math.log10(applied / base)))
    produced = render_once(session, part, fixture,
                           os.path.join(outdir, measured["fixture"],
                                        "%s-control" % part))
    set_gain(session, channel, base, 0.0)              # the fader goes back either way
    if len(produced) != 1:
        return None
    control = G.compare(measured["runs"][0], produced[0])
    tol = G.tolerance(measured["floor"])
    caught, lines = G.verdict(control, tol)
    print("  measured (%s)        : %.3f LSB max |delta|, %d differing frames, level "
          "%+.6f dB, envelope %.6f dB"
          % (os.path.basename(produced[0]), control["max_delta_lsb"],
             control["differing_frames"], control["level_delta_db"] or 0.0,
             control["envelope"]["max_delta_db"]))
    for line in lines:
        print("  %s" % line)
    session.check("%s: THE NEGATIVE CONTROL - a %+.2f dB gain change is CAUGHT"
                  % (label, CONTROL_DB),
                  caught is False,
                  "measured %.3f LSB against a %.3f LSB tolerance - a comparison that "
                  "cannot fail is not evidence" % (control["max_delta_lsb"], tol["lsb"]))
    golden_passed, golden_lines = R.compare_fingerprints(G.fingerprint(produced[0]), row)
    print("  the record's golden term on the same control render:")
    for line in golden_lines:
        print("  %s" % line)  # noqa: E501  (each term, with its own tolerance)
    session.check("%s: the record's golden term catches the control too" % label,
                  golden_passed is False,
                  "the record's own tolerance, %s"
                  % ("failed the control" if not golden_passed
                     else "PASSED it - that term is blind to a %+.2f dB change" % CONTROL_DB))
    return {"delta_db": CONTROL_DB, "measured": control, "tolerance": tol,
            "caught": not caught}


def bound_sweep(session, fixture, measured, outdir):
    """Which gain changes this programme distinguishes - the bound, measured not asserted."""
    part = measured["part"]
    label = "%s / %s" % (measured["fixture"], part)
    base, channel = fixture["volume"], fixture["channel"]
    reference = measured["runs"][0]
    tol = G.tolerance(measured["floor"])
    print("")
    print("=== %s: THE BOUND - which fader changes are distinguishable ===" % label)
    print("  tolerance: %.3f LSB / %.6f dB (twice the measured floor, floored at 1 LSB)"
          % (tol["lsb"], tol["db"]))
    print("  %-11s %-13s %-14s %-11s %s"
          % ("delta dB", "max |delta|", "level dB", "verdict", "note"))
    rows = []
    for delta in SWEEP_DB:
        set_gain(session, channel, base, delta)
        produced = render_once(session, part, fixture,
                               os.path.join(outdir, measured["fixture"],
                                            "%s-sweep-%s" % (part, delta)))
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


def record_rows(measured_by_key, binary_sha256):
    """The record rows for this run's measurements (the record module builds them)."""
    return R.rows_for(list(measured_by_key.values()), COMMANDS, binary_sha256)


def try_quit(session, instance, recorder):
    """control.quit, and the instance must actually stop."""
    reply = session.call("control.quit")  # the quit's own reply, not an assumption
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def open_demo_fixture(session):
    """The second fixture: a BUNDLED project that is RECORDED as not bit-reproducible.

    docs/RENDER-DETERMINISM.md records it as 3/3 runs distinct, 98.3 % of its frames
    differing, up to 13 758 LSB, with the mechanism in the instruments (section 10).  That is
    the non-determinism this programme's tolerance model exists to survive, so the model is
    measured against a fixture that HAS it: one demonstrated only on a bit-reproducible
    fixture has not been demonstrated.

    The fader the control moves is the MASTER channel - it exists in every session - and
    opening a project REPLACES the session, which is why this fixture goes second.
    """
    if not os.path.exists(DEMO_PROJECT):
        session.check("the bundled fixture project exists", False, DEMO_PROJECT)
        return None
    opened = session.result("project.open", {"path": DEMO_PROJECT})
    session.check("the bundled fixture opens with no load errors",
                  opened.get("file") == DEMO_PROJECT
                  and opened.get("loaded_with_errors") in (False, None),
                  "file=%r errors=%r count=%r" % (opened.get("file"),
                                                  opened.get("loaded_with_errors"),
                                                  opened.get("error_count")))
    if opened.get("loaded_with_errors"):
        return None
    mixer = session.result("mixer.get_state")
    channels = {c.get("id"): c for c in (mixer.get("channels") or [])}
    master = next((cid for cid, entry in channels.items() if entry.get("is_master")), None)
    if master is None:
        session.check("the bundled fixture has an addressable master channel", False,
                      "mixer.get_state ids=%s" % sorted(channels))
        return None
    volume = float(channels[master]["volume"])
    print("fixture %s: %d track(s), tempo %r, master %s fader %.4f"
          % (DEMO, opened.get("track_count", 0), opened.get("tempo"), master, volume))
    return {"track": None, "clips": [], "channel": master, "volume": volume,
            "name": None}


def fixture_plan():
    """Every fixture this programme measures, and the headline paths it measures on each.

    Two fixtures on purpose: the socket-built one is cheap and covers all THREE headline
    paths (a render, a stem export, a freeze/bounce); the bundled one is where the recorded
    non-determinism lives and carries the render path through it.
    """
    return (
        {"name": FIXTURE, "setup": build_fixture, "parts": PARTS},
        {"name": DEMO, "setup": open_demo_fixture, "parts": ("render",)},
    )


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
    measured_by_key, any_fixture = {}, False
    with H.start_instance(binary) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript, recorder)
        session.result("control.version")
        outdir = os.path.join(instance.tmp, "golden")
        print("instance : %s\nbinary   : sha256 %s" % (binary, binary_sha256))
        print("mode     : %s, %d run(s) per path"
              % ("WRITE RECORD" if write_record else "verify", runs))
        try:
            for plan in fixture_plan():
                fixture = plan["setup"](session)
                if fixture is None:
                    continue
                any_fixture = True
                for part in plan["parts"]:
                    measured = measure_path(session, plan["name"], part, fixture, outdir,
                                            runs)
                    if measured is None:
                        continue
                    key = (plan["name"], part)
                    if write_record:
                        measured_by_key[key] = measured
                        continue
                    row = rows.get(key)
                    if row is None:
                        recorder.check("%s %s: the committed record has a golden row"
                                       % (plan["name"], part), False,
                                       "no row for %r in %s; run --write-record and commit "
                                       "the record" % (key, R.RECORD_FILE))
                        continue
                    measured["row"] = row
                    measured_by_key[key] = measured
                    label = "%s / %s" % (plan["name"], part)
                    R.judge_measurement(session.check, label, measured["floor"],
                                        G.fingerprint(measured["runs"][0]), row)
                if write_record:
                    continue
                for key, measured in measured_by_key.items():
                    if key[0] == plan["name"]:
                        negative_control(session, fixture, measured, outdir)
                if not ("--skip-sweep" in argv) and (plan["name"], "render") \
                        in measured_by_key:
                    sweep = bound_sweep(session, fixture, measured_by_key[(plan["name"],
                                                                           "render")],
                                        outdir)
                    recorder.check("%s render: the -2 dB sweep point is caught (so the "
                                   "sweep has a working end)" % plan["name"],
                                   any(row["delta_db"] == -2.0 for row in sweep["caught"]),
                                   "caught: %s" % [row["delta_db"]
                                                   for row in sweep["caught"]])
            if write_record and measured_by_key:
                new_rows = record_rows(measured_by_key, binary_sha256)
                path = R.write_record(new_rows, R.provenance(binary_sha256, runs, (
                    "fixture %s = the socket-built session in this file's build_fixture(), on "
                    "the shipped default template" % FIXTURE,
                    "fixture %s = data/projects/shorties/Root84-TrancyLoop.mmpz, which "
                    "docs/RENDER-DETERMINISM.md records as still not bit-reproducible"
                    % DEMO)), record_path)
                print("\nrecord written: %s (%d rows)" % (path, len(new_rows)))
            elif not write_record:
                try_quit(session, instance, recorder)
        except BaseException:
            report_on_abort(recorder, transcript, instance)
            raise
    report_results(recorder)
    if not any_fixture:
        H.ok("no audible instrument in this build: Skipped, never Passed")
        return 77
    if not measured_by_key:
        print("")
        print("FAIL: no fixture produced a complete measurement")
        return 1
    if recorder.problems:
        print("")
        recorder.problems.report("golden-audio integration programme")
        return 1
    H.ok("golden-audio integration programme (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
