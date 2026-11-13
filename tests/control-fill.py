#!/usr/bin/env python3
"""Q050-294.3: seeded step fills, driven over the control socket.

note.step_fill writes notes into a MIDI clip's step grid from a seeded shape or a seeded random
fill, in the active scale. This driver starts the REAL `zene` binary headless and proves:

  1. a seeded shape fill places C-major notes in 48..72, reports the ids it placed (all in
     note.list), records ONE undo step, and ONE control.undo restores the exact prior note list
     (ids included) and the grid's cells; a second identical fill gives the same note list;
  2. after a fill, roll.get_state and note.step_get_state list the same notes by id, key and
     position as note.list, and window.step_grid's cells have those ids with the same key and step;
  3. on an eight-column clip, a seeded random fill (density 25, seed 3) keeps notes and gives the
     same non-empty note list twice with an undo between; a density-100 random fill leaves a note in
     every one of the eight step columns;
  4. refusals - an unknown clip, seed -1, seed 1.5, an unknown kind, a low/high range of 60/61,
     and a fill with no scale - are typed, and each leaves note.list and undo_depth unchanged.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-fill.py <zene>
Exit 0 only when every check held.
"""

import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

C_MAJOR = {0, 2, 4, 5, 7, 9, 11}


def make_clip(session):
    track = session.result("track.add", {"type": "instrument", "name": "Fill"}).get("track")
    clip = session.result("clip.add", {"track": track, "position": 0}).get("clip")
    keys = (60, 64, 67, 61)  # C E G and an out-of-scale C#
    notes = [{"key": key, "position": i * 12, "length": 12, "velocity": 100} for i, key in enumerate(keys)]
    session.result("note.add_many", {"clip": clip, "notes": notes})
    session.result("scale.root_set", {"root": "C"})
    session.result("scale.set", {"scale": "Major"})
    return clip


def make_columns_clip(session, count=8, key=60):
    """A fresh clip with one note in each of `count` step columns, so a random fill has columns to keep."""
    track = session.result("track.add", {"type": "instrument", "name": "FillRandom"}).get("track")
    clip = session.result("clip.add", {"track": track, "position": 0}).get("clip")
    notes = [{"key": key, "position": i * 12, "length": 12, "velocity": 100} for i in range(count)]
    session.result("note.add_many", {"clip": clip, "notes": notes})
    return clip


def listed(session, clip):
    return session.result("note.list", {"clip": clip}).get("notes") or []


def depth(session):
    return session.result("control.undo_depth").get("depth")


def ids_of(notes):
    """note.list names a note by its `note` id; roll.get_state names it by `id`."""
    return {n["note"] if "note" in n else n["id"] for n in notes}


def grid_cells(session, clip):
    return session.result("window.step_grid", {"clip": clip, "step_ticks": 12}).get("cells") or []


def fill_args(clip, **extra):
    args = {"clip": clip, "kind": "shape", "contour": "arch", "seed": 7}
    args.update(extra)
    return args


def reply_ids(reply):
    return [(n.get("note") or n.get("id")) if isinstance(n, dict) else n for n in reply.get("notes") or []]


def same_ids(cells, roll_notes, step_notes, by_id):
    """The grid's cells, the roll's notes and the step state name the same note ids as the note list."""
    return {c["note"] for c in cells} == set(by_id) == {n["id"] for n in roll_notes} == {n["note"] for n in step_notes}


def row_agrees(cell, roll, step, note):
    """One note's grid cell, roll row and step row all carry the note list's key and position."""
    if not (cell["key"] == note["key"] and cell["step"] == note["position"] // 12):
        return False
    if not (roll["key"] == note["key"] and roll["position"] == note["position"]):
        return False
    return step["key"] == note["key"] and step["step"] == note["position"] // 12


def agrees(cells, roll_notes, step_notes, by_id):
    """The grid's cells, the roll's notes and the step state are the note list, id by id."""
    if not same_ids(cells, roll_notes, step_notes, by_id):
        return False
    cell_map = {c["note"]: c for c in cells}
    roll_map = {n["id"]: n for n in roll_notes}
    step_map = {n["note"]: n for n in step_notes}
    return all(row_agrees(cell_map[nid], roll_map[nid], step_map[nid], note) for nid, note in by_id.items())


def check_fill_and_undo(session, recorder, clip):
    before = listed(session, clip)
    d0 = depth(session)
    before_cells = grid_cells(session, clip)
    reply = session.result("note.step_fill", fill_args(clip))
    keys = reply.get("keys") or []
    recorder.check("the seeded arch fill places notes", (reply.get("filled") or 0) >= 1, "reply=%r" % reply)
    recorder.check("the fill reports one key per note it placed", len(keys) == reply.get("filled"),
                   "filled=%r keys=%r" % (reply.get("filled"), keys))
    recorder.check("every fill key is a C-major pitch in 48..72",
                   bool(keys) and all(48 <= k <= 72 and k % 12 in C_MAJOR for k in keys), "keys=%r" % keys)
    after = listed(session, clip)
    ids = reply_ids(reply)
    recorder.check("every note the fill reports is in note.list",
                   bool(ids) and set(ids) <= ids_of(after), "ids=%r list=%r" % (ids, after))
    recorder.check("the fill records one undo step", depth(session) == d0 + 1,
                   "depth=%r d0=%r" % (depth(session), d0))
    session.result("control.undo")
    undone = listed(session, clip)
    recorder.check("ONE control.undo restores the exact prior note list (ids included)",
                   undone == before, "before=%r after=%r" % (before, undone))
    recorder.check("and the grid shows the cells it showed before the fill",
                   grid_cells(session, clip) == before_cells, "now=%r was=%r" % (grid_cells(session, clip), before_cells))
    recorder.check("and the undo depth is back where it was", depth(session) == d0,
                   "depth=%r d0=%r" % (depth(session), d0))
    session.result("note.step_fill", fill_args(clip))
    again = listed(session, clip)
    recorder.check("a second identical seeded fill gives the same note list (same ids)",
                   again == after, "first=%r second=%r" % (after, again))


def reads_agree(by_id, roll, step):
    """The roll and the step state list the note list's notes, by id, key and position."""
    return (ids_of(roll) == set(by_id) == {n["note"] for n in step}
            and all(by_id[n["id"]]["key"] == n["key"] and by_id[n["id"]]["position"] == n["position"] for n in roll)
            and all(by_id[n["note"]]["key"] == n["key"] and by_id[n["note"]]["position"] // 12 == n["step"] for n in step))


def check_reads_agree(session, recorder, clip):
    notes = listed(session, clip)
    by_id = {n["note"]: n for n in notes}
    roll = session.result("roll.get_state", {"clip": clip}).get("notes") or []
    step = session.result("note.step_get_state", {"clip": clip}).get("notes") or []
    recorder.check("roll.get_state and note.step_get_state list the same notes as note.list, by id, key and position",
                   reads_agree(by_id, roll, step),
                   "list=%r roll=%r step=%r" % (notes, roll, step))


def check_grid_shows_fill(session, recorder, clip):
    notes = listed(session, clip)
    by_id = {n["note"]: n for n in notes}
    roll = session.result("roll.get_state", {"clip": clip}).get("notes") or []
    step = session.result("note.step_get_state", {"clip": clip}).get("notes") or []
    cells = grid_cells(session, clip)
    recorder.check("window.step_grid shows the filled notes: same ids, keys and steps as note.list",
                   bool(notes) and agrees(cells, roll, step, by_id),
                   "cells=%r list=%r" % (cells, notes))


RANDOM_SEED = 3  # density-25 seed 3 keeps at least one of the eight columns (checked below, not assumed)


def check_random(session, recorder, _clip):
    clip = make_columns_clip(session)  # its own eight-column clip, so the repeat compares non-empty lists
    start = listed(session, clip)
    d0 = depth(session)
    random_args = {"kind": "random", "density": 25, "seed": RANDOM_SEED}
    reply = session.result("note.step_fill", fill_args(clip, **random_args))
    first = listed(session, clip)
    recorder.check("the density-25 seed-3 random fill keeps at least one note before the repeat compares it",
                   len(first) > 0, "seed=%r first=%r" % (RANDOM_SEED, first))
    recorder.check("the random fill's filled count equals the distinct step columns it left",
                   reply.get("filled") == len({n["position"] // 12 for n in first}),
                   "filled=%r columns=%r" % (reply.get("filled"), sorted({n["position"] // 12 for n in first})))
    session.result("control.undo")
    recorder.check("one undo returns the random fill's start pattern", listed(session, clip) == start and depth(session) == d0,
                   "start=%r now=%r" % (start, listed(session, clip)))
    session.result("note.step_fill", fill_args(clip, **random_args))
    second = listed(session, clip)
    recorder.check("a seeded random fill (density 25, seed %d) gives the same non-empty note list twice" % RANDOM_SEED,
                   bool(first) and second == first, "first=%r second=%r" % (first, second))
    session.result("control.undo")
    # The density-100 fill runs on the eight-column start pattern, which still holds one note per column.
    reply = session.result("note.step_fill", fill_args(clip, kind="random", density=100, seed=RANDOM_SEED))
    step_ticks = reply.get("step_ticks") or 12
    held = {n["position"] // step_ticks for n in start}
    filled = {n["position"] // step_ticks for n in listed(session, clip)}
    session.result("control.undo")
    recorder.check("a density-100 random fill leaves a note in every one of the eight columns that held one before",
                   len(held) == 8 and held <= filled, "held=%r filled=%r" % (sorted(held), sorted(filled)))


def check_refusals(session, recorder, clip):
    refusals = [
        ("an unknown clip", fill_args("clip-999999")),
        ("seed -1", fill_args(clip, seed=-1)),
        ("seed 1.5", fill_args(clip, seed=1.5)),
        ('kind "shap"', fill_args(clip, kind="shap")),
        ("a low 60 / high 61 range", fill_args(clip, kind="random", density=25, seed=3, low=60, high=61)),
    ]
    for label, args in refusals:
        before, d0 = listed(session, clip), depth(session)
        err = session.typed_error("note.step_fill", args)
        recorder.check("refused, typed, and no change: %s" % label,
                       bool(err.get("kind") or err.get("code")) and listed(session, clip) == before and depth(session) == d0,
                       "error=%r" % err)
    session.result("scale.set", {"scale": ""})
    before, d0 = listed(session, clip), depth(session)
    err = session.typed_error("note.step_fill", fill_args(clip))
    recorder.check("a fill with no scale is Refused, and no change",
                   str(err.get("kind") or err.get("code") or "").lower() == "refused"
                   and listed(session, clip) == before and depth(session) == d0, "error=%r" % err)
    session.result("scale.set", {"scale": "Major"})


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
        check_fill_and_undo(session, recorder, clip)
        check_reads_agree(session, recorder, clip)
        check_grid_shows_fill(session, recorder, clip)
        check_random(session, recorder, clip)
        check_refusals(session, recorder, clip)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("seeded step fill control-surface driver")
        return 1
    H.ok("seeded step fill control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
