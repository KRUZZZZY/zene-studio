#!/usr/bin/env python3
"""The eleven `session.*` row drivers of the per-id proof table.

SPLIT OUT of `control-session-api-proof.py` on 2026-09-16 by the 0.3.0 fix-up
pass (Gate 7 file-length, Gate 4 CCN). Each `drive_*` function builds exactly
one row of the proof table: it issues the id, reads the effect back, and fills
the row's observed / A16 / verdict columns. The order they are called in is
the entry point's business (`drive_all` in control-session-api-proof.py); this
module only knows how to build a row.

The four functions the split rewrote (set_scene, set_slot, clear_slot, clear)
had their compound conditions moved into the named predicates below and the
A16 read-back into `a16_record` - same calls, same order, same strings, same
`rows.problems` entries. Every other body here is the byte-exact text the
one-file version carried.
"""

import os
import time

from session_api_proof_lib import (DRAIN_TIMEOUT, GRID_SCENES, LAUNCH_TIMEOUT, POPULATED,
                                    RESET_TIMEOUT, SEEK_FRACTION, grid, launch,
                                    send, state_of, transaction_for, wait_until)


# ---------------------------------------------------------------------------
# the row predicates, and the one read-back that is not a row's own reply
# ---------------------------------------------------------------------------


def a16_record(session, rows, command):
    """The live A16 record for `command`, with the row's A16 column filled from it.

    Returns the record (or None) so the caller keeps its own check and its own
    failure message - the two messages in this file are not the same string.
    """
    record = transaction_for(session, command)
    rows.row(command)["a16"] = "%s, reversible=%s (live)" % ((record or {}).get("class"),
                                                             (record or {}).get("reversible"))
    return record


def slot_matches(slot, track, scene):
    """The reply's slot carries what session.set_slot was asked for."""
    return (slot.get("type") == "midi" and slot.get("pattern") == 1 + track
            and slot.get("name") == "slot-%d-%d" % (track, scene)
            and slot.get("quantisation") == "global")


def scene_matches(scene):
    """The reply's scene carries the name and the tempo session.set_scene asked for."""
    return (scene.get("name") == "api-proof-scene" and scene.get("tempo_enabled") is True
            and abs(float(scene.get("tempo", 0.0)) - 128.5) < 1e-6)


def scene_was_read_back(readback):
    return (readback.get("name") == "api-proof-scene"
            and readback.get("tempo_enabled") is True)


def scene_observed(readback, record):
    return ("scene 0 name=%r tempo=%r tempo_enabled=%s; A16 record class=%s reversible=%s"
            % (readback.get("name"), readback.get("tempo"),
               readback.get("tempo_enabled"), (record or {}).get("class"),
               (record or {}).get("reversible")))


def slot_is_empty(slot, target):
    """The cleared cell answered as an EMPTY slot at the address it was sent."""
    return (slot.get("type") == "empty" and slot.get("track") == target[0]
            and slot.get("scene") == target[1])


def undo_restored_slot(undone, restored, before):
    """ONE control.undo restored the cleared cell and the clip count with it."""
    return (undone.get("undone") is True
            and undone.get("undone_command") == "session.clear_slot"
            and restored == before)


def grid_is_empty(after):
    return (int(after.get("tracks", -1)) == 0 and int(after.get("scenes", -1)) == 0
            and int(after.get("clips", -1)) == 0)


def undo_restored_grid(undone, restored, before):
    """ONE control.undo restored the whole grid the session.clear had emptied."""
    return (undone.get("undone") is True and undone.get("undone_command") == "session.clear"
            and int(restored.get("clips", -1)) == int(before.get("clips", -2))
            and int(restored.get("tracks", -1)) == int(before.get("tracks", -2)))


def cleared_project_writes_no_session_block(session, instance, rows):
    """Empty the grid again, save, and read the FILE back on the client side.

    Returns (ok, evidence): the boolean is part of the row's verdict, the text
    is the row's own sentence about the file.
    """
    session.result("session.clear")
    path = os.path.join(instance.tmp, "session-cleared.mmp")
    session.result("project.save", {"path": path})
    try:
        with open(path, "r", errors="replace") as handle:
            text = handle.read()
    except OSError as error:
        ok = rows.check("session.clear", False,
                        "could not read the saved project back: %s" % error)
        return ok, "the saved project could not be read back: %s" % error
    ok = rows.check("session.clear", "<session" not in text,
                    "a cleared grid still wrote a <session> block (%d bytes)" % len(text))
    return ok, "a cleared grid writes no <session> block (%d bytes saved)" % len(text)


# ---------------------------------------------------------------------------
# the rows, in drive order
# ---------------------------------------------------------------------------


def drive_get_state(session, rows):
    """session.get_state - the read the whole table is built on."""
    state = state_of(session)
    observed = ("grid=%dx%d clips=%d quantisation=%s scenes=%d launch.completed=%s"
                % (int(grid(state).get("tracks", -1)), int(grid(state).get("scenes", -1)),
                   int(grid(state).get("clips", -1)), state.get("quantisation"),
                   len(state.get("scenes") or []), launch(state).get("completed_launches")))
    row = rows.row("session.get_state")
    row["reply_ok"] = "error" not in state
    ok = rows.check("session.get_state", "error" not in state,
                    "the read failed: %r" % state)
    ok &= rows.check("session.get_state",
                     all(key in state for key in ("grid", "quantisation", "slots", "scenes", "launch")),
                     "the result is missing a declared field: %r" % sorted(state))
    ok &= rows.check("session.get_state", int(grid(state).get("tracks", -1)) == 0,
                     "a fresh instance should report an EMPTY grid, got %r" % grid(state))
    row["observed"] = observed
    row["a16"] = "not_mutating (read)"
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok


def drive_set_grid(session, rows):
    """session.set_grid - the dimensions change, and the A16 class is read back."""
    song_tracks = int(session.result("track.list").get("count", 0))
    ok = rows.check("session.set_grid", song_tracks >= 1,
                    "no song tracks to size the grid from (song_tracks=%d)" % song_tracks)
    columns = song_tracks + 1
    before = grid(state_of(session))
    reply = send(session, rows, "session.set_grid", {"tracks": columns, "scenes": GRID_SCENES})
    ok &= rows.check("session.set_grid",
                     int(grid(reply).get("tracks", -1)) == columns
                     and int(grid(reply).get("scenes", -1)) == GRID_SCENES,
                     "reply grid=%r (wanted %dx%d)" % (grid(reply), columns, GRID_SCENES))
    readback = grid(state_of(session))
    ok &= rows.check("session.set_grid",
                     int(readback.get("tracks", -1)) == columns
                     and int(readback.get("scenes", -1)) == GRID_SCENES,
                     "the read-back does not show the resize: %r" % readback)
    record = transaction_for(session, "session.set_grid")
    ok &= rows.check("session.set_grid", record is not None,
                     "no A16 record was recorded for the edit")
    row = rows.row("session.set_grid")
    row["reply_ok"] = True
    row["observed"] = "grid %dx%d -> %dx%d; A16 record class=%s reversible=%s" % (
        int(before.get("tracks", -1)), int(before.get("scenes", -1)), columns, GRID_SCENES,
        (record or {}).get("class"), (record or {}).get("reversible"))
    row["a16"] = "%s, reversible=%s (live)" % ((record or {}).get("class"),
                                               (record or {}).get("reversible"))
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return columns, song_tracks, ok


def drive_set_quantisation(session, rows):
    reply = send(session, rows, "session.set_quantisation", {"quantisation": "none"})
    ok = rows.check("session.set_quantisation", reply.get("quantisation") == "none",
                    "reply=%r" % reply)
    readback = state_of(session).get("quantisation")
    ok &= rows.check("session.set_quantisation", readback == "none",
                     "the read-back says %r after setting 'none'" % readback)
    # Set it to 'bar' for the launches below: every launch cell is 'global', and
    # global must resolve to the session default, which is what makes the launch
    # assertions a measurement of the resolution rather than of a constant.
    reply = send(session, rows, "session.set_quantisation", {"quantisation": "bar"})
    ok &= rows.check("session.set_quantisation", reply.get("quantisation") == "bar",
                     "reply=%r" % reply)
    record = transaction_for(session, "session.set_quantisation")
    ok &= rows.check("session.set_quantisation", record is not None,
                     "no A16 record was recorded for the edit")
    row = rows.row("session.set_quantisation")
    row["reply_ok"] = True
    row["observed"] = ("quantisation 'none' read back, then 'bar'; A16 record class=%s reversible=%s"
                       % ((record or {}).get("class"), (record or {}).get("reversible")))
    row["a16"] = "%s, reversible=%s (live)" % ((record or {}).get("class"),
                                               (record or {}).get("reversible"))
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok


def drive_set_scene(session, rows):
    reply = send(session, rows, "session.set_scene",
                 {"scene": 0, "name": "api-proof-scene", "tempo": 128.5})
    scene = reply.get("scene") or {}
    ok = rows.check("session.set_scene", scene_matches(scene),
                    "reply scene=%r" % scene)
    readback = (state_of(session).get("scenes") or [{}])[0]
    ok &= rows.check("session.set_scene", scene_was_read_back(readback),
                     "the read-back scene is %r" % readback)
    record = a16_record(session, rows, "session.set_scene")
    ok &= rows.check("session.set_scene", record is not None,
                     "no A16 record was recorded for the edit")
    row = rows.row("session.set_scene")
    row["reply_ok"] = True
    row["observed"] = (scene_observed(readback, record))
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok


def drive_set_slot(session, rows, columns, song_tracks):
    """Build the cells every later row addresses."""
    expected = list(POPULATED) + [(song_tracks, 0)]
    ok = True
    reply = {}
    for index, (track, scene) in enumerate(expected):
        reply = send(session, rows, "session.set_slot", {
            "track": track, "scene": scene, "type": "midi", "pattern": 1 + track,
            "name": "slot-%d-%d" % (track, scene),
            "mode": "trigger", "quantisation": "global"})
        slot = reply.get("slot") or {}
        ok &= rows.check("session.set_slot", slot_matches(slot, track, scene),
                         "cell (%d,%d) replied %r" % (track, scene, slot))
        if index == 0:
            record = a16_record(session, rows, "session.set_slot")
            ok &= rows.check("session.set_slot", record is not None,
                             "no A16 record was recorded for the edit")
    cells = sorted((int(slot.get("track", -1)), int(slot.get("scene", -1)))
                   for slot in state_of(session).get("slots", []))
    ok &= rows.check("session.set_slot", cells == sorted(expected),
                     "read-back cells=%r expected=%r" % (cells, sorted(expected)))
    row = rows.row("session.set_slot")
    row["reply_ok"] = True
    row["observed"] = ("%d cells built and read back, %r; the last reply slot=%r"
                       % (len(expected), cells, (reply.get("slot") or {})))
    if not row["a16"]:
        row["a16"] = "true_inverse (live record not found)"
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok


def drive_transport(session, rows):
    """The audio thread must be running its own clock before a launch means anything."""
    session.result("transport.play")
    ticks_per_bar = int(launch(state_of(session)).get("ticks_per_bar", 0) or 0)
    session.result("transport.seek", {"ticks": int(ticks_per_bar * SEEK_FRACTION)})
    first = int(launch(state_of(session)).get("position", 0))
    time.sleep(0.5)
    now = launch(state_of(session))
    ok = rows.check("session.launch_slot", first > 0 and int(now.get("position", 0)) > first,
                    "the play head did not advance on its own (%r -> %r)"
                    % (first, now.get("position")))
    ok &= rows.check("session.launch_slot", now.get("transport_running") is True,
                     "the transport is not running: %r" % now)
    ok &= rows.check("session.launch_slot",
                     ticks_per_bar > 0 and int(now.get("next_bar", 0)) % ticks_per_bar == 0,
                     "ticks_per_bar=%r next_bar=%r" % (ticks_per_bar, now.get("next_bar")))
    return ok, ticks_per_bar


def drive_launch_slot(session, rows):
    before = state_of(session)
    next_bar = int(launch(before).get("next_bar", 0))
    completed = int(launch(before).get("completed_launches", 0))
    reply = send(session, rows, "session.launch_slot", {"track": 0, "scene": 0})
    ok = rows.check("session.launch_slot",
                    reply.get("track") == 0 and reply.get("scene") == 0
                    and reply.get("mode") == "trigger" and reply.get("quantisation") == "bar",
                    "reply=%r" % reply)
    # 'global' on the cell resolved to the session default 'bar' - the resolution
    # is the measurement, not the constant.
    ok &= rows.check("session.launch_slot", int(reply.get("scheduled_tick", -1)) == next_bar,
                     "scheduled_tick=%r but the engine's own next_bar=%d"
                     % (reply.get("scheduled_tick"), next_bar))
    ok &= rows.check("session.launch_slot",
                     (reply.get("clip") or {}).get("name") == "slot-0-0",
                     "the reply's clip is %r" % reply.get("clip"))
    state, waited, held = wait_until(
        session, lambda current: int(launch(current).get("completed_launches", 0)) > completed,
        LAUNCH_TIMEOUT)
    now = launch(state)
    ok &= rows.check("session.launch_slot",
                     held and int(now.get("start_line", -1)) == next_bar
                     and int(now.get("start_line_starts", 0)) == 1,
                     "after %.1fs launch=%r (expected the start on line %d)" % (waited, now, next_bar))
    row = rows.row("session.launch_slot")
    row["reply_ok"] = True
    row["observed"] = ("scheduled_tick=%s (== engine next_bar %d), then start_line=%s "
                       "start_line_starts=%s completed_launches %d->%s after %.1fs"
                       % (reply.get("scheduled_tick"), next_bar, now.get("start_line"),
                          now.get("start_line_starts"), completed,
                          now.get("completed_launches"), waited))
    row["a16"] = "not_mutating (live: no record for it)"
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return next_bar, ok


def drive_launch_scene(session, rows):
    completed = int(launch(state_of(session)).get("completed_launches", 0))
    reply = send(session, rows, "session.launch_scene", {"scene": 0})
    ok = rows.check("session.launch_scene",
                    int(reply.get("clips", 0)) >= 2 and int(reply.get("scene", -1)) == 0,
                    "reply=%r" % reply)
    entries = reply.get("launched") or []
    ticks = {int(entry.get("scheduled_tick", -1)) for entry in entries}
    ok &= rows.check("session.launch_scene", reply.get("in_sync") is True and len(ticks) == 1,
                     "in_sync=%r scheduled ticks=%r (a scene launch means ONE line)"
                     % (reply.get("in_sync"), sorted(ticks)))
    sync_tick = int(reply.get("sync_tick", -2))
    # Bounded wait for the starts to LAND: the reply names the line the row was
    # scheduled for, and the engine records the start when its clock reaches it.
    # Asserting the read-back before that would be asserting a request, which is
    # the whole distinction this group's launch read-back exists to make.
    state, waited, held = wait_until(
        session,
        lambda current: int(launch(current).get("start_line", -1)) == sync_tick
        and int(launch(current).get("start_line_starts", 0)) >= len(entries),
        LAUNCH_TIMEOUT)
    readback = launch(state)
    ok &= rows.check("session.launch_scene",
                     held and int(readback.get("start_line", -1)) == sync_tick
                     and int(readback.get("start_line_starts", 0)) >= len(entries),
                     "after %.1fs the engine read-back is %r (wanted the start of %d clips on line %d)"
                     % (waited, readback, len(entries), sync_tick))
    ok &= rows.check("session.launch_scene",
                     int(readback.get("completed_launches", 0)) >= completed + len(entries),
                     "completed_launches %d -> %s, not +%d" % (completed,
                                                               readback.get("completed_launches"),
                                                               len(entries)))
    ok &= rows.check("session.launch_scene", int(readback.get("dropped_commands", -1)) == 0,
                     "the launch engine dropped commands: %r" % readback)
    row = rows.row("session.launch_scene")
    row["reply_ok"] = True
    row["observed"] = ("clips=%s all scheduled on sync_tick=%s in_sync=%s; the starts LANDED after "
                       "%.1fs: start_line=%s start_line_starts=%s completed %d->%s dropped=%s"
                       % (reply.get("clips"), reply.get("sync_tick"), reply.get("in_sync"), waited,
                          readback.get("start_line"), readback.get("start_line_starts"), completed,
                          readback.get("completed_launches"), readback.get("dropped_commands")))
    row["a16"] = "not_mutating (live: no record for it)"
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok


def drive_stop_slot(session, rows):
    before = launch(state_of(session))
    processed = int(before.get("processed_commands", -1))
    reply = send(session, rows, "session.stop_slot", {"track": 0, "scene": 0})
    ok = rows.check("session.stop_slot",
                    reply.get("stop_requested") is True and reply.get("track") == 0
                    and reply.get("scene") == 0,
                    "reply=%r" % reply)
    state, waited, held = wait_until(
        session, lambda current: int(launch(current).get("processed_commands", 0)) > processed,
        DRAIN_TIMEOUT)
    now = launch(state)
    ok &= rows.check("session.stop_slot", held,
                     "the audio thread never consumed the stop (processed_commands stayed %d)" % processed)
    ok &= rows.check("session.stop_slot", int(now.get("dropped_commands", -1)) == 0,
                     "the stop dropped a command: %r" % now)
    row = rows.row("session.stop_slot")
    row["reply_ok"] = True
    row["observed"] = ("stop_requested=true; the audio thread consumed it: processed_commands "
                       "%d -> %s after %.1fs, dropped=%s"
                       % (processed, now.get("processed_commands"), waited,
                          now.get("dropped_commands")))
    row["a16"] = "not_mutating (live: no record for it)"
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok


def drive_stop_all(session, rows):
    clips_before = int(grid(state_of(session)).get("clips", -1))
    completed = int(launch(state_of(session)).get("completed_launches", 0))
    reply = send(session, rows, "session.stop_all")
    ok = rows.check("session.stop_all", reply.get("reset_requested") is True, "reply=%r" % reply)
    state, waited, held = wait_until(
        session, lambda current: int(launch(current).get("completed_launches", 0)) == 0,
        RESET_TIMEOUT)
    ok &= rows.check("session.stop_all", held,
                     "the reset was never applied (completed_launches stayed %d)" % completed)
    clips_after = int(grid(state_of(session)).get("clips", -1))
    ok &= rows.check("session.stop_all", clips_after == clips_before,
                     "the reset touched the MODEL: clips %d -> %d" % (clips_before, clips_after))
    row = rows.row("session.stop_all")
    row["reply_ok"] = True
    row["observed"] = ("reset_requested=true; completed_launches %d -> %s after %.1fs, and the grid "
                       "model was left alone (clips=%d)" % (completed,
                                                            launch(state).get("completed_launches"),
                                                            waited, clips_after))
    row["a16"] = "not_mutating (live: no record for it)"
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok


def drive_clear_slot(session, rows):
    before = int(grid(state_of(session)).get("clips", -1))
    target = (1, 1)
    reply = send(session, rows, "session.clear_slot", {"track": target[0], "scene": target[1]})
    slot = reply.get("slot") or {}
    ok = rows.check("session.clear_slot", slot_is_empty(slot, target),
                    "reply slot=%r" % slot)
    after = int(grid(state_of(session)).get("clips", -1))
    ok &= rows.check("session.clear_slot", after == before - 1,
                     "clips %d -> %d, not one fewer" % (before, after))
    record = a16_record(session, rows, "session.clear_slot")
    ok &= rows.check("session.clear_slot", record is not None, "no A16 record for the edit")
    undone = session.result("control.undo")
    restored = int(grid(state_of(session)).get("clips", -1))
    ok &= rows.check("session.clear_slot", undo_restored_slot(undone, restored, before),
                     "one control.undo did not restore the cell: undo=%r clips=%d (wanted %d)"
                     % (undone, restored, before))
    row = rows.row("session.clear_slot")
    row["reply_ok"] = True
    row["observed"] = ("cell %r emptied (clips %d -> %d); A16 class=%s reversible=%s; one control.undo "
                       "restored it (clips back to %d, undone_command=%r)"
                       % (target, before, after, (record or {}).get("class"),
                          (record or {}).get("reversible"), restored,
                          undone.get("undone_command")))
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok


def drive_clear(session, rows, instance):
    before = grid(state_of(session))
    reply = send(session, rows, "session.clear")
    after = grid(reply)
    ok = rows.check("session.clear", grid_is_empty(after), "reply grid=%r" % after)
    readback = grid(state_of(session))
    ok &= rows.check("session.clear", int(readback.get("clips", -1)) == 0,
                     "the read-back still reports clips: %r" % readback)
    record = a16_record(session, rows, "session.clear")
    ok &= rows.check("session.clear", record is not None, "no A16 record for the edit")
    undone = session.result("control.undo")
    restored = grid(state_of(session))
    ok &= rows.check("session.clear", undo_restored_grid(undone, restored, before),
                     "one control.undo did not restore the whole grid: undo=%r grid=%r (wanted %r)"
                     % (undone, restored, before))
    # The engine's word is not evidence about the FILE: empty the grid again,
    # save and read the file back on the client side.
    file_ok, file_evidence = cleared_project_writes_no_session_block(session, instance, rows)
    ok &= file_ok
    row = rows.row("session.clear")
    row["reply_ok"] = True
    row["observed"] = ("grid %dx%d/%d clips -> %dx%d/0; A16 class=%s reversible=%s; one control.undo "
                       "restored %d clips; %s"
                       % (int(before.get("tracks", -1)), int(before.get("scenes", -1)),
                          int(before.get("clips", -1)), int(after.get("tracks", -1)),
                          int(after.get("scenes", -1)), (record or {}).get("class"),
                          (record or {}).get("reversible"), int(before.get("clips", -1)), file_evidence))
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok

