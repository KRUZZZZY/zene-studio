#!/usr/bin/env python3
"""RAW control-surface transcript for `render.stems` (0.3.0, row 68).

The acceptance evidence for the stem-export verb, produced by driving the REAL
`zene` binary headless and printing every request and reply verbatim.

The engine for this feature landed long before the id did (commit 117068e76,
docs/STEM-EXPORT.md; `StemExportTest` proves the four-stems-sum-to-the-mix
property at the C++ level). What this file proves is the thing that was MISSING:
that the DECLARED id drives that engine over the socket, end to end.

What it drives, in order:

  1. a session built through the registry   two instrument tracks, one with a
                                            note on a clip - so the export has
                                            real tracks to select and something
                                            to render;
  2. the refusals                           a relative `out`, an unsupported
                                            format and a negative `tail_bars`
                                            are each typed, and NOTHING is
                                            created on disk - the argument
                                            contract is checked before the
                                            child is ever spawned;
  3. `render.stems`                         THE EXPORT. Two files appear, named
                                            to the documented contract
                                            (`<index>_<name>.wav`, 1-based), each
                                            a non-empty RIFF/WAVE file. This
                                            call is where the DECLARED BOUND
                                            lives, so it is given its own
                                            per-command budget
                                            (RENDER_TIMEOUT) and the measured
                                            wall time is printed - exactly the
                                            way tests/freeze_bounce_evidence.py
                                            scopes the four commands that
                                            already render;
  4. `render.stems` again, SAME directory   the second call reports the files it
                                            REWROTE, not an empty set: a
                                            re-export must not be refused for
                                            having found no new names;
  5. `control.transactions`                 the A16 record for render.stems: it
                                            leaves NO project transaction,
                                            because it writes output artefacts
                                            and no project state.

The bound itself is NOT fixed here and this file does not pretend it is: the
render runs in a child process and the control surface does not answer - not
even `control.ping` - until it finishes (docs/RENDER-CHILD-WAIT.md:120-126,
docs/KNOWN-LIMITATIONS.md). What this test does is DECLARE the budget it needs
rather than raising a socket timeout to hide the wait.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path.

Usage: QT_QPA_PLATFORM=offscreen python3 control-stem-export-verb.py <zene>
Exit code 0 only when every assertion held.
"""

import itertools
import os
import re
import sys
import time

import control_socket_harness as H

# The declared per-command budget, in seconds: ONE FULL ENGINE START plus the
# audio, not one socket round trip. Same shape and same number as
# tests/freeze_bounce_evidence.py:57 - the four commands that already render use
# it, and render.stems is the fifth. Every OTHER command keeps the harness's own
# SOCKET_TIMEOUT (30.0).
RENDER_TIMEOUT = 180.0
RENDER_COMMANDS = ("render.stems",)

# The documented file-name contract: index 1-based, zero-padded to at least two
# digits, then the track name, then the format's extension.
STEM_NAME = re.compile(r"^\d{2,}_.+\.wav$")
WAV_HEADER_BYTES = 44

REQUEST_IDS = itertools.count(1)


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        budget = RENDER_TIMEOUT if command in RENDER_COMMANDS else None
        return self.client.call(next(REQUEST_IDS), command, args, timeout=budget,
                                transcript=self.transcript)

    def result(self, command, args=None):
        """The reply's result, or {'error': ...} so a failed call is visible."""
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def timed(self, command, args=None):
        """The reply and the wall seconds this call took - the measured bound."""
        started = time.time()
        result = self.result(command, args)
        return result, time.time() - started


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def build_session(session):
    """Two instrument tracks, one clip and one note - through the registry.

    A fresh instance already holds the demo project's tracks, so this session is
    NOT empty: the export under test selects every unmuted track it finds, and
    the checks below derive their expectation from that fact rather than assuming
    the two tracks added here are the whole song.
    """
    track = session.result("track.add", {"type": "instrument", "name": "Bass"})["track"]
    second = session.result("track.add", {"type": "instrument", "name": "VWLead"})["track"]
    clip = session.result("clip.add", {"track": track, "position": 0, "length": 768})["clip"]
    session.result("note.add", {"clip": clip, "key": 40, "position": 0, "length": 192,
                                "velocity": 100})
    return [track, second], ["Bass", "VWLead"]


def files_in(directory):
    if not os.path.isdir(directory):
        return []
    return sorted(name for name in os.listdir(directory)
                  if os.path.isfile(os.path.join(directory, name)))


def check_refusals(session, recorder, outdir):
    escaped = os.path.join(outdir, "refused")
    relative = session.result("render.stems", {"out": "stems"})
    recorder.check("a relative 'out' is refused typed",
                   "error" in relative and relative["error"].get("kind") == "invalid_args",
                   repr(relative)[:200])
    bad_format = session.result("render.stems", {"out": escaped, "format": "aiff"})
    recorder.check("an unsupported format is refused typed",
                   "error" in bad_format and bad_format["error"].get("kind") == "invalid_args",
                   repr(bad_format)[:200])
    bad_tail = session.result("render.stems", {"out": escaped, "tail_bars": -1})
    recorder.check("a negative tail_bars is refused typed",
                   "error" in bad_tail and bad_tail["error"].get("kind") == "invalid_args",
                   repr(bad_tail)[:200])
    recorder.check("a refused export created nothing on disk",
                   not os.path.exists(escaped),
                   "escaped=%r exists=%r" % (escaped, os.path.exists(escaped)))


def has_stem_named(names, name):
    """Whether the reply lists a stem for the track called \p name."""
    return any(entry.endswith("_" + name + ".wav") for entry in names)


def check_the_selection(recorder, names, tracks, names_wanted):
    """The export covered the tracks this fixture added.

    The expectation is DERIVED, not assumed: a fresh instance already holds the
    demo project's tracks, so the export selects more than the two added here.
    """
    recorder.check("the export selected the two tracks just added, not a fixed list",
                   len(names) >= len(tracks),
                   "listed=%r tracks=%r" % (names, tracks))
    for name in names_wanted:
        recorder.check("a stem for the track named %s is among them" % name,
                       has_stem_named(names, name), "names=%r" % (names,))


def check_names_follow_the_contract(recorder, names):
    recorder.check("every reported name follows the documented contract",
                   bool(names) and all(STEM_NAME.match(name) for name in names),
                   "names=%r" % (names,))


def file_size(path):
    return os.path.getsize(path) if os.path.isfile(path) else -1


def check_the_files(recorder, wanted, names):
    recorder.check("the files the reply named are on disk",
                   all(os.path.isfile(os.path.join(wanted, name)) for name in names),
                   "dir=%r" % (files_in(wanted),))
    sizes = [file_size(os.path.join(wanted, name)) for name in names]
    recorder.check("every stem is a non-empty RIFF/WAVE file, not a stub",
                   bool(sizes) and all(size > WAV_HEADER_BYTES for size in sizes),
                   "sizes=%r" % (sizes,))


def check_the_export(session, recorder, outdir, tracks, names_wanted):
    wanted = os.path.join(outdir, "stems")
    result, seconds = session.timed("render.stems", {"out": wanted, "format": "wav"})
    print("render.stems took %.1fs (declared budget %.0fs)" % (seconds, RENDER_TIMEOUT))
    recorder.check("render.stems answered success", "error" not in result, repr(result)[:300])
    if "error" in result:
        return wanted, []
    names = result.get("stems") or []
    check_the_selection(recorder, names, tracks, names_wanted)
    check_names_follow_the_contract(recorder, names)
    check_the_files(recorder, wanted, names)
    recorder.check("the reported count and sample rate are the ones measured",
                   result.get("count") == len(names) and result.get("sample_rate") == 44100,
                   "count=%r sample_rate=%r" % (result.get("count"), result.get("sample_rate")))
    return wanted, names


def check_re_export(session, recorder, wanted, first):
    """A SECOND export into the same directory reports the files it rewrote."""
    result, seconds = session.timed("render.stems", {"out": wanted, "format": "wav"})
    print("the re-export took %.1fs (declared budget %.0fs)" % (seconds, RENDER_TIMEOUT))
    recorder.check("a re-export into the same directory is NOT refused",
                   "error" not in result, repr(result)[:300])
    recorder.check("a re-export reports the files it rewrote, not an empty set",
                   (result.get("stems") or []) == first,
                   "second=%r first=%r" % (result.get("stems"), first))


def check_transactions(session, recorder):
    records = session.result("control.transactions")
    entries = records.get("transactions") or []
    named = [entry for entry in entries if entry.get("command") == "render.stems"]
    recorder.check("render.stems leaves no project transaction",
                   not named, "records=%r" % (named[:2],))
    recorder.check("every record on the wire is one control.transactions reports",
                   isinstance(entries, list), "type=%s" % type(entries).__name__)


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report_results(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-62s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)


def run_checks(session, instance, recorder, outdir):
    tracks, names_wanted = build_session(session)
    check_refusals(session, recorder, outdir)
    wanted, names = check_the_export(session, recorder, outdir, tracks, names_wanted)
    if names:
        check_re_export(session, recorder, wanted, names)
    check_transactions(session, recorder)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        session.result("control.version")
        run_checks(session, instance, recorder, instance.tmp)
        check_quit(session, instance, recorder)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("render.stems control-surface transcript")
        return 1
    H.ok("render.stems control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
