#!/usr/bin/env python3
"""Q050-294.1: the melodic step state of an ordinary MIDI clip, driven over the control socket.

A melodic step view is a grid VIEW of a MidiClip: every cell is one ordinary note, so the socket
reads and edits the same clip the piano roll shows. This driver starts the REAL `zene` binary
headless and proves:

  1. note.step_get_state reports each note's step, key, velocity, chance and ratchet, the scale
     the cells are read against (in_scale, scale_degree) and the occupied columns; notes made the
     ordinary way read chance 1 and ratchet 1 (the old-note defaults);
  2. note.step_set edits pitch, velocity, chance and ratchet, adds and removes cells, snaps a new
     pitch into the set scale and leaves an out-of-scale note editable - and ONE control.undo puts
     back the exact note list from before (note.list equal, ids included);
  3. the same clip reads the same through roll.get_state (the piano roll's own read);
  4. scale_mode strict refuses an out-of-scale pitch, off writes it; a bad entry refuses the whole
     batch and changes nothing; a batch that changes nothing records no undo step;
  5. a project saved with only default notes carries no prob / rep attribute and reopens with
     chance 1 and ratchet 1; chance and ratchet round-trip through save and reopen.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-note-step.py <zene>
Exit 0 only when every check held.
"""

import os
import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

MAJOR_MASK = "101011010101"


def make_clip(session):
    track = session.result("track.add", {"type": "instrument", "name": "Melody"}).get("track")
    clip = session.result("clip.add", {"track": track, "position": 0}).get("clip")
    keys = (60, 64, 67, 61)  # C E G and an out-of-scale C#
    notes = [{"key": key, "position": i * 12, "length": 12, "velocity": 100} for i, key in enumerate(keys)]
    session.result("note.add_many", {"clip": clip, "notes": notes})
    session.result("scale.set", {"scale": "Major"})
    return clip


def listed(session, clip):
    return session.result("note.list", {"clip": clip}).get("notes") or []


def state(session, clip, **extra):
    args = {"clip": clip}
    args.update(extra)
    return session.result("note.step_get_state", args)


def by_key(st):
    return {n["key"]: n for n in st.get("notes") or []}


def is_default_note(note, velocity=100):
    """The old-note defaults: on the grid, chance 1, ratchet 1."""
    return note["velocity"] == velocity and note["chance"] == 1.0 and note["ratchet"] == 1 and note["offset"] == 0


def reads_every_field(st, notes):
    steps = [notes[k]["step"] for k in (60, 64, 67, 61)]
    return st.get("count") == 4 and st.get("step_ticks") == 12 and steps == [0, 1, 2, 3]


def scale_applied(st, notes):
    in_out = notes[60]["in_scale"] is True and notes[61]["in_scale"] is False
    return in_out and notes[67]["scale_degree"] == 5 and (st.get("scale") or {}).get("mask") == MAJOR_MASK


def columns_of(st):
    return [(c["step"], c["pitches"]) for c in st.get("columns") or []]


def folds_to_fewer_columns(coarse):
    steps = [n["step"] for n in sorted(coarse["notes"], key=lambda n: n["position"])]
    return steps == [0, 0, 1, 1] and len(coarse["columns"]) == 2


def roll_matches(roll, notes):
    return sorted(roll) == [60, 61, 64, 67] and all(roll[k].get("velocity") == notes[k]["velocity"] for k in roll)


def named_scale_applied(named):
    notes = by_key(named)
    return (named.get("scale") or {}).get("scale") == "Minor" and notes[60]["in_scale"] is True \
        and notes[61]["in_scale"] is False


def check_read(session, recorder, clip):
    st = state(session, clip)
    notes = by_key(st)
    recorder.check("step_get_state reports step, key, velocity, chance and ratchet of every note",
                   reads_every_field(st, notes), "state=%r" % st)
    recorder.check("notes made the ordinary way read the old defaults: chance 1, ratchet 1, on the grid",
                   all(is_default_note(n) for n in notes.values()), "notes=%r" % notes)
    recorder.check("the scale is applied to the read: C C# in/out of Major, degrees, mask",
                   scale_applied(st, notes), "scale=%r" % st.get("scale"))
    recorder.check("the occupied columns list each step's pitches",
                   columns_of(st) == [(0, [60]), (1, [64]), (2, [67]), (3, [61])], "columns=%r" % st.get("columns"))
    coarse = state(session, clip, step_ticks=24)
    recorder.check("a coarser grid folds the same notes into fewer columns",
                   folds_to_fewer_columns(coarse), "coarse=%r" % coarse.get("columns"))
    roll = {n.get("key"): n for n in session.result("roll.get_state", {"clip": clip}).get("notes") or []}
    recorder.check("roll.get_state (the piano roll's read) shows the same notes", roll_matches(roll, notes),
                   "roll=%r" % roll)
    named = state(session, clip, scale="Minor", root="A")
    recorder.check("a scale named on the call overrides the context for that read", named_scale_applied(named),
                   "scale=%r" % named.get("scale"))


def step_batch(n):
    return [{"note": n[60], "velocity": 150, "chance": 0.5, "ratchet": 3},
            {"step": 4, "key": 61, "velocity": 90},          # new cell, C# is out of Major: snapped down to C
            {"step": 2, "key": 72, "chance": 0.25},           # a second pitch in step 2
            {"note": n[61], "velocity": 70},                  # out-of-scale note stays editable
            {"note": n[64], "remove": True},
            {"note": n[67], "key": 66}]                       # a pitch change: F# is out of Major, snaps down to F


def fields_written(first, note_id):
    written = first.get("velocity") == 150 and first.get("chance") == 0.5 and first.get("ratchet") == 3
    return written and first.get("note") == note_id


def new_cell_ok(cells):
    if len(cells) != 1:
        return False
    cell = cells[0]
    return cell["key"] == 60 and cell["velocity"] == 90 and cell["length"] == 12 and cell["position"] == 48 \
        and cell["chance"] == 1.0 and cell["ratchet"] == 1


def column_ok(column, notes):
    return len(column) == 1 and column[0]["pitches"] == [65, 72] and notes[72]["chance"] == 0.25


def out_of_scale_edited(note):
    return note["key"] == 61 and note["velocity"] == 70 and note["in_scale"] is False


def piano_roll_agrees(roll, rows, n):
    edited = (roll.get(n[60]) or {}).get("velocity") == 150 and (rows.get(n[60]) or {}).get("probability") == 0.5
    return edited and n[64] not in roll and sorted(roll) == sorted(rows)


def check_cells(recorder, after, n):
    notes = by_key(after)
    by_id = {x["note"]: x for x in after["notes"]}
    recorder.check("velocity, chance and ratchet are written on the note",
                   fields_written(by_id.get(n[60]) or {}, n[60]), "first=%r" % by_id.get(n[60]))
    new_cells = [x for x in after["notes"] if x["step"] == 4]
    recorder.check("a new cell lands on the grid with the defaults, its pitch snapped into the scale",
                   new_cell_ok(new_cells), "new=%r" % new_cells)
    column = [c for c in after["columns"] if c["step"] == 2]
    recorder.check("a column holds several pitch cells", column_ok(column, notes), "column=%r" % column)
    recorder.check("an out-of-scale note keeps its key and takes the edit",
                   out_of_scale_edited(by_id[n[61]]), "c#=%r" % by_id.get(n[61]))
    recorder.check("a removed cell is gone", n[64] not in by_id, "notes=%r" % after["notes"])
    recorder.check("a changed pitch is snapped into the scale (F# -> F), the note keeping its id",
                   by_id[n[67]]["key"] == 65 and 66 not in notes, "keys=%r" % sorted(notes))


def check_applied(session, recorder, clip, n, reply):
    recorder.check("note.step_set answers what it did",
                   reply.get("edited") == 6 and reply.get("added") == 2 and reply.get("removed") == 1, "reply=%r" % reply)
    check_cells(recorder, state(session, clip), n)
    roll = {x.get("id"): x for x in session.result("roll.get_state", {"clip": clip}).get("notes") or []}
    rows = {x.get("note"): x for x in listed(session, clip)}
    recorder.check("roll.get_state and note.list (the piano roll's reads) show the edited clip",
                   piano_roll_agrees(roll, rows, n), "roll=%r rows=%r" % (roll, rows))


def check_edit_and_undo(session, recorder, clip):
    before_list = listed(session, clip)
    before_state = state(session, clip)
    n = {k: v["note"] for k, v in by_key(before_state).items()}
    reply = session.result("note.step_set", {"clip": clip, "edits": step_batch(n)})
    check_applied(session, recorder, clip, n, reply)
    session.result("control.undo")
    recorder.check("ONE control.undo restores the exact note list (note.list, ids and all)",
                   listed(session, clip) == before_list, "after undo=%r before=%r" % (listed(session, clip), before_list))
    recorder.check("and the step state reads as it did", state(session, clip) == before_state, "state differs after undo")
    return n


def check_scale_modes(session, recorder, clip):
    before = listed(session, clip)
    refused = session.typed_error("note.step_set", {"clip": clip, "scale_mode": "strict",
                                                   "edits": [{"step": 6, "key": 61}]})
    recorder.check("scale_mode strict refuses an out-of-scale pitch, typed, and writes nothing",
                   refused.get("kind") == "invalid_args" and listed(session, clip) == before, "error=%r" % refused)
    session.result("note.step_set", {"clip": clip, "scale_mode": "off", "edits": [{"step": 6, "key": 61}]})
    keys = sorted(n["key"] for n in listed(session, clip) if n["position"] == 72)
    recorder.check("scale_mode off writes the pitch as given", keys == [61], "keys=%r" % keys)
    session.result("control.undo")
    session.result("scale.set", {"scale": ""})
    session.result("note.step_set", {"clip": clip, "edits": [{"step": 6, "key": 61}]})
    keys = sorted(n["key"] for n in listed(session, clip) if n["position"] == 72)
    recorder.check("with no scale set nothing is snapped", keys == [61], "keys=%r" % keys)
    session.result("control.undo")
    session.result("scale.set", {"scale": "Major"})
    recorder.check("the scale checks leave the clip as it was", listed(session, clip) == before, "list changed")


def depth(session):
    return session.result("control.undo_depth")


def check_refusals(session, recorder, clip, ids):
    before = listed(session, clip)
    bad = [({"note": ids[60], "velocity": 50}, {"note": ids[67], "velocity": 999}, "velocity out of range"),
           ({"note": ids[60], "step": 1}, None, "note and step together"),
           ({"step": 1}, None, "a cell without a key"),
           ({"note": ids[60], "ratchet": 0}, None, "ratchet 0"),
           ({"note": ids[60], "ratchet": 17}, None, "ratchet 17"),
           ({"note": ids[60], "chance": 1.5}, None, "chance 1.5"),
           ({"note": 99, "step": 1, "key": 70}, None, "a non-string note id"),
           ({"note": ids[60], "pitch": 3}, None, "an unknown field"),
           ({"step": 1, "key": 70}, {"step": 1, "key": 70}, "the same cell twice"),
           ({"note": "note-99999", "velocity": 10}, None, "an unknown note")]
    kinds = []
    for first, second, label in bad:
        edits = [first] + ([second] if second else [])
        error = session.typed_error("note.step_set", {"clip": clip, "edits": edits})
        kinds.append((label, error.get("kind")))
    expected = {"an unknown note": "not_found"}
    recorder.check("every bad entry is refused, typed",
                   all(kind == expected.get(label, "invalid_args") for label, kind in kinds), "kinds=%r" % kinds)
    recorder.check("and the first, valid entry of a refused batch is not applied", listed(session, clip) == before,
                   "clip changed by a refused batch")
    empty = session.typed_error("note.step_set", {"clip": clip, "edits": []})
    recorder.check("an empty batch is refused", empty.get("kind") == "invalid_args", "error=%r" % empty)
    depth_before = depth(session)
    same = session.result("note.step_set", {"clip": clip, "edits": [{"note": ids[60], "velocity": 100, "chance": 1,
                                                                  "ratchet": 1},
                                                                 {"step": 9, "key": 70, "remove": True}]})
    recorder.check("a batch that changes nothing reports edited 0 and records no undo step",
                   same.get("edited") == 0 and depth(session) == depth_before and listed(session, clip) == before,
                   "reply=%r depth %r -> %r" % (same, depth_before, depth(session)))


def check_persistence(session, recorder, instance, clip, ids):
    project = os.path.join(instance.workspace, "note-step.mmp")
    session.result("project.save", {"path": project})
    with open(project, "rb") as handle:
        text = handle.read()
    recorder.check("a project holding only default notes writes no prob or rep attribute",
                   b" prob=" not in text and b" rep=" not in text, "the file carries a chance or ratchet attribute")
    opened = session.result("project.open", {"path": project})
    reread = state(session, clip)
    recorder.check("it reopens with chance 1 and ratchet 1 on every note",
                   opened.get("error_count") == 0 and reread.get("count") == 4
                   and all(n["chance"] == 1.0 and n["ratchet"] == 1 for n in reread["notes"]), "reread=%r" % reread)
    session.result("note.step_set", {"clip": clip, "edits": [{"note": ids[64], "chance": 0.5, "ratchet": 4, "velocity": 120}]})
    edited = state(session, clip)
    session.result("project.save", {"path": project})
    session.result("project.open", {"path": project})
    recorded = state(session, clip)
    recorder.check("chance, ratchet and velocity round-trip through save and reopen",
                   recorded.get("notes") == edited.get("notes"), "saved=%r reopened=%r" % (edited.get("notes"), recorded.get("notes")))


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
        clip = make_clip(session)
        check_read(session, recorder, clip)
        ids = check_edit_and_undo(session, recorder, clip)
        check_scale_modes(session, recorder, clip)
        check_refusals(session, recorder, clip, ids)
        check_persistence(session, recorder, instance, clip, ids)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("note step control-surface driver")
        return 1
    H.ok("note step control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
