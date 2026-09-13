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
import wave

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))

# Below this, a render is "no audio at all" rather than a quiet one.
SILENT_DBFS = -60.0


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        return self.client.call(next(REQUEST_IDS), command, args, transcript=self.transcript)

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
