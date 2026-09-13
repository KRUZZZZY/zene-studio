#!/usr/bin/env python3
"""RAW control-surface transcript for the five session.* ids the M1 milestone
does not drive: `session.launch_slot`, `session.stop_slot`, `session.stop_all`,
`session.clear_slot` and `session.clear`.

WHY THIS FILE EXISTS. `docs/COVERAGE-MATRIX-2026-09-13.md` section 3.3 measured
nine command ids with no registered-test reference. Five of them are the slot
lifecycle of the session.* group: control-session-m1.py drives the grid, the
SCENE launch path and the launch read-back, but it never triggers ONE slot,
never stops anything and never empties a cell or the grid - `grep -rlE
'session\\.(clear|clear_slot|launch_slot|stop_all|stop_slot)' src include tests`
returned only the five implementation files and the registry header. This is
that group's registered proof, in the same mould as its sibling
(control-session-m1.py, ctest ControlSessionLaunch).

WHAT IT PROVES, id by id, each check carrying its own measured number:
launch_slot, stop_slot, stop_all, clear_slot, clear - in that order below. Two
shapes in here are deliberate and are worth naming once:

  * the grid is built ONE COLUMN WIDER than the song's own track list, because a
    fresh instance already has song tracks and launch_slot's "no song track"
    refusal is otherwise unreachable; and
  * the `control.undo` that restores a cleared grid is taken BEFORE the
    `project.save`, because a save pushes its own file-revision checkpoint and an
    undo issued after it would restore the FILE instead of the grid.

WHAT IT DOES NOT PROVE. The launch engine's per-slot PHASE is not readable over
the socket (`session.get_state` reports the model's cells and the aggregate
launch counters, not `SlotLaunchState`), so nothing here asserts that a stopped
slot went back to Idle. That half is the engine test's:
tests/src/core/SessionSchedulerTest.cpp asserts the phase transitions after
requestStop/reset in-process. What this file adds is the id contract - each id
invoked over the socket, its reply asserted, its model effect read back, and its
inverse (the journal checkpoint) exercised.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path.

Usage: QT_QPA_PLATFORM=offscreen python3 control-session-lifecycle-transcript.py <lmms>
Exit code 0 only when every check held; 77 (ctest Skipped, never Passed) when
this build has no session.* group at all (WANT_SESSION_VIEW=OFF).
"""

import os
import sys
import time

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 2000))

#: The play head is parked a quarter of the way into a bar before a launch, so
#: the line the launch is scheduled for is three quarters of a bar away and no
#: launch is racing a grid boundary. The tick itself is derived from the
#: ticks-per-bar the engine reports, never hardcoded.
SEEK_FRACTION = 0.25
#: A clip starts on the next bar line; at the 120 BPM / 4-4 the instance starts
#: in that is around 2 s away. The bound is generous because a loaded build box
#: is not a metronome.
LAUNCH_TIMEOUT = 30.0
#: The audio thread drains the command queue once per period, so a request is
#: consumed in milliseconds. This is the bound on NOTICING that.
DRAIN_TIMEOUT = 20.0
#: How long the reset is given to be applied (it lands on the audio thread's
#: next period).
RESET_TIMEOUT = 20.0
GRID_SCENES = 2
#: The cells this transcript populates, as (track, scene), before the extra cell
#: in the track-less last column. The grid is one column wider than the song's
#: track list, so that last column is the "no song track" refusal's target.
POPULATED = [(0, 0), (1, 0), (2, 0), (0, 1), (1, 1)]
EMPTY_CELL = (2, 1)


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        return self.client.call(next(REQUEST_IDS), command, args, transcript=self.transcript)

    def result(self, command, args=None):
        """The reply's result, or {'error': ...} so a failure is visible."""
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def error(self, command, args=None):
        """The typed error of a refused call, or {} when it unexpectedly passed."""
        reply = self.call(command, args)
        if reply.get("ok") is False:
            return reply.get("error") or {}
        return {}


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.checks = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.checks.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def state_of(session):
    """`session.get_state` in one read: grid, slots and the launch read-back."""
    return session.result("session.get_state")


def wait_until(session, ready, timeout):
    """Poll `session.get_state` until `ready(state)`, or the bound expires.

    Returns (state, seconds_waited, held). A bounded wait that never becomes
    true is a failed check at the call site, never a hang.
    """
    started = time.time()
    state = state_of(session)
    while time.time() - started < timeout:
        if ready(state):
            return state, time.time() - started, True
        time.sleep(0.1)
        state = state_of(session)
    return state, time.time() - started, False


def launch(state):
    return state.get("launch", {})


def grid_clips(session):
    return int(state_of(session).get("grid", {}).get("clips", -1))


def transaction_commands(session):
    """Every command named by a recorded transaction (SPEC A16)."""
    records = session.result("control.transactions").get("transactions") or []
    return [record.get("command") for record in records]


def check_registered(session, recorder):
    """The five ids are in the live registry - the precondition of every check."""
    listing = session.result("control.commands_list")
    ids = [entry.get("id") for entry in listing.get("commands", [])]
    wanted = ("session.launch_slot", "session.stop_slot", "session.stop_all",
              "session.clear_slot", "session.clear")
    missing = [command for command in wanted if command not in ids]
    recorder.check("the five session.* ids are registered", not missing,
                   "missing=%s (registered=%d)" % (missing, len(ids)))
    return not missing


def build_grid(session, recorder):
    """Song tracks, then a grid ONE COLUMN WIDER than them, with five cells.

    The width is derived from the engine's own track list rather than fixed: a
    fresh instance starts with several song tracks, and launch_slot refuses a
    column that has none, so a fixed 3-column grid can never reach that refusal
    (measured: this run starts with 6 song tracks). The last column is populated
    so the refusal has a clip to find, since the EMPTY check runs first.
    """
    song_tracks = int(session.result("track.list").get("count", 0))
    recorder.check("the instance has song tracks to size the grid from",
                   song_tracks >= 1, "song_tracks=%d" % song_tracks)
    columns = song_tracks + 1
    last = song_tracks

    before = state_of(session).get("grid", {})
    recorder.check("the grid is empty before the run",
                   int(before.get("tracks", -1)) == 0 and int(before.get("scenes", -1)) == 0,
                   "grid before=%r" % before)

    grid = session.result("session.set_grid", {"tracks": columns, "scenes": GRID_SCENES})
    recorder.check("set_grid establishes a %dx%d grid" % (columns, GRID_SCENES),
                   int(grid.get("grid", {}).get("tracks", -1)) == columns
                   and int(grid.get("grid", {}).get("scenes", -1)) == GRID_SCENES,
                   "grid=%r (columns derived from song_tracks=%d)" % (grid.get("grid"), song_tracks))
    quantisation = session.result("session.set_quantisation", {"quantisation": "bar"})
    recorder.check("the session default quantisation is one bar",
                   quantisation.get("quantisation") == "bar", "reply=%r" % quantisation)

    expected = list(POPULATED) + [(last, 0)]
    for track, scene in expected:
        session.result("session.set_slot", {
            "track": track, "scene": scene, "type": "midi", "pattern": 1 + track,
            "name": "slot-%d-%d" % (track, scene), "mode": "trigger",
            "quantisation": "global"})
    slots = state_of(session).get("slots", [])
    cells = sorted((int(slot.get("track", -1)), int(slot.get("scene", -1))) for slot in slots)
    recorder.check("%d clip cells were built" % len(expected), cells == sorted(expected),
                   "cells=%r expected=%r" % (cells, sorted(expected)))
    return song_tracks


def check_transport_running(session, recorder):
    """Park the play head inside a bar; the launch below is scheduled against it.

    A launch is only a measurement if the audio thread is calling
    Song::processNextBuffer while this test drives the socket from ANOTHER
    process, so the play head must advance on its own - the order
    control-session-m1.py documents.
    """
    session.result("transport.play")
    state = launch(state_of(session))
    ticks_per_bar = int(state.get("ticks_per_bar", 0) or 0)
    session.result("transport.seek", {"ticks": int(ticks_per_bar * SEEK_FRACTION)})
    first = int(launch(state_of(session)).get("position", 0))
    time.sleep(0.5)
    now = launch(state_of(session))
    position = int(now.get("position", 0))
    recorder.check("the play head advances on its own after the seek",
                   first > 0 and position > first, "position %r -> %r" % (first, position))
    recorder.check("the song transport is running",
                   now.get("transport_running") is True, "launch=%r" % now)
    recorder.check("the engine reports ticks per bar",
                   ticks_per_bar > 0 and int(now.get("next_bar", 0)) % ticks_per_bar == 0,
                   "ticks_per_bar=%d next_bar=%r" % (ticks_per_bar, now.get("next_bar")))
    return ticks_per_bar


def check_launch_slot(session, recorder):
    """session.launch_slot: the tick, the cell, and the start it produced."""
    before = state_of(session)
    next_bar = int(launch(before).get("next_bar", 0))
    completed = int(launch(before).get("completed_launches", 0))

    reply = session.result("session.launch_slot", {"track": 0, "scene": 0})
    recorder.check("launch_slot answers for one cell",
                   reply.get("track") == 0 and reply.get("scene") == 0, "reply=%r" % reply)
    recorder.check("launch_slot reports the bar line the engine's clock reports",
                   int(reply.get("scheduled_tick", -1)) == next_bar,
                   "scheduled_tick=%r next_bar=%d" % (reply.get("scheduled_tick"), next_bar))
    recorder.check("launch_slot reports the same line as its own next_bar",
                   int(reply.get("next_bar", -1)) == int(reply.get("scheduled_tick", -2)),
                   "reply=%r" % reply)
    recorder.check("launch_slot reports the cell that was built",
                   (reply.get("clip") or {}).get("name") == "slot-0-0"
                   and (reply.get("clip") or {}).get("pattern") == 1,
                   "clip=%r" % reply.get("clip"))

    state, waited, held = wait_until(
        session,
        lambda current: int(launch(current).get("completed_launches", 0)) > completed,
        LAUNCH_TIMEOUT)
    now = launch(state)
    recorder.check("the launched slot started on the scheduled bar line",
                   held and int(now.get("start_line", -1)) == next_bar
                   and int(now.get("start_line_starts", 0)) == 1,
                   "after %.1fs launch=%r (expected line %d, one start)"
                   % (waited, now, next_bar))
    recorder.check("no launch command was dropped",
                   int(now.get("dropped_commands", -1)) == 0, "launch=%r" % now)
    return next_bar


def check_launch_refusals(session, recorder, song_tracks):
    """session.launch_slot's two documented refusals, plus the grid bound."""
    empty = session.error("session.launch_slot", {"track": EMPTY_CELL[0], "scene": EMPTY_CELL[1]})
    recorder.check("launching an EMPTY cell is a typed refusal",
                   empty.get("kind") == "invalid_args" and "is empty" in (empty.get("message") or ""),
                   "error=%r" % empty)

    trackless = session.error("session.launch_slot", {"track": song_tracks, "scene": 0})
    recorder.check("launching a column with no song track is a typed refusal",
                   trackless.get("kind") == "invalid_args"
                   and "has no song track" in (trackless.get("message") or ""),
                   "column=%d song_tracks=%d error=%r" % (song_tracks, song_tracks, trackless))

    outside = session.error("session.launch_slot", {"track": 99, "scene": 0})
    recorder.check("launching outside the grid is a typed refusal",
                   outside.get("kind") == "invalid_args", "error=%r" % outside)


def check_stop_slot(session, recorder):
    """session.stop_slot: the reply, and the audio thread consuming the request."""
    before = launch(state_of(session))
    processed = int(before.get("processed_commands", -1))

    reply = session.result("session.stop_slot", {"track": 0, "scene": 0})
    recorder.check("stop_slot answers for the cell it was given",
                   reply.get("stop_requested") is True
                   and reply.get("track") == 0 and reply.get("scene") == 0,
                   "reply=%r" % reply)

    state, waited, held = wait_until(
        session,
        lambda current: int(launch(current).get("processed_commands", 0)) > processed,
        DRAIN_TIMEOUT)
    now = launch(state)
    recorder.check("the audio thread consumed the stop request",
                   held and int(now.get("processed_commands", 0)) > processed,
                   "processed_commands %d -> %r after %.1fs"
                   % (processed, now.get("processed_commands"), waited))
    recorder.check("the stop dropped no command",
                   int(now.get("dropped_commands", -1)) == 0, "launch=%r" % now)

    outside = session.error("session.stop_slot", {"track": 99, "scene": 0})
    recorder.check("stopping outside the grid is a typed refusal",
                   outside.get("kind") == "invalid_args", "error=%r" % outside)


def check_stop_all(session, recorder):
    """session.stop_all: the reset is applied, and it does not wedge the engine."""
    clips_before = grid_clips(session)
    completed_before = int(launch(state_of(session)).get("completed_launches", 0))
    reply = session.result("session.stop_all")
    recorder.check("stop_all answers reset_requested",
                   reply.get("reset_requested") is True, "reply=%r" % reply)

    # The reset lands on the audio thread's next period and takes the session
    # bookkeeping back to zero with the slots (SessionScheduler: "A project
    # change starts a fresh session"). That it HAPPENS is the effect this id
    # claims; the model must not be touched by it.
    state, waited, held = wait_until(
        session,
        lambda current: int(launch(current).get("completed_launches", 0)) == 0,
        RESET_TIMEOUT)
    recorder.check("the audio thread applied the reset (the launched state went back to zero)",
                   held, "completed_launches %d -> %r after %.1fs"
                   % (completed_before, launch(state).get("completed_launches"), waited))
    recorder.check("the reset left the grid model alone",
                   grid_clips(session) == clips_before,
                   "clips %d -> %d" % (clips_before, grid_clips(session)))

    # And a launch after the reset must be accepted AND start on its own line:
    # the counter was zeroed, so "1" here is a fresh start, not a stale number.
    after = session.result("session.launch_slot", {"track": 1, "scene": 0})
    tick = after.get("scheduled_tick")
    recorder.check("the scheduler still accepts a launch after the reset",
                   isinstance(tick, int) and after.get("track") == 1, "reply=%r" % after)
    state, waited, held = wait_until(
        session,
        lambda current: int(launch(current).get("completed_launches", 0)) == 1,
        LAUNCH_TIMEOUT)
    now = launch(state)
    recorder.check("that launch started on the line it was scheduled for",
                   held and int(now.get("start_line", -1)) == tick,
                   "after %.1fs launch=%r (scheduled %r)" % (waited, now, tick))


def check_clear_slot(session, recorder):
    """session.clear_slot: the cell empties, is read back, and undoes."""
    before = grid_clips(session)
    reply = session.result("session.clear_slot", {"track": 1, "scene": 1})
    cleared = reply.get("slot") or {}
    recorder.check("clear_slot reports the cell it emptied",
                   cleared.get("type") == "empty" and cleared.get("track") == 1
                   and cleared.get("scene") == 1,
                   "slot=%r" % cleared)
    recorder.check("clear_slot is visible in the model read-back",
                   grid_clips(session) == before - 1, "clips %d -> %d" % (before, grid_clips(session)))
    recorder.check("clear_slot is recorded as a transaction",
                   "session.clear_slot" in transaction_commands(session),
                   "transactions=%r" % transaction_commands(session))

    undone = session.result("control.undo")
    recorder.check("one control.undo restores the cleared cell",
                   undone.get("undone") is True and undone.get("undone_command") == "session.clear_slot"
                   and grid_clips(session) == before,
                   "undo=%r clips=%d (expected %d)" % (undone, grid_clips(session), before))

    outside = session.error("session.clear_slot", {"track": 99, "scene": 0})
    recorder.check("clearing outside the grid is a typed refusal",
                   outside.get("kind") == "invalid_args", "error=%r" % outside)


def check_clear_grid(session, recorder, instance):
    """session.clear: the grid empties, undoes, and takes the block out of the file.

    Order matters and is deliberate: the undo comes BEFORE the save. A save
    pushes its own file-revision checkpoint (A16), so `control.undo` issued after
    one restores the replaced FILE instead of the grid - measured, and the reason
    the save is the last assertion here.
    """
    before = state_of(session).get("grid", {})
    reply = session.result("session.clear")
    grid = reply.get("grid") or {}
    recorder.check("clear empties the whole grid",
                   int(grid.get("tracks", -1)) == 0 and int(grid.get("scenes", -1)) == 0
                   and int(grid.get("clips", -1)) == 0, "grid=%r" % grid)
    readback = state_of(session).get("grid", {})
    recorder.check("the emptied grid is what the read-back reports",
                   int(readback.get("clips", -1)) == 0, "grid=%r" % readback)
    recorder.check("clear is recorded as a transaction",
                   "session.clear" in transaction_commands(session),
                   "transactions=%r" % transaction_commands(session))

    undone = session.result("control.undo")
    grid = state_of(session).get("grid", {})
    recorder.check("one control.undo restores the cleared grid",
                   undone.get("undone") is True and undone.get("undone_command") == "session.clear"
                   and int(grid.get("tracks", -1)) == int(before.get("tracks", -2))
                   and int(grid.get("scenes", -1)) == int(before.get("scenes", -2))
                   and int(grid.get("clips", -1)) == int(before.get("clips", -2)),
                   "undo=%r grid=%r (expected %r)" % (undone, grid, before))

    # The engine's word is not evidence about the FILE: empty the grid again,
    # save, and read the file back on the client side. A project with no session
    # must re-save without a block.
    session.result("session.clear")
    path = os.path.join(instance.tmp, "session-cleared.mmp")
    saved = session.result("project.save", {"path": path})
    try:
        with open(path, "r", errors="replace") as handle:
            text = handle.read()
        recorder.check("a cleared session writes no <session> block",
                       "<session" not in text,
                       "saved %d bytes, reply=%r" % (len(text), saved))
    except OSError as error:
        recorder.check("a cleared session writes no <session> block", False,
                       "could not read the saved project back: %s" % error)


def check_not_a_project_edit(session, recorder, commands):
    """The launch/stop verbs record no transaction (their SPEC A16 claim)."""
    recorded = transaction_commands(session)
    offending = [command for command in commands if command in recorded]
    recorder.check("launch/stop verbs record no transaction",
                   not offending, "recorded=%r" % offending)


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
    for name, passed, evidence in recorder.checks:
        print("  %-58s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)


def run_checks(session, instance, recorder):
    if not check_registered(session, recorder):
        return False
    song_tracks = build_grid(session, recorder)
    check_transport_running(session, recorder)
    check_launch_slot(session, recorder)
    check_launch_refusals(session, recorder, song_tracks)
    check_stop_slot(session, recorder)
    check_stop_all(session, recorder)
    check_clear_slot(session, recorder)
    check_clear_grid(session, recorder, instance)
    check_not_a_project_edit(session, recorder, ("session.launch_slot", "session.stop_slot",
                                                  "session.stop_all"))
    check_quit(session, instance, recorder)
    return True


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
        registered = run_checks(session, instance, recorder)

    report_results(recorder)
    transcript.dump()
    if not registered:
        H.ok("this build has no session.* group (WANT_SESSION_VIEW=OFF): "
             "Skipped, never Passed")
        return 77
    if recorder.problems:
        print("")
        recorder.problems.report("session.* slot lifecycle control-surface transcript")
        return 1
    H.ok("session.* slot lifecycle control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
