#!/usr/bin/env python3
"""Control-surface transcript for `render.sections` and a stem export at the selected rate/depth.

THE CLAIM UNDER TEST (Q050-009 / feature row 90), measured on the REAL binary:

  1. SECTIONS. The song's named sections (section.add) are exported one bounded file per
     section, each NAMED after the section and rendered to exactly the section's own ticks -
     a span of N ticks produces the frames the tempo and rate imply (Engine::updateFramesPerTick's
     expression, recomputed here); each file's own WAV header is parsed rather than trusting the
     reply.

     PORTABLE NAMES: the file names are the same on every platform. Names that differ only by
     case (Intro / intro) are two files (the second gets "-2"), a Windows-reserved name (CON)
     gets "_" appended, and a trailing dot or space becomes "_" - each file still bounded to its
     own section's ticks.

  2. SELECTED SETTINGS. A render preset applied through export.preset_apply sets the rate and
     bit depth of BOTH a section export AND a stem export: the headers of the files written by
     render.sections and render.stems carry the preset's 48 kHz / 24-bit, not the old fixed
     44.1 kHz / 16-bit.

WHY IT IS A REGISTERED CTEST: the release contract (CHARTER 3.1) requires the feature to be
reachable THROUGH THE SOCKET, so this drives the REAL binary (offscreen, off the shared harness)
in its own temp HOME/XDG world. THE BUDGET is declared rather than hidden: every render runs in a
child process and the surface does not answer - control.ping included - until it finishes
(docs/RENDER-CHILD-WAIT.md); render.sections runs one child PER SECTION, so its budget is a
multiple.

Usage: QT_QPA_PLATFORM=offscreen python3 control-render-sections.py <zene-binary>
Exit codes: 0 every check held; 1 a check failed (the app log is printed); 2 cannot run.
"""

from __future__ import annotations

import itertools
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402

#: ONE FULL ENGINE START plus the render. render.sections starts one child per section (two
#: here), so its own call gets twice that plus slack.
RENDER_TIMEOUT = 180.0
SECTIONS_TIMEOUT = 5 * RENDER_TIMEOUT

PRESET = "sections 48-24"
PRESET_RATE = 48000
PRESET_DEPTH = "24"

TEMPO = 140
DEFAULT_TICKS_PER_BAR = 192
ONE_BAR = 768
SONG_BARS = 3

#: A frame count lands on the render's period boundary rather than on the tick; an ignored range
#: is off by a whole bar, two orders of magnitude outside this slack.
FRAME_SLACK = 1024

REQUEST_IDS = itertools.count(1)


def frames_per_tick(rate, tempo=TEMPO):
    return rate * 60.0 * 4 / DEFAULT_TICKS_PER_BAR / tempo


def expected_frames(ticks, rate, tempo=TEMPO):
    return int(round(ticks * frames_per_tick(rate, tempo)))


def wav_header(path):
    """What a RIFF/WAVE file's own header says: rate, channels, bits, data window and frames."""
    with open(path, "rb") as handle:
        blob = handle.read()
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("%s is not a RIFF/WAVE file" % path)
    info = {"bytes": len(blob), "rate": None, "channels": None, "bits": None,
            "data_size": None}
    offset = 12
    while offset + 8 <= len(blob):
        chunk_id = blob[offset:offset + 4]
        size = struct.unpack_from("<I", blob, offset + 4)[0]
        body = offset + 8
        if chunk_id == b"fmt " and size >= 16:
            info["channels"], info["rate"] = struct.unpack_from("<H I", blob, body + 2)
            info["bits"] = struct.unpack_from("<H", blob, body + 14)[0]
        elif chunk_id == b"data":
            info["data_size"] = min(size, len(blob) - body)
        offset = body + size + (size % 2)
    if info["rate"] is None or info["data_size"] is None:
        raise ValueError("%s has no fmt/data chunk (%r)" % (path, info))
    info["frames"] = info["data_size"] // (info["channels"] * (info["bits"] // 8))
    return info


class Session:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None, timeout=None):
        return self.client.call(next(REQUEST_IDS), command, args, timeout=timeout,
                                transcript=self.transcript)

    def result(self, command, args=None, timeout=None):
        reply = self.call(command, args, timeout=timeout)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def timed(self, command, args=None, timeout=None):
        started = time.time()
        result = self.result(command, args, timeout=timeout)
        return result, time.time() - started


class Recorder:
    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def build_session(session):
    session.result("transport.set_tempo", {"bpm": TEMPO})
    track = session.result("track.add", {"type": "instrument", "name": "Sections"})["track"]
    clip = session.result("clip.add", {"track": track, "position": 0,
                                       "length": ONE_BAR * SONG_BARS})["clip"]
    for bar in range(SONG_BARS):
        session.result("note.add", {"clip": clip, "key": 45 + bar, "position": bar * ONE_BAR,
                                    "length": ONE_BAR, "velocity": 100})
    return track


def check_sections(session, recorder, outdir):
    """Two named sections -> two correctly named, bounded files at the applied rate/depth."""
    intro = session.result("section.add", {"start": 0, "end": ONE_BAR, "name": "Intro"})
    verse = session.result("section.add", {"start": ONE_BAR, "end": 3 * ONE_BAR, "name": "Verse"})
    recorder.check("section.add names the two sections",
                   "error" not in intro and "error" not in verse, "%r %r" % (intro, verse))
    if "error" in intro or "error" in verse:
        return

    directory = os.path.join(outdir, "sections")
    result, seconds = session.timed("render.sections", {"out": directory},
                                    timeout=SECTIONS_TIMEOUT)
    print("render.sections took %.1fs (declared budget %.0fs)" % (seconds, SECTIONS_TIMEOUT))
    recorder.check("render.sections answered success", "error" not in result, repr(result)[:300])
    if "error" in result:
        return

    names = sorted(os.listdir(directory)) if os.path.isdir(directory) else []
    recorder.check("one file per section, named after the section",
                   names == ["Intro.wav", "Verse.wav"], "names=%r" % (names,))
    recorder.check("the reply lists one file per section with its ticks",
                   result.get("count") == 2
                   and [(f.get("name"), f.get("start_ticks"), f.get("end_ticks"))
                        for f in result.get("files", [])]
                   == [("Intro", 0, ONE_BAR), ("Verse", ONE_BAR, 3 * ONE_BAR)],
                   repr(result.get("files"))[:400])

    for name, span in (("Intro.wav", ONE_BAR), ("Verse.wav", 2 * ONE_BAR)):
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            recorder.check("%s exists" % name, False, "missing %r" % (path,))
            continue
        want = expected_frames(span, PRESET_RATE)
        info = wav_header(path)
        recorder.check("%s is bounded to its %d ticks (%d frames, want %d)"
                       % (name, span, info["frames"], want),
                       abs(info["frames"] - want) <= FRAME_SLACK,
                       "frames=%r want=%r" % (info["frames"], want))
        recorder.check("%s follows the applied preset: %d Hz / %d bit"
                       % (name, PRESET_RATE, int(PRESET_DEPTH)),
                       info["rate"] == PRESET_RATE and info["bits"] == int(PRESET_DEPTH),
                       "header=%r" % (info,))
    recorder.check("the reply names the applied settings",
                   (result.get("sample_rate"), result.get("bit_depth"), result.get("applied_preset"))
                   == (PRESET_RATE, PRESET_DEPTH, PRESET),
                   repr(result)[:250])


def check_portable_names(session, recorder, outdir):
    """Section file names are the same on EVERY platform: names that differ only
    by case stay distinct files, Windows-reserved and trailing-dot names become
    valid ones, and each file keeps its own section's bounds."""
    quarter = ONE_BAR // 3
    base = 2 * ONE_BAR
    added = [("intro", base, base + quarter), ("CON", base + quarter, base + 2 * quarter),
             ("Coda. ", base + 2 * quarter, 3 * ONE_BAR)]
    for name, start, end in added:
        reply = session.result("section.add", {"start": start, "end": end, "name": name})
        recorder.check("section.add accepts %r" % name, "error" not in reply, repr(reply)[:200])
        if "error" in reply:
            return
    directory = os.path.join(outdir, "sections-portable")
    result, seconds = session.timed("render.sections", {"out": directory},
                                    timeout=SECTIONS_TIMEOUT)
    print("portable render.sections took %.1fs" % seconds)
    recorder.check("portable render.sections answered success", "error" not in result,
                   repr(result)[:300])
    if "error" in result:
        return
    wanted = ["CON_.wav", "Coda_.wav", "Intro.wav", "Verse.wav", "intro-2.wav"]
    names = sorted(os.listdir(directory)) if os.path.isdir(directory) else []
    recorder.check("case-only names are distinct files, CON and 'Coda. ' are valid names",
                   names == wanted, "names=%r want=%r" % (names, wanted))
    folded = [name.lower() for name in names]
    recorder.check("no two file names collide on a case-insensitive filesystem",
                   len(set(folded)) == len(folded), "names=%r" % (names,))
    reported = {entry.get("name"): entry for entry in result.get("files", [])}
    spans = {"Intro": ("Intro.wav", 0, ONE_BAR), "Verse": ("Verse.wav", ONE_BAR, 3 * ONE_BAR),
             "intro": ("intro-2.wav", base, base + quarter),
             "CON": ("CON_.wav", base + quarter, base + 2 * quarter),
             "Coda.": ("Coda_.wav", base + 2 * quarter, 3 * ONE_BAR)}
    recorder.check("the reply lists the five sections",
                   result.get("count") == 5, repr(result.get("files"))[:400])
    for key, (file_name, start, end) in spans.items():
        entry = reported.get(key) or reported.get(key + " ") or {}
        recorder.check("%s reports its own bounds %d..%d" % (file_name, start, end),
                       (entry.get("start_ticks"), entry.get("end_ticks")) == (start, end)
                       and os.path.basename(entry.get("path") or "") == file_name,
                       repr(entry)[:300])
        path = os.path.join(directory, file_name)
        if not os.path.isfile(path):
            recorder.check("%s exists" % file_name, False, "missing %r" % (path,))
            continue
        want = expected_frames(end - start, PRESET_RATE)
        info = wav_header(path)
        recorder.check("%s is bounded to its %d ticks (%d frames, want %d)"
                       % (file_name, end - start, info["frames"], want),
                       abs(info["frames"] - want) <= FRAME_SLACK,
                       "frames=%r want=%r" % (info["frames"], want))


def check_stems_follow_the_selection(session, recorder, outdir):
    """render.stems writes at the selected preset's rate and bit depth, not a fixed 44.1 kHz."""
    directory = os.path.join(outdir, "stems")
    result, seconds = session.timed("render.stems", {"out": directory, "format": "wav"},
                                    timeout=RENDER_TIMEOUT)
    print("render.stems took %.1fs (declared budget %.0fs)" % (seconds, RENDER_TIMEOUT))
    recorder.check("render.stems answered success", "error" not in result, repr(result)[:300])
    if "error" in result:
        return
    recorder.check("render.stems reports the selected rate and depth",
                   result.get("sample_rate") == PRESET_RATE and result.get("bit_depth") == PRESET_DEPTH,
                   "sample_rate=%r bit_depth=%r" % (result.get("sample_rate"),
                                                    result.get("bit_depth")))
    stems = result.get("stems") or []
    if not stems:
        recorder.check("a stem file was written", False, repr(result)[:200])
        return
    info = wav_header(os.path.join(directory, stems[0]))
    recorder.check("a stem's own header carries the selected rate and depth",
                   info["rate"] == PRESET_RATE and info["bits"] == int(PRESET_DEPTH),
                   "header=%r" % (info,))


def check_refusals(session, recorder, outdir):
    relative = session.result("render.sections", {"out": "sections"})
    recorder.check("a relative out is refused typed", relative.get("error", {}).get("kind") == "invalid_args",
                   repr(relative)[:200])
    unknown = session.result("render.sections", {"out": os.path.join(outdir, "unknown"),
                                                 "sections": ["sec-999"]})
    recorder.check("an unknown section is a typed not_found",
                   unknown.get("error", {}).get("kind") == "not_found", repr(unknown)[:200])
    # Half a range is refused by render.render under render.sections too, but a section always
    # carries both ticks; the section-list refusals above are the ones this verb adds.


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
    store = session.result("export.preset_add", {"name": PRESET, "sample_rate": PRESET_RATE,
                                                 "bit_depth": PRESET_DEPTH, "stereo_mode": "stereo"})
    recorder.check("export.preset_add stored the preset", "error" not in store, repr(store)[:200])
    applied = session.result("export.preset_apply", {"name": PRESET})
    recorder.check("export.preset_apply put it in force", "error" not in applied, repr(applied)[:200])
    check_refusals(session, recorder, outdir)
    check_sections(session, recorder, outdir)
    check_portable_names(session, recorder, outdir)
    check_stems_follow_the_selection(session, recorder, outdir)


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
        recorder.problems.report("section export + selected-rate stem transcript")
        return 1
    H.ok("section export and selected-rate/depth stem export (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
