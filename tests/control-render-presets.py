#!/usr/bin/env python3
"""END-TO-END proof of the render/export presets and the ranged render (rows 70/71).

THE CLAIM UNDER TEST, in two halves, each MEASURED rather than asserted:

  PRESETS. An agent over the control socket can save a named render/export preset (a
  sample rate, a bit depth, a stereo mode), read it back, apply it, remove it and
  reverse every one of those - and the applied preset is real: the next render's own
  WAV header carries the rate and the depth the preset named, which is checked by
  parsing the FILE, not by reading the command's answer.

  RANGE. `render.render` can render a TIME RANGE instead of the whole project, and the
  range is exact: a span of N ticks produces the frame count N ticks at that tempo and
  rate implies (Engine::updateFramesPerTick's own expression, recomputed here), two
  spans of the same start are proportional, and the audio of a rendered span IS the
  audio of the same span of the whole-project render (byte-compared after the header).
  Half a range, an empty range and a negative range are typed refusals that write
  nothing.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST: the release contract (CHARTER 3.1)
requires the feature to be reachable THROUGH THE SOCKET. So this drives the REAL binary
(`$<TARGET_FILE:zene>`, offscreen, the shared `control_socket_harness`), whose own temp
HOME/XDG world is also why the store this test writes is a temporary one.

THE BUDGET, declared rather than hidden: every render runs in a CHILD process and the
control surface does not answer - not even control.ping - until it finishes
(docs/RENDER-CHILD-WAIT.md). Each render.render call gets RENDER_TIMEOUT and its
measured wall time is printed, the way tests/control-stem-export-verb.py does it.

Usage: QT_QPA_PLATFORM=offscreen python3 control-render-presets.py <zene-binary>
Exit codes: 0 every check held; 1 a check failed (the app log is printed); 2 cannot run.
"""

from __future__ import annotations

import hashlib
import itertools
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: ONE FULL ENGINE START plus the render, not one socket round trip: the same number
#: the four commands that already render use (tests/control-stem-export-verb.py).
RENDER_TIMEOUT = 180.0
RENDER_COMMANDS = ("render.render",)

#: The preset this proof stores, and the settings it names.
PRESET = "socket 24/96"
PRESET_RATE = 96000
PRESET_DEPTH = "24"
PRESET_MODE = "stereo"

#: The tempo the checks DERIVE their frame counts from (pinned with transport.set_tempo)
#: and the engine's own conversion, src/core/Engine.cpp:140:
#:   framesPerTick = rate * 60 * 4 / DefaultTicksPerBar / bpm
TEMPO = 140
TICKS_PER_BAR = 768
DEFAULT_RATE = 44100
DEFAULT_DEPTH = "16"

#: One bar and two bars: the two spans every range check is made of. The song built
#: below is SONG_BARS bars, and the whole-project render adds the engine's own one-bar
#: tail (Song's m_exportTailBars), so it is strictly longer than TWO_BARS.
ONE_BAR = 768
TWO_BARS = 1536
SONG_BARS = 4

#: A frame count can land on the render's period boundary rather than on the tick, so
#: every expectation is compared with this slack (a whole LMMS period is 256 frames).
#: The failure this still catches is the one that matters: a range that was IGNORED is
#: off by a whole bar (75600 frames here), two orders of magnitude outside the slack.
FRAME_SLACK = 1024

REQUEST_IDS = itertools.count(1)


def frames_per_tick(rate, tempo=TEMPO):
    """The engine's own conversion, at the engine's own rate for the render."""
    return rate * 60.0 * 4 / TICKS_PER_BAR / tempo


def expected_frames(ticks, rate, tempo=TEMPO):
    return int(round(ticks * frames_per_tick(rate, tempo)))


def wav_info(path):
    """What a RIFF/WAVE file's own header says: rate, channels, bits, data window.

    Parsed here rather than trusted from the render's reply, because a reply can only
    report the settings it INTENDED while the encoder writes something else.
    """
    with open(path, "rb") as handle:
        blob = handle.read()
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("%s is not a RIFF/WAVE file" % path)
    info = {"bytes": len(blob), "rate": None, "channels": None, "bits": None,
            "data_offset": None, "data_size": None}
    offset = 12
    while offset + 8 <= len(blob):
        chunk_id = blob[offset:offset + 4]
        size = struct.unpack_from("<I", blob, offset + 4)[0]
        body = offset + 8
        if chunk_id == b"fmt " and size >= 16:
            channels, rate, bits = struct.unpack_from("<H I H", blob, body + 2)
            info.update(channels=channels, rate=rate, bits=bits)
        elif chunk_id == b"data":
            info["data_offset"] = body
            info["data_size"] = min(size, len(blob) - body)
        offset = body + size + (size % 2)
    if info["rate"] is None or info["data_offset"] is None:
        raise ValueError("%s has no fmt/data chunk (%r)" % (path, info))
    info["data"] = blob[info["data_offset"]:info["data_offset"] + info["data_size"]]
    info["frames"] = info["data_size"] // (info["channels"] * (info["bits"] // 8))
    return info


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

    def note(self, name, evidence):
        self.results.append((name, True, evidence))
        print("  note: %-58s %s" % (name, evidence))


def build_session(session):
    """One instrument track, a clip of SONG_BARS bars and a note on each bar.

    A fresh instance already holds the demo project's tracks, so nothing here assumes
    this clip is the whole song: the range checks derive their expectations from the
    SPAN they ask for and the tempo, never from the song's total length.
    """
    session.result("transport.set_tempo", {"bpm": TEMPO})
    track = session.result("track.add", {"type": "instrument", "name": "RenderPresets"})["track"]
    clip = session.result("clip.add", {"track": track, "position": 0,
                                       "length": ONE_BAR * SONG_BARS})["clip"]
    for bar in range(SONG_BARS):
        session.result("note.add", {"clip": clip, "key": 45 + bar, "position": bar * ONE_BAR,
                                    "length": ONE_BAR, "velocity": 100})
    return track, clip


def store_list(session):
    return session.result("export.preset_list")


def check_refusals(session, recorder, outdir):
    """Every refusal is typed, and NOTHING is written when one is refused."""
    before = store_list(session)
    recorder.check("the store starts empty in this instance's own world",
                   before.get("count") == 0 and before.get("active") is False, repr(before)[:200])
    escaped = session.result("export.preset_add", {"name": "..", "sample_rate": 48000,
                                                   "bit_depth": "16", "stereo_mode": "stereo"})
    recorder.check("a name that would escape the store is refused typed",
                   escaped.get("error", {}).get("kind") == "invalid_args", repr(escaped)[:200])
    low = session.result("export.preset_add", {"name": "too low", "sample_rate": 22050,
                                               "bit_depth": "16", "stereo_mode": "stereo"})
    recorder.check("a sample rate the render path cannot honour is refused typed",
                   low.get("error", {}).get("kind") == "invalid_args", repr(low)[:200])
    partial = session.result("export.preset_add", {"name": "half", "sample_rate": 48000})
    recorder.check("a preset missing its bit depth is refused typed",
                   partial.get("error", {}).get("kind") == "invalid_args", repr(partial)[:200])

    whole = os.path.join(outdir, "refused-whole.wav")
    half_range = session.result("render.render", {"out": whole, "start_ticks": ONE_BAR})
    recorder.check("half a range is refused typed",
                   half_range.get("error", {}).get("kind") == "invalid_args",
                   repr(half_range)[:200])
    empty = session.result("render.render", {"out": whole, "start_ticks": ONE_BAR,
                                             "end_ticks": ONE_BAR})
    recorder.check("an empty range is refused typed",
                   empty.get("error", {}).get("kind") == "invalid_args", repr(empty)[:200])
    negative = session.result("render.render", {"out": whole, "start_ticks": -1,
                                                "end_ticks": ONE_BAR})
    recorder.check("a negative range is refused typed",
                   negative.get("error", {}).get("kind") == "invalid_args", repr(negative)[:200])
    recorder.check("a refused range wrote no file at all",
                   not os.path.exists(whole), "out=%r exists=%r" % (whole, os.path.exists(whole)))
    recorder.check("the refused store writes left the store empty",
                   store_list(session).get("count") == 0, repr(store_list(session))[:200])


def add_preset(session, **overrides):
    args = {"name": PRESET, "sample_rate": PRESET_RATE, "bit_depth": PRESET_DEPTH,
            "stereo_mode": PRESET_MODE}
    args.update(overrides)
    return session.result("export.preset_add", args)


def check_store(session, recorder):
    """add / read back / no silent overwrite / byte-identical re-issue / list."""
    added = add_preset(session)
    recorder.check("export.preset_add saved the preset",
                   "error" not in added and added.get("replaced") is False, repr(added)[:250])
    if "error" in added:
        return None
    store = added.get("dir")
    document = os.path.join(store, added.get("name") + ".zrp")
    recorder.check("the document is on disk where the reply says it is",
                   os.path.isfile(document), "path=%r dir=%r" % (document, store))
    recorder.check("the reply's three settings are the ones asked for",
                   (added.get("sample_rate"), added.get("bit_depth"), added.get("stereo_mode"))
                   == (PRESET_RATE, PRESET_DEPTH, PRESET_MODE), repr(added)[:250])
    sha = added.get("sha256")

    duplicate = add_preset(session)
    recorder.check("an existing preset is refused unless overwrite is asked for",
                   duplicate.get("error", {}).get("kind") == "refused", repr(duplicate)[:250])
    changed = add_preset(session, stereo_mode="mono", overwrite=True)
    recorder.check("overwrite replaces the document and says it replaced one",
                   changed.get("replaced") is True and changed.get("sha256") != sha,
                   repr(changed)[:250])
    again = add_preset(session, overwrite=True)
    recorder.check("re-issuing the same settings rebuilds the document byte for byte",
                   again.get("sha256") == sha, "first=%r again=%r" % (sha, again.get("sha256")))

    listed = store_list(session)
    one = (listed.get("presets") or [{}])[0]
    recorder.check("export.preset_list reports the stored settings",
                   (listed.get("count"), one.get("name"), one.get("sample_rate"),
                    one.get("bit_depth"), one.get("stereo_mode"))
                   == (1, added.get("name"), PRESET_RATE, PRESET_DEPTH, PRESET_MODE),
                   repr(listed)[:300])
    recorder.check("export.preset_list reports the dir the documents live in",
                   listed.get("dir") == store, "dir=%r store=%r" % (listed.get("dir"), store))
    return {"document": document, "sha256": sha}


def render(session, recorder, outdir, name, args):
    """One render.render call: the reply, the wall time and the file's own header."""
    call = dict(args)
    call["out"] = os.path.join(outdir, name)
    result, seconds = session.timed("render.render", call)
    print("render.render %-18s took %.1fs (declared budget %.0fs)" % (name, seconds, RENDER_TIMEOUT))
    recorder.check("render.render %s answered success" % name, "error" not in result,
                   repr(result)[:300])
    if "error" in result:
        return None, None
    return result, wav_info(call["out"])


def check_whole_project(session, recorder, outdir):
    """A render with no preset and no range is the render this command always was."""
    result, info = render(session, recorder, outdir, "whole.wav", {})
    if result is None:
        return None
    recorder.check("a whole-project render reports no range and no applied preset",
                   result.get("range") is None and not result.get("applied_preset"),
                   "range=%r applied=%r" % (result.get("range"), result.get("applied_preset")))
    recorder.check("the default render's own header is 44100 Hz / 16 bit",
                   info["rate"] == DEFAULT_RATE and info["bits"] == 16
                   and result.get("bit_depth") == DEFAULT_DEPTH,
                   "header=%r reply_bit_depth=%r" % (info, result.get("bit_depth")))
    recorder.check("the whole-project render's frame count is the file's own frame count",
                   result.get("frames") == info["frames"],
                   "reply=%r header=%r" % (result.get("frames"), info["frames"]))
    return info


def check_ranges(session, recorder, outdir, whole):
    """The range is exact, proportional, and the same audio as that span of the whole."""
    info_one = None
    one, info_one = render(session, recorder, outdir, "one-bar.wav",
                           {"start_ticks": 0, "end_ticks": ONE_BAR})
    two, info_two = render(session, recorder, outdir, "two-bars.wav",
                           {"start_ticks": 0, "end_ticks": TWO_BARS})
    if one is None or two is None:
        return None
    want_one = expected_frames(ONE_BAR, DEFAULT_RATE)
    want_two = expected_frames(TWO_BARS, DEFAULT_RATE)
    recorder.check("one bar of ticks renders the frames the tempo implies (%d)" % want_one,
                   abs(info_one["frames"] - want_one) <= FRAME_SLACK,
                   "frames=%r want=%r" % (info_one["frames"], want_one))
    recorder.check("two bars of ticks render the frames the tempo implies (%d)" % want_two,
                   abs(info_two["frames"] - want_two) <= FRAME_SLACK,
                   "frames=%r want=%r" % (info_two["frames"], want_two))
    recorder.check("two bars of ticks render twice the frames of one",
                   abs(info_two["frames"] - 2 * info_one["frames"]) <= 2 * FRAME_SLACK,
                   "two=%r one=%r" % (info_two["frames"], info_one["frames"]))
    recorder.check("a ranged render is SHORTER than the whole-project render",
                   info_two["frames"] < whole["frames"],
                   "two=%r whole=%r" % (info_two["frames"], whole["frames"]))
    recorder.check("the reply reports the span it rendered",
                   (one.get("range") or {}).get("ticks") == ONE_BAR
                   and (two.get("range") or {}).get("end_ticks") == TWO_BARS,
                   "one=%r two=%r" % (one.get("range"), two.get("range")))
    recorder.check("the two rendered spans agree with each other, byte for byte",
                   info_two["data"].startswith(info_one["data"]),
                   "one=%d bytes two=%d bytes" % (len(info_one["data"]), len(info_two["data"])))
    recorder.check("a rendered span IS that span of the whole-project render, byte for byte",
                   whole["data"].startswith(info_two["data"]),
                   "whole=%d bytes two=%d bytes" % (len(whole["data"]), len(info_two["data"])))
    return info_one


def check_apply(session, recorder, outdir, info_one, document):
    """The applied preset reaches the RENDER: measured in the next file's own header."""
    applied = session.result("export.preset_apply", {"name": PRESET})
    recorder.check("export.preset_apply reports the settings it put in force",
                   (applied.get("active"), applied.get("sample_rate"), applied.get("bit_depth"),
                    applied.get("applied_preset")) == (True, PRESET_RATE, PRESET_DEPTH, PRESET),
                   repr(applied)[:250])
    recorder.check("export.preset_apply answers with the document's own path",
                   applied.get("path") == document,
                   "path=%r document=%r" % (applied.get("path"), document))

    result, info = render(session, recorder, outdir, "applied-96k.wav",
                          {"start_ticks": 0, "end_ticks": ONE_BAR})
    if result is None:
        return
    recorder.check("the applied preset reaches the FILE: its header is %d Hz / %s bit"
                   % (PRESET_RATE, PRESET_DEPTH),
                   info["rate"] == PRESET_RATE and info["bits"] == int(PRESET_DEPTH),
                   "header=%r" % (info,))
    want = expected_frames(ONE_BAR, PRESET_RATE)
    recorder.check("the same span at the preset's rate renders the frames it implies (%d)" % want,
                   abs(info["frames"] - want) <= FRAME_SLACK,
                   "frames=%r want=%r" % (info["frames"], want))
    recorder.check("the reply names the preset the render was started with",
                   (result.get("applied_preset"), result.get("sample_rate"))
                   == (PRESET, PRESET_RATE),
                   repr(result)[:250])
    # The rate is not a report-only field: the 96 kHz file must be longer than the
    # 44.1 kHz render of the SAME span by the ratio of the two rates.
    ratio = info["frames"] / float(info_one["frames"])
    recorder.check("96 kHz renders the frame ratio of 44100 and 96000",
                   abs(ratio - PRESET_RATE / float(DEFAULT_RATE)) < 0.01,
                   "ratio=%.4f frames=%r vs %r" % (ratio, info["frames"], info_one["frames"]))

    undone = session.result("control.undo")
    recorder.check("control.undo reverses the apply (it is a recorded step)",
                   (undone.get("undone_command"), undone.get("class"))
                   == ("export.preset_apply", "true_inverse"), repr(undone)[:250])
    after, info_after = render(session, recorder, outdir, "after-undo.wav",
                               {"start_ticks": 0, "end_ticks": ONE_BAR})
    if after is None:
        return
    recorder.check("after the undo the render is back to the default settings",
                   info_after["rate"] == DEFAULT_RATE and info_after["bits"] == 16,
                   "header=%r" % (info_after,))
    recorder.check("after the undo the SAME span renders the SAME audio, byte for byte",
                   info_after["data"] == info_one["data"],
                   "before=%d after=%d bytes" % (len(info_one["data"]), len(info_after["data"])))
    recorder.check("the undo left no preset applied",
                   store_list(session).get("active") is False, repr(store_list(session))[:200])


def check_remove(session, recorder, path_for):
    """The last verb: remove, and the store's own undo puts the document back."""
    document = path_for["document"]
    removed = session.result("export.preset_remove", {"name": PRESET})
    recorder.check("export.preset_remove deleted the document",
                   "error" not in removed and not os.path.exists(document),
                   "reply=%r exists=%r" % (removed, os.path.exists(document)))
    gone = session.result("export.preset_remove", {"name": PRESET})
    recorder.check("removing a preset the store does not hold is a typed not_found",
                   gone.get("error", {}).get("kind") == "not_found", repr(gone)[:200])
    undone = session.result("control.undo")
    recorder.check("control.undo puts the removed document back",
                   undone.get("undone_command") == "export.preset_remove"
                   and os.path.isfile(document), repr(undone)[:250])
    if os.path.isfile(document):
        with open(document, "rb") as handle:
            restored = handle.read()
        digest = hashlib.sha256(restored).hexdigest()
        recorder.check("the restored preset is the same document, byte for byte",
                       digest == path_for["sha256"] and store_list(session).get("count") == 1,
                       "sha256=%r was=%r" % (digest, path_for["sha256"]))
        recorder.note("the removed and restored document is %d bytes" % len(restored),
                      "sha256=%s" % digest)


def check_transactions(session, recorder):
    """The A16 record on the wire: the classes the rows claim, and what is NOT there."""
    entries = (session.result("control.transactions").get("transactions") or [])
    by_command = {}
    for entry in entries:
        by_command.setdefault(entry.get("command"), []).append(entry)
    for command in ("export.preset_add", "export.preset_apply", "export.preset_remove"):
        records = by_command.get(command) or []
        recorder.check("%s is on the wire as true_inverse" % command,
                       bool(records) and all(r.get("class") == "true_inverse" for r in records),
                       "records=%r" % (records[:1],))
        recorder.check("%s records a mechanism naming its inverse" % command,
                       bool(records) and all("checkpoint" in (r.get("mechanism") or "")
                                             for r in records),
                       "mechanism=%r" % ((records[0].get("mechanism") if records else None),))
    recorder.check("render.render leaves NO project transaction (it writes an output file)",
                   not by_command.get("render.render"),
                   "records=%r" % ((by_command.get("render.render") or [])[:1],))
    recorder.check("export.preset_list leaves no transaction either (it is a read)",
                   not by_command.get("export.preset_list"),
                   "records=%r" % ((by_command.get("export.preset_list") or [])[:1],))


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
    build_session(session)
    check_refusals(session, recorder, outdir)
    path_for = check_store(session, recorder)
    whole = check_whole_project(session, recorder, outdir) if path_for else None
    info_one = check_ranges(session, recorder, outdir, whole) if whole else None
    if info_one is not None:
        check_apply(session, recorder, outdir, info_one, path_for["document"])
    if path_for is not None:
        check_remove(session, recorder, path_for)
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
        recorder.problems.report("render/export preset + ranged render transcript")
        return 1
    H.ok("render/export presets and the ranged render (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
