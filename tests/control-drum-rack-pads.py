#!/usr/bin/env python3
"""Q050-066 stage 1 over the control socket: a drum rack's sample pads, created, edited and read back.

Starts the REAL `zene` binary headless (its own throwaway config) and drives it as an agent does:

  1. an OLD-STYLE rack - built with the pre-existing track.drum_rack alone, each pad playing one note at
     its key - is saved, reopened and rendered: it opens with no load error, the new
     track.drum_rack_get_state reads the same pads back, and the reopened render equals the first under
     the repository's audio comparison contract (tests/golden_audio_lib.py: the same-build run-to-run
     floor, then max |delta|, level and envelope); the saved project carries no element or attribute the
     base format lacks (every tag is one the old project already had);
  2. pads are created and edited through the registered commands - track.drum_rack_pad_add with a name
     and a trigger key, track.drum_rack_pad_set with a new name and key - and the answers are the state
     get_state reads back;
  3. each edit is one undo step over the socket (control.undo puts the previous pad back, control.redo
     the edit), and the edited rack survives a save and reopen field for field and renders equal;
  4. NEGATIVE CONTROL: the edited rack sounds different from the old one, and the same comparison must
     say so - so the equality checks above can fail.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-drum-rack-pads.py <zene>
Exit 0 only when every check held; 77 (ctest Skipped) when the build cannot load the sampler.
"""

import os
import sys
import xml.etree.ElementTree as ET

import control_socket_harness as H
import golden_audio_lib as G
from freeze_bounce_evidence import Recorder, Session, SILENT_DBFS, report_results

SAMPLES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "samples", "drums")
CLIP_TICKS = 192


def sample(name):
    return os.path.normpath(os.path.join(SAMPLES, name))


def note_on_pad(session, pad, key, position):
    """One clip on @pad with one note at @key: the pad's sample, played at @position ticks."""
    clip = session.result("clip.add", {"track": pad, "position": 0, "length": CLIP_TICKS * 2}).get("clip")
    session.result("note.add", {"clip": clip, "key": key, "position": position, "length": 48, "velocity": 110})
    return clip


def rack_state(session, folder):
    return session.result("track.drum_rack_get_state", {"track": folder})


def second_pad(session, folder):
    pads = rack_state(session, folder).get("pads", [])
    return pads[1] if len(pads) > 1 else None


def render(session, path):
    reply = session.result("render.render", {"out": path, "format": "wav"})
    return path if reply.get("path") else None


def tags_of(path):
    return {element.tag for element in ET.parse(path).getroot().iter()}


def same_sound(recorder, name, floor_paths, first, other, expect_equal=True):
    tolerance = G.tolerance(G.measure_floor(floor_paths))
    passed, lines = G.verdict(G.compare(first, other), tolerance)
    print("%s:\n%s" % (name, "\n".join(lines)))
    recorder.check(name, passed if expect_equal else not passed, "\n".join(lines))


def check_old_rack(session, recorder, work):
    """An old-style rack: saved, reopened, read back and rendered the same. Returns (folder, state, renders, tags)."""
    made = session.result("track.drum_rack", {"name": "Kit", "pads": [
        {"sample": sample("bassdrum01.ogg"), "name": "Kick"}, {"sample": sample("snare01.ogg"), "key": 38}]})
    if made.get("error"):
        return None
    folder, pads = made["folder"], made["tracks"][:-1]
    note_on_pad(session, pads[0], 36, 0)
    note_on_pad(session, pads[1], 38, CLIP_TICKS // 2)
    before = rack_state(session, folder)
    recorder.check("the old-style rack reads back its two pads, names and keys",
                   [(p["name"], p["key"], p["sends_to"]) for p in before.get("pads", [])]
                   == [("Kick", 36, made["bus"]), ("snare01", 38, made["bus"])], "state=%r" % before)
    first, second = render(session, os.path.join(work, "old-1.wav")), render(session, os.path.join(work, "old-2.wav"))
    level = G.file_dbfs(first)[1] if first else None
    recorder.check("the old-style rack renders audible audio", level is not None and level > SILENT_DBFS, "dBFS=%r" % level)
    saved = os.path.join(work, "old.mmp")
    session.result("project.save", {"path": saved})
    opened = session.result("project.open", {"path": saved})
    recorder.check("the saved old-style rack opens with no load error", opened.get("error_count") == 0, "reply=%r" % opened)
    recorder.check("the reopened old-style rack reads back the same state", rack_state(session, folder) == before,
                   "after=%r" % rack_state(session, folder))
    again = render(session, os.path.join(work, "old-reopened.wav"))
    same_sound(recorder, "the reopened old-style rack renders equal to the first", [first, second], first, again)
    return folder, before, [first, second], tags_of(saved)


def check_pad_edits(session, recorder, folder, bus):
    """Pads made and edited through the commands; each edit one undo step. Returns (hat, snare) pad ids."""
    added = session.result("track.drum_rack_pad_add", {"track": folder, "sample": sample("hihat_closed01.ogg"),
                                                       "name": "Hat", "key": 42})
    recorder.check("track.drum_rack_pad_add makes a named pad on its key, sending to the bus",
                   (added.get("name"), added.get("key"), added.get("sends_to")) == ("Hat", 42, bus), "reply=%r" % added)
    state = rack_state(session, folder)
    recorder.check("the added pad's answer is what get_state reads", state.get("pads", [])[-1:] == [added], "state=%r" % state)
    if len(state.get("pads", [])) < 3:
        return None
    note_on_pad(session, added.get("track"), 42, CLIP_TICKS)
    snare = state["pads"][1]
    edited = session.result("track.drum_rack_pad_set", {"track": snare["track"], "name": "Rim", "key": 40})
    recorder.check("track.drum_rack_pad_set renames and re-keys the pad",
                   (edited.get("name"), edited.get("key")) == ("Rim", 40), "reply=%r" % edited)
    recorder.check("the edit's answer is what get_state reads", second_pad(session, folder) == edited,
                   "state=%r" % rack_state(session, folder))
    session.result("control.undo")
    recorder.check("one control.undo puts the pad's name and key back together",
                   second_pad(session, folder) == snare, "state=%r" % rack_state(session, folder))
    session.result("control.redo")
    recorder.check("control.redo applies the edit again", second_pad(session, folder) == edited,
                   "state=%r" % rack_state(session, folder))
    return added.get("track"), snare["track"]


def check_edited_round_trip(session, recorder, work, folder, old_render, old_tags):
    state = rack_state(session, folder)
    first, second = render(session, os.path.join(work, "edited-1.wav")), render(session, os.path.join(work, "edited-2.wav"))
    saved = os.path.join(work, "edited.mmp")
    session.result("project.save", {"path": saved})
    recorder.check("the saved project carries no element the old format lacks", tags_of(saved) <= old_tags,
                   "new tags=%r" % sorted(tags_of(saved) - old_tags))
    session.result("track.drum_rack_pad_set", {"track": state["pads"][0]["track"], "name": "Moved", "key": 60})
    opened = session.result("project.open", {"path": saved})
    recorder.check("the edited rack opens with no load error", opened.get("error_count") == 0, "reply=%r" % opened)
    recorder.check("the reopened rack reads back every pad field for field", rack_state(session, folder) == state,
                   "saved=%r reopened=%r" % (state, rack_state(session, folder)))
    again = render(session, os.path.join(work, "edited-reopened.wav"))
    same_sound(recorder, "the reopened edited rack renders equal to the saved one", [first, second], first, again)
    same_sound(recorder, "negative control: the edited rack does not render like the old one", old_render, old_render[0],
               first, expect_equal=False)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1], workingdir=None) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        listed = session.result("plugin.list", {"kind": "instrument", "loadable_only": True}).get("devices") or []
        if not any(str(d.get("name", "")).lower() == "audiofileprocessor" for d in listed):
            H.fail("build lists no loadable audiofileprocessor module", instance, transcript)
        work = instance.workspace
        old = check_old_rack(session, recorder, work)
        if old is None:
            transcript.dump()
            H.fail("track.drum_rack refused the fixture", instance, transcript)
        folder, before, old_render, old_tags = old
        if check_pad_edits(session, recorder, folder, before.get("bus")) is not None:
            check_edited_round_trip(session, recorder, work, folder, old_render, old_tags)
        log = instance.read_log()
        recorder.check("no sanitizer or crash report in the instance log",
                       "AddressSanitizer" not in log and "Segmentation" not in log, "log tail=%r" % log[-400:])
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("drum rack pads control-surface driver")
        return 1
    H.ok("drum rack pads control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
