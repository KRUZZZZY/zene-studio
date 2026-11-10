#!/usr/bin/env python3
"""Q050-294.2: the melodic step grid, driven over the control socket.

The step grid is a window over an ordinary MIDI clip: window.step_grid opens it and answers what
the grid SHOWS (its own cells, read by the panel), and its edits are note.step_set calls - the
command an agent sends. This driver starts the REAL `zene` binary headless and proves:

  1. window.step_grid opens a clip in the grid: the grid's cells are the clip's notes, the same
     ids, keys, steps, velocities and chances roll.get_state (the piano roll's read) and
     note.step_get_state report; opening it records no undo step;
  2. a note.step_set batch (velocity, chance and ratchet on a note, a new cell snapped into the
     scale, a scale-snapped pitch change, a removal) shows in the grid at once, and the grid,
     roll.get_state and note.step_get_state still agree;
  3. ONE control.undo restores the exact prior pattern - note.list equal, ids included - and the
     grid shows the cells it showed before;
  4. the grid's view settings: a coarser resolution regroups the same notes, scale rows only hides
     empty out-of-scale rows but keeps a row that holds a note, and "none" reads with no scale;
  5. an unknown clip or scale is refused, typed, and the grid keeps its clip.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-step-grid.py <zene>
Exit 0 only when every check held.
"""

import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def make_clip(session):
    track = session.result("track.add", {"type": "instrument", "name": "Grid"}).get("track")
    clip = session.result("clip.add", {"track": track, "position": 0}).get("clip")
    keys = (60, 64, 67, 61)  # C E G and an out-of-scale C#
    notes = [{"key": key, "position": i * 12, "length": 12, "velocity": 100} for i, key in enumerate(keys)]
    session.result("note.add_many", {"clip": clip, "notes": notes})
    session.result("scale.root_set", {"root": "C"})
    session.result("scale.set", {"scale": "Major"})
    return clip


def cells_by_id(grid):
    return {c["note"]: c for c in grid.get("cells") or []}


def agrees(grid, roll, step, step_ticks=12):
    """The grid's cells, the piano roll's notes and the step state are one set of notes."""
    cells = cells_by_id(grid)
    roll_notes = {n["id"]: n for n in roll.get("notes") or []}
    step_notes = {n["note"]: n for n in step.get("notes") or []}
    if not (set(cells) == set(roll_notes) == set(step_notes)) or not cells:
        return False
    return all(cell_agrees(cell, roll_notes[nid], step_notes[nid], step_ticks) for nid, cell in cells.items())


def cell_agrees(cell, roll_note, step_note, step_ticks):
    same_roll = (cell["key"], cell["step"], cell["velocity"]) == \
        (roll_note["key"], roll_note["position"] // step_ticks, roll_note["velocity"])
    return same_roll and all(cell[k] == step_note[k] for k in ("key", "step", "velocity", "chance", "ratchet"))


def reads(session, clip):
    return (session.result("roll.get_state", {"clip": clip}),
            session.result("note.step_get_state", {"clip": clip}))


def check_open(session, recorder, clip):
    depth = session.result("control.undo_depth").get("depth")
    grid = session.result("window.step_grid", {"clip": clip, "step_ticks": 12})
    recorder.check("window.step_grid opens the clip in the grid",
                   grid.get("shown") is True and grid.get("clip") == clip and len(grid.get("cells") or []) == 4,
                   "grid=%r" % grid)
    roll, step = reads(session, clip)
    recorder.check("the grid, roll.get_state and note.step_get_state show the same notes",
                   agrees(grid, roll, step), "grid=%r roll=%r" % (grid.get("cells"), roll.get("notes")))
    recorder.check("opening the grid records no undo step",
                   isinstance(depth, int) and session.result("control.undo_depth").get("depth") == depth,
                   "depth before %r" % depth)
    return grid


def edit_batch(ids):
    return [{"note": ids[60], "velocity": 150, "chance": 0.5, "ratchet": 3},
            {"step": 5, "key": 66},                    # F# is out of C major: a new cell snaps down to F
            {"note": ids[64], "key": 63},              # D# snaps down to D
            {"note": ids[67], "remove": True}]


def edited_as_asked(cells, ids):
    by_key_step = {(c["key"], c["step"]): c for c in cells.values()}
    first = cells.get(ids[60]) or {}
    return first.get("velocity") == 150 and first.get("chance") == 0.5 and first.get("ratchet") == 3 \
        and (65, 5) in by_key_step and (62, 1) in by_key_step and ids[67] not in cells and len(cells) == 4


def check_edit_and_undo(session, recorder, clip, opened):
    ids = {c["key"]: c["note"] for c in opened.get("cells") or []}
    before = session.result("note.list", {"clip": clip}).get("notes")
    reply = session.result("note.step_set", {"clip": clip, "edits": edit_batch(ids)})
    recorder.check("note.step_set takes the batch", reply.get("edited") == 4, "reply=%r" % reply)
    grid = session.result("window.step_grid", {"clip": clip})
    recorder.check("the grid shows the edit at once: velocity, chance, ratchet, a snapped new cell and pitch, a removal",
                   edited_as_asked(cells_by_id(grid), ids), "cells=%r" % grid.get("cells"))
    roll, step = reads(session, clip)
    recorder.check("after the edit the grid, the piano roll and the step state still agree",
                   agrees(grid, roll, step), "grid=%r roll=%r" % (grid.get("cells"), roll.get("notes")))
    session.result("control.undo")
    after = session.result("note.list", {"clip": clip}).get("notes")
    recorder.check("ONE control.undo restores the exact prior pattern (note.list equal, ids included)",
                   after == before, "before=%r after=%r" % (before, after))
    grid = session.result("window.step_grid", {"clip": clip})
    recorder.check("and the grid shows the cells it showed before the edit",
                   grid.get("cells") == opened.get("cells"), "now=%r was=%r" % (grid.get("cells"), opened.get("cells")))


def check_view(session, recorder, clip):
    coarse = session.result("window.step_grid", {"clip": clip, "step_ticks": 24})
    steps = sorted((c["key"], c["step"]) for c in coarse.get("cells") or [])
    recorder.check("a 1/8 grid regroups the same notes: two sixteenths per step",
                   steps == [(60, 0), (61, 1), (64, 0), (67, 1)] and coarse.get("step_ticks") == 24, "cells=%r" % steps)
    roll, step = reads(session, clip)
    step = session.result("note.step_get_state", {"clip": clip, "step_ticks": 24})
    recorder.check("the coarse grid still agrees with the piano roll", agrees(coarse, roll, step, 24), "coarse=%r" % coarse)
    only = session.result("window.step_grid", {"clip": clip, "step_ticks": 12, "scale": "Major", "root": 0,
                                               "scale_rows_only": True})
    rows = only.get("rows") or []
    recorder.check("scale rows only hides an empty out-of-scale row but keeps the row of the C# note",
                   63 not in rows and 61 in rows and 62 in rows, "rows=%r" % rows)
    none = session.result("window.step_grid", {"clip": clip, "scale": "none", "scale_rows_only": False})
    recorder.check("scale none reads the clip with no scale",
                   (none.get("scale") or {}).get("scale_set") is False and 63 in (none.get("rows") or []),
                   "scale=%r" % none.get("scale"))
    session.result("window.step_grid", {"clip": clip, "scale": ""})


def check_refusals(session, recorder, clip):
    missing = session.typed_error("window.step_grid", {"clip": "clip-99999"})
    recorder.check("an unknown clip is refused, typed", bool(missing.get("kind") or missing.get("code")), "error=%r" % missing)
    bad = session.typed_error("window.step_grid", {"clip": clip, "scale": "No Such Scale"})
    recorder.check("an unknown scale is refused, typed", bool(bad.get("kind") or bad.get("code")), "error=%r" % bad)
    kept = session.result("window.step_grid", {})
    recorder.check("the grid keeps its clip after the refusals", kept.get("clip") == clip, "grid=%r" % kept.get("clip"))


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
        opened = check_open(session, recorder, clip)
        check_edit_and_undo(session, recorder, clip, opened)
        check_view(session, recorder, clip)
        check_refusals(session, recorder, clip)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("step grid control-surface driver")
        return 1
    H.ok("step grid control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
