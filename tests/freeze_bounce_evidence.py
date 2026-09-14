#!/usr/bin/env python3
"""Evidence helpers shared by the freeze / bounce-in-place transcript.

Split out of tests/control-freeze-commands-transcript.py the way
tests/link_sync_evidence.py was split out of its transcript: the file-length
ratchet measures a file as a unit (Gate 7, 500 lines), and these helpers are the
half that is not about freeze at all - the socket session wrapper, the check
recorder, the WAV measurements, and picking an instrument that actually makes a
sound. The transcript keeps the checks themselves.

The one rule this module exists to make possible: a level is MEASURED from the
file the engine wrote, never taken from the reply that described it.
"""

import array
import math
import os
import time
import wave

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))

# Below this, a render is "no audio at all" rather than a quiet one.
SILENT_DBFS = -60.0

# The bound for a command that RUNS A RENDER: render.render, bounce.in_place,
# freeze.track and freeze.region all run the product's own CLI render
# (`zene render <file> -o <out>`) IN A CHILD PROCESS against a serialised copy of
# the session - ControlCommandsProject.cpp's render.render documents the path and
# BounceInPlace.cpp:179 calls the same helper - so such a command costs ONE FULL
# ENGINE START plus the audio, not one socket round trip.
#
# Bounding it with the harness's SOCKET_TIMEOUT (30s) bounds the wrong thing, and
# reports a working render as a hang. That is measured, not argued: the
# linux-arm64 job fails ControlFreezeCommandsTranscript and
# ControlTrackFolderTranscript at exactly 30.0s with
#
#     control_socket_harness.Blocked: no response line inside 30.0s (socket timed out)
#     diagnosis: the instance is STILL RUNNING (pid 40056) - a HANG, not a crash
#     cpu: burned 0% of one core over 1.0s
#     5 threads, all state=S: poll_schedule_timeout, futex_do_wait x3, hrtimer_nanosleep
#
# while the instance is parked in `poll_schedule_timeout` on a `zene render` child
# that is burning the core. The SAME command passes in ControlSocketIntegration,
# whose client waits 60s, and the engine start alone is ~34s on that job
# (docs/control-arm64-cluster-logs/EVIDENCE.md, item 9d15bd7e9, STILL OPEN)
# against ~0.3s here.
#
# So a render gets the treatment readiness already has (control_socket_harness's
# STARTUP_BOUND: "a declared budget, never one socket read"): a bound of its own,
# NOT a raise of SOCKET_TIMEOUT - every other command keeps the 30s that makes a
# genuine hang cost seconds. 5x the measured arm64 engine start, and well inside
# the 900s ctest TIMEOUT these transcripts carry, so a render child that never
# finishes still FAILS rather than passing slowly.
RENDER_TIMEOUT = 180.0

# The commands whose cost includes a whole engine start. Named here, not at the
# call sites, so the list and the reason cannot drift apart.
RENDER_COMMANDS = ("render.render", "bounce.in_place", "freeze.track", "freeze.region")

# A render slower than this is printed with its wall time, so a run on a slow
# platform says how much of its time a render's engine start is - the arm64 job's
# ~34s would be invisible otherwise.
SLOW_RENDER_REPORT_SECONDS = 5.0


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        """One request, bounded by the operation's own budget.

        A render gets RENDER_TIMEOUT (the engine start a render costs, see its
        comment); everything else keeps the harness's SOCKET_TIMEOUT by passing
        no timeout, exactly as before.
        """
        budget = RENDER_TIMEOUT if command in RENDER_COMMANDS else None
        started = time.time()
        reply = self.client.call(next(REQUEST_IDS), command, args, timeout=budget,
                                 transcript=self.transcript)
        elapsed = time.time() - started
        if command in RENDER_COMMANDS and elapsed >= SLOW_RENDER_REPORT_SECONDS:
            print("slow render: %s took %.1fs (one engine start + the audio; "
                  "RENDER_TIMEOUT is %.0fs)" % (command, elapsed, RENDER_TIMEOUT))
        return reply

    def result(self, command, args=None):
        """The reply, or {'error': ...} so a failed call is visible in a check."""
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def typed_error(self, command, args=None):
        reply = self.call(command, args)
        return (reply.get("error") or {}) if reply.get("ok") is False else {}


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def report_results(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-58s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
            recorder.problems.add("%s (%s)" % (name, evidence))


def raw_dbfs(raw, width):
    """The RMS of interleaved PCM in dBFS, measured rather than asserted."""
    if width == 2:
        samples, scale = array.array("h"), 32768.0
    elif width == 4:
        samples, scale = array.array("f"), 1.0
    else:
        raise AssertionError("unsupported WAV sample width %d" % width)
    samples.frombytes(raw)
    if not len(samples):
        return float("-inf")
    total = 0.0
    for value in samples:
        total += float(value) * float(value)
    return 20.0 * math.log10(math.sqrt(total / len(samples)) / scale + 1e-20)


def wav_measure(path):
    """The file's own frame count and RMS in dBFS."""
    with wave.open(path, "rb") as handle:
        width = handle.getsampwidth()
        frames = handle.getnframes()
        raw = handle.readframes(frames)
    return frames, raw_dbfs(raw, width)


def window_measure(path, bars, first_bar, last_bar):
    """Frames and dBFS of bars [first_bar, last_bar) of a `bars`-bar render.

    The render is the song plus the bar Song::startExport appends to every
    non-loop render, so its length in bars is known from the fixture and each
    bar is frames // bars - which is what lets a check measure ONE bar of it
    (the region a freeze covers) instead of diluting it over the whole file.
    """
    with wave.open(path, "rb") as handle:
        width = handle.getsampwidth()
        per_bar = handle.getnframes() // bars
        handle.setpos(first_bar * per_bar)
        raw = handle.readframes((last_bar - first_bar) * per_bar)
    return len(raw) // (2 * width), raw_dbfs(raw, width)


def render_measure(session, path, bars=None, first_bar=0, last_bar=1):
    """render.render the live session and measure what it wrote.

    Returns (dBFS, frames_or_None, reply). With `bars` the measurement is
    confined to bars [first_bar, last_bar) of the render.
    """
    rendered = session.result("render.render", {"out": path, "format": "wav"})
    if not rendered.get("path"):
        return None, None, rendered
    if bars is None:
        frames, level = wav_measure(path)
    else:
        frames, level = window_measure(path, bars, first_bar, last_bar)
    return level, frames, rendered


# Built-in instruments that make a sound with no sample and no setting up. The
# catalogue's first entry is AudioFileProcessor, which is SILENT until a sample
# is loaded - a fixture built on it would render silence and make every
# audibility check pass vacuously, so the audible ones are named and tried first.
AUDIBLE_INSTRUMENTS = ("tripleoscillator", "kicker", "organic", "lb302", "monstro", "sfxr")


def load_audible_instrument(session, track):
    """Load an instrument that sounds onto `track`. The device id, or None.

    None means the fixture could not be made audible: the caller must report
    Skipped rather than let silence pass for a working take.
    """
    catalogue = session.result("plugin.list", {"kind": "instrument", "loadable_only": True})
    devices = catalogue.get("devices") or []
    ordered = [d for name in AUDIBLE_INSTRUMENTS for d in devices if d.get("name") == name]
    ordered += [d for d in devices if d not in ordered]
    for device in ordered[:5]:
        loaded = session.result("plugin.load", {"target": track, "device": device.get("id")})
        if loaded.get("plugin"):
            print("instrument: %s (%s)" % (device.get("name"), device.get("format")))
            return device.get("id")
    return None


def note_ids(session, clip):
    """The ids of a clip's notes, from roll.get_state."""
    roll = session.result("roll.get_state", {"clip": clip})
    return [n.get("id") for n in (roll.get("notes") or []) if n.get("id")]
