#!/usr/bin/env python3
"""The reworked note editor, driven end to end over the control socket.

The note editor (docs/NOTE-EDITOR.md) is the shell's detail panel. Every edit it makes is a
registered command, and its view is driven by window.note_editor_set, so an agent can do what
a user does. This driver starts the REAL `zene` binary headless and proves:

  1. `window.note_editor_set {clip}` opens a clip in the editor, reveals the panel, and answers
     the editor's state - the same object `window.get_state` reports as `note_editor`;
  2. the view choices round-trip: tool, grid, fold (the row count follows the fold), lane,
     ghosts, zoom - and an unknown tool is refused, typed;
  3. `note.edit` changes several notes - position, key, length, velocity, pan, chance - as ONE
     edit, and ONE `control.undo` restores every one of them;
  4. a batch with one bad entry changes nothing (validated before anything is written);
  5. the MPE lanes write through `note.expression_set`, and the chance lane through note.edit;
  6. `window.screenshot` renders the Edit view with the editor in the centre;
  7. (0.5.0 L07) note.add_many adds a batch as one undo step and note.list answers every note;
     note.scale, note.ratchet and note.transform_where change exactly the notes they name;
     groove.quantize takes 'swing'; key_names.* name a track's keys (one undo restores);
     window.note_editor_set drives the paint tool, the drum view, the colour mode and
     keep-in-scale; a Lua script adds notes through zene.command.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-note-editor.py <zene> [<screenshot dir>]
Exit 0 only when every check held.
"""

import os
import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def make_clip(session):
    track = session.result("track.add", {"type": "instrument", "name": "Keys"}).get("track")
    clip = session.result("clip.add", {"track": track, "position": 0}).get("clip")
    notes = []
    for i, key in enumerate((60, 64, 67, 72)):
        added = session.result("note.add", {"clip": clip, "key": key, "position": i * 48, "length": 48,
                                            "velocity": 100})
        notes.append(added.get("note"))
    return clip, notes


def roll_notes(session, clip):
    return {n.get("id"): n for n in session.result("roll.get_state", {"clip": clip}).get("notes") or []}


def check_open(session, recorder, clip):
    opened = session.result("window.note_editor_set", {"clip": clip})
    recorder.check("note_editor_set opens the clip", opened.get("clip") == clip, "reply=%r" % opened)
    state = session.result("window.get_state")
    recorder.check("get_state reports the same editor", (state.get("note_editor") or {}).get("clip") == clip,
                   "note_editor=%r" % state.get("note_editor"))
    detail = [p for p in state.get("panels") or [] if p.get("id") == "detail"]
    recorder.check("the note editor panel is on screen", bool(detail) and detail[0].get("visible") is True,
                   "detail=%r" % detail)


def check_view(session, recorder):
    reply = session.result("window.note_editor_set", {"tool": "slice", "grid": 24, "lane": "pan", "ghosts": False,
                                                      "zoom": 0.5})
    recorder.check("tool, grid, lane, ghosts and zoom round-trip",
                   reply.get("tool") == "slice" and reply.get("grid") == 24 and reply.get("lane") == "pan"
                   and reply.get("ghosts") is False and abs((reply.get("zoom") or 0) - 0.5) < 1e-9, "reply=%r" % reply)
    used = session.result("window.note_editor_set", {"fold": "used"})
    recorder.check("folding to used keys shows the four keys", used.get("rows") == 4, "reply=%r" % used)
    full = session.result("window.note_editor_set", {"fold": "none"})
    recorder.check("unfolding shows every key", full.get("rows") == 128, "reply=%r" % full)
    error = session.typed_error("window.note_editor_set", {"tool": "chisel"})
    recorder.check("an unknown tool is refused, typed", error.get("kind") == "invalid_args", "error=%r" % error)


def has_fields(note, expected):
    """Whether the roll.get_state note carries every expected field value."""
    return all((note or {}).get(key) == value for key, value in expected.items())


BATCH_FIRST = {"position": 12, "key": 61, "velocity": 150}
BATCH_SECOND = {"length": 96, "pan": -40}


def check_batch(session, recorder, clip, notes):
    edits = [dict(BATCH_FIRST, note=notes[0]), dict(BATCH_SECOND, note=notes[1], probability=0.5)]
    reply = session.result("note.edit", {"clip": clip, "edits": edits})
    after = roll_notes(session, clip)
    applied = reply.get("edited") == 2 and has_fields(after.get(notes[0]), BATCH_FIRST) \
        and has_fields(after.get(notes[1]), BATCH_SECOND)
    recorder.check("note.edit applies every field of the batch", applied, "after=%r" % after)
    session.result("control.undo")
    undone = roll_notes(session, clip)
    restored = has_fields(undone.get(notes[0]), {"position": 0, "key": 60}) \
        and has_fields(undone.get(notes[1]), {"length": 48})
    recorder.check("ONE control.undo restores the whole batch", restored, "undone=%r" % undone)


def check_atomic(session, recorder, clip, notes):
    before = roll_notes(session, clip)
    error = session.typed_error("note.edit", {"clip": clip, "edits": [{"note": notes[0], "key": 70},
                                                                      {"note": notes[1], "velocity": 999}]})
    recorder.check("a batch with a bad entry is refused, typed", error.get("kind") == "invalid_args", "error=%r" % error)
    recorder.check("and changes nothing", roll_notes(session, clip) == before, "the clip changed")


def check_lanes(session, recorder, clip, notes):
    timbre = session.result("note.expression_set", {"clip": clip, "note": notes[2], "timbre": 90})
    recorder.check("the timbre lane's command writes per-note expression", timbre.get("timbre") == 90, "reply=%r" % timbre)
    chance = session.result("note.edit", {"clip": clip, "edits": [{"note": notes[3], "probability": 0.25}]})
    recorder.check("the chance lane's command writes the note's probability", chance.get("edited") == 1, "reply=%r" % chance)


def phrase(session, clip):
    """A two-bar phrase with a velocity contour, so the screenshot shows what the editor draws."""
    melody = (60, 62, 64, 67, 65, 64, 62, 60, 64, 67, 69, 72, 71, 67, 64, 62)
    for i, key in enumerate(melody):
        session.result("note.add", {"clip": clip, "key": key, "position": 192 + i * 24, "length": 22,
                                    "velocity": 40 + (i * 37) % 160})
    session.result("clip.resize", {"clip": clip, "length": 192 * 4})
    session.result("scale.set", {"scale": "Major"})


def check_screenshot(session, recorder, outdir, clip):
    phrase(session, clip)
    session.result("window.set_view", {"view": "edit"})
    session.result("window.note_editor_set", {"clip": clip, "tool": "draw", "lane": "velocity", "ghosts": True,
                                              "fit": True})
    path = os.path.join(outdir, "note-editor-edit-view.png")
    shot = session.result("window.screenshot", {"path": path})
    recorder.check("the Edit view renders with the note editor in the centre",
                   (shot.get("width") or 0) >= 1600 and os.path.exists(path), "reply=%r" % shot)


def shape(session, clip):
    rows = session.result("note.list", {"clip": clip}).get("notes") or []
    return ["%d@%d+%d" % (r["key"], r["position"], r["length"]) for r in rows]


def new_clip(session, name):
    track = session.result("track.add", {"type": "instrument", "name": name}).get("track")
    return track, session.result("clip.add", {"track": track, "position": 0}).get("clip")


def check_note_shapes(session, recorder):
    _, clip = new_clip(session, "Shapes")
    start = ["60@0+48", "61@48+12", "67@96+12"]
    added = session.result("note.add_many", {"clip": clip, "notes": [
        {"key": 60, "position": 0, "length": 48}, {"key": 61, "position": 48, "length": 12, "velocity": 40},
        {"key": 67, "position": 96, "length": 12, "velocity": 160}]})
    recorder.check("note.add_many adds the batch, note.list answers it",
                   added.get("added") == 3 and shape(session, clip) == start, "notes=%r" % shape(session, clip))
    bad = session.call("note.add_many", {"clip": clip, "notes": [{"key": 200, "position": 0, "length": 1}]})
    recorder.check("a bad note batch is refused, typed", (bad.get("error") or {}).get("kind") == "invalid_args", "reply=%r" % bad)
    session.result("note.scale", {"clip": clip, "time_factor": 2.0, "length_factor": 1.0})
    recorder.check("note.scale x2 doubles the positions", shape(session, clip) == ["60@0+48", "61@96+12", "67@192+12"],
                   "notes=%r" % shape(session, clip))
    session.result("control.undo")
    session.result("note.transform_where", {"clip": clip, "filter": {"length_max": 12}, "action": "remove"})
    recorder.check("find and change removes the short notes", shape(session, clip) == ["60@0+48"], "notes=%r" % shape(session, clip))
    session.result("note.ratchet", {"clip": clip, "count": 4})
    recorder.check("a ratchet splits the note in four", shape(session, clip) == ["60@0+12", "60@12+12", "60@24+12", "60@36+12"],
                   "notes=%r" % shape(session, clip))
    session.result("control.undo")
    session.result("control.undo")
    recorder.check("each note edit is one undo step", shape(session, clip) == start, "notes=%r" % shape(session, clip))
    _, swung = new_clip(session, "Swing")
    session.result("note.add_many", {"clip": swung, "notes": [{"key": 60, "position": 1, "length": 12},
                                                              {"key": 62, "position": 25, "length": 12}]})
    session.result("groove.quantize", {"clip": swung, "grid": 24, "swing": 0.5})
    recorder.check("quantise with swing delays the off step", shape(session, swung) == ["60@0+12", "62@30+12"],
                   "notes=%r" % shape(session, swung))


def key_names(session, track):
    return {n["key"]: n["name"] for n in session.result("key_names.get", {"track": track}).get("names") or []}


def check_drum_view(session, recorder):
    track, clip = new_clip(session, "Drums")
    session.result("key_names.preset", {"track": track, "preset": "gm_drums"})
    names = key_names(session, track)
    recorder.check("the GM drum preset names the keys", names.get(24) == "Bass Drum 1" and len(names) == 47,
                   "names=%d key24=%r" % (len(names), names.get(24)))
    session.result("key_names.set", {"track": track, "key": 24, "name": "Kick"})
    session.result("control.undo")
    recorder.check("one undo restores a renamed key", key_names(session, track).get(24) == "Bass Drum 1",
                   "key24=%r" % key_names(session, track).get(24))
    check_view_fields(session, recorder, clip)
    check_lua_notes(session, recorder, clip)


def check_view_fields(session, recorder, clip):
    reply = session.call("window.note_editor_set", {"clip": clip, "tool": "paint", "fold": "drums",
                                                    "color_mode": "pitch", "keep_in_scale": True})
    state = reply.get("result") or {}
    recorder.check("window.note_editor_set drives paint, drum view, colour and keep-in-scale",
                   reply.get("ok") and state.get("tool") == "paint" and state.get("fold") == "drums"
                   and state.get("color_mode") == "pitch" and state.get("keep_in_scale") is True
                   and state.get("rows") == 47, "reply=%r" % reply)
    session.call("window.note_editor_set", {"tool": "draw", "fold": "none", "color_mode": "velocity", "keep_in_scale": False})


def check_lua_notes(session, recorder, clip):
    source = ("local r, err = zene.command('note.add_many', {clip = '%s', notes = {{key = 24, position = 0, length = 6}, "
              "{key = 26, position = 6, length = 6}}})\n"
              "print('added ' .. tostring(err == nil and r.added))\n" % clip)
    ran = session.result("script.run", {"source": source})
    lines = ran.get("log") or ran.get("lines") or ran.get("output") or []
    text = "\n".join(lines) if isinstance(lines, list) else str(lines)
    recorder.check("a Lua script adds notes through zene.command", "added 2" in text, "log=%r" % ran)


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
        outdir = os.path.abspath(argv[2]) if len(argv) > 2 else instance.tmp
        os.makedirs(outdir, exist_ok=True)
        session = Session(client, transcript)
        clip, notes = make_clip(session)
        check_open(session, recorder, clip)
        check_view(session, recorder)
        check_batch(session, recorder, clip, notes)
        check_atomic(session, recorder, clip, notes)
        check_lanes(session, recorder, clip, notes)
        check_screenshot(session, recorder, outdir, clip)
        check_note_shapes(session, recorder)
        check_drum_view(session, recorder)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("note editor control-surface driver")
        return 1
    H.ok("note editor control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
