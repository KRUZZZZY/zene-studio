#!/usr/bin/env python3
"""THE per-id proof table for the `session.*` command group, driven through ONE
live `--control-socket` instance.

WHY THIS FILE EXISTS. The group has two registered proofs already -
`control-session-m1.py` (ctest ControlSessionLaunch: the grid, the scene launch
and the launch read-back) and `control-session-lifecycle-transcript.py` (ctest
ControlSessionLifecycleTranscript: launch_slot / stop_slot / stop_all /
clear_slot / clear). Each drives the ids it needs, in the order the FEATURE
wants. Neither answers the question this file exists to answer, which is the
audit question and not the feature question:

    for EACH of the group's eleven ids, is the thing that ran the WORK,
    or is it a stub that only refuses?

So the unit of this file is the ID, not the feature. Every id gets one row,
the row is built from a live call and its read-back, and the row carries the
measuring command and the measured result - including the A16 classification
read back OUT OF THE RUNNING INSTANCE (`control.transactions`), so the table's
"registered reference" column is a measurement too and not a grep.

THE ELEVEN IDS (the group's whole surface, in the order the table prints):

    session.get_state  session.set_grid  session.set_quantisation
    session.set_scene  session.set_slot  session.clear  session.clear_slot
    session.launch_slot  session.launch_scene  session.stop_slot
    session.stop_all

DRIVE ORDER vs TABLE ORDER. The table prints in the fixed order above; the
driving order is the one that keeps the session legal (a grid must exist before
a cell is addressed; a cell must hold a clip before it is launched; a clear
must come after the reads it would otherwise erase). The drive order is printed
by the run, so a reader can see that no row depends on state a later row built.

WHAT A REFUSAL STUB WOULD LOOK LIKE, and why a row would say so: a handler that
returns only a typed failure. Every row here asserts the id answered `ok:true`
for a LEGAL call AND that the claimed effect is visible in the next read-back,
so a stub cannot pass a row. The refusals the group DOES have (an out-of-grid
address, an empty cell, a column with no song track) are measured too, as the
refusal rows at the bottom, because "refuses the illegal call" is the property
a stub would fail to combine with "performs the legal one".

WHAT IT DOES NOT PROVE. A launch's per-slot PHASE is not readable over the
socket (session.get_state reports the model's cells and the aggregate launch
counters, not SlotLaunchState), so nothing here asserts a stopped slot returned
to Idle; that half is tests/src/core/SessionSchedulerTest.cpp. And a launched
slot renders no audio in this tree (src/core/SessionClip.cpp is serialisation
only) - the limitation docs/KNOWN-LIMITATIONS.md carries - so "started" here
means the engine recorded the start on the scheduled line, which is exactly
what launch.start_line reports and is stated as such rather than as audio.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path.

Usage: QT_QPA_PLATFORM=offscreen python3 control-session-api-proof.py <zene>
Exit code 0 only when every row's checks held; 77 (ctest Skipped, never Passed)
when this build has no session.* group at all (WANT_SESSION_VIEW=OFF).
"""

import hashlib
import json
import os
import sys
import time

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 3000))

#: The group's whole surface, in the order the table prints.
IDS = (
    "session.get_state",
    "session.set_grid",
    "session.set_quantisation",
    "session.set_scene",
    "session.set_slot",
    "session.clear",
    "session.clear_slot",
    "session.launch_slot",
    "session.launch_scene",
    "session.stop_slot",
    "session.stop_all",
)

#: The six the A16 table classifies `true_inverse` and the five it classifies
#: `not_mutating`. Measured against the live instance, not assumed: the class
#: in the table is the one the RUNNING registry reported.
MUTATING = ("session.set_grid", "session.set_quantisation", "session.set_scene",
            "session.set_slot", "session.clear_slot", "session.clear")
NON_MUTATING = ("session.get_state", "session.launch_slot", "session.launch_scene",
                "session.stop_slot", "session.stop_all")

#: The play head is parked a quarter of a bar in, so the line a launch is
#: scheduled for is three quarters of a bar away and no launch races a boundary.
SEEK_FRACTION = 0.25
LAUNCH_TIMEOUT = 30.0
DRAIN_TIMEOUT = 20.0
RESET_TIMEOUT = 20.0
GRID_SCENES = 2
#: (track, scene) cells this run populates, plus one in the track-less last
#: column so the "no song track" refusal has a clip to find.
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
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def error(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is False:
            return reply.get("error") or {}
        return {}


class Rows:
    """One row per id, and the problems found building them.

    A row is (id, registered, command_sent, reply_ok, observed, a16, verdict).
    Every field is filled from a live call: `verdict` is the only field this
    file decides, and it is derived - STUB when the id never answered ok, and
    MEASURED when it answered, changed what it claims to change and (for the
    mutating six) recorded the class its A16 row declares.
    """

    def __init__(self):
        self.rows = {}
        self.problems = H.Problems()
        self.refusals = []
        #: Set by main(): the binary under test and the live registry's ids.
        self.binary = ""
        self.live_ids = set()

    def row(self, command):
        return self.rows.setdefault(command, {
            "command_sent": "", "reply_ok": None, "observed": "",
            "a16": "", "verdict": "UNMEASURED",
        })

    def check(self, command, passed, evidence):
        if not passed:
            self.problems.add("%s: %s" % (command, evidence))
        return passed

    def refusal(self, command, args, error, expectation):
        """A measured refusal: the id refused the ILLEGAL call as documented."""
        message = error.get("message") or ""
        passed = error.get("kind") == "invalid_args" and expectation in message
        if not passed:
            self.problems.add("%s refusal (%s): %r" % (command, args, error))
        self.refusals.append((command, json.dumps(args, separators=(",", ":")),
                              error.get("kind", "<ok>"), passed))
        return passed


def send(session, rows, command, args=None):
    """Issue one id and record the request verbatim on its row."""
    sent = command + (" " + json.dumps(args, separators=(",", ":"), sort_keys=True) if args else "")
    rows.row(command)["command_sent"] = sent
    return session.result(command, args)


def transaction_for(session, command):
    """The A16 record the live registry holds for `command`, or None."""
    records = session.result("control.transactions").get("transactions") or []
    for record in records:
        if record.get("command") == command:
            return record
    return None


def state_of(session):
    return session.result("session.get_state")


def launch(state):
    return state.get("launch", {})


def grid(state):
    return state.get("grid", {})


def wait_until(session, ready, timeout):
    started = time.time()
    state = state_of(session)
    while time.time() - started < timeout:
        if ready(state):
            return state, time.time() - started, True
        time.sleep(0.1)
        state = state_of(session)
    return state, time.time() - started, False


def hash_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


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
    ok = rows.check("session.set_scene",
                    scene.get("name") == "api-proof-scene" and scene.get("tempo_enabled") is True
                    and abs(float(scene.get("tempo", 0.0)) - 128.5) < 1e-6,
                    "reply scene=%r" % scene)
    readback = (state_of(session).get("scenes") or [{}])[0]
    ok &= rows.check("session.set_scene",
                     readback.get("name") == "api-proof-scene"
                     and readback.get("tempo_enabled") is True,
                     "the read-back scene is %r" % readback)
    record = transaction_for(session, "session.set_scene")
    ok &= rows.check("session.set_scene", record is not None,
                     "no A16 record was recorded for the edit")
    row = rows.row("session.set_scene")
    row["reply_ok"] = True
    row["observed"] = ("scene 0 name=%r tempo=%r tempo_enabled=%s; A16 record class=%s reversible=%s"
                       % (readback.get("name"), readback.get("tempo"),
                          readback.get("tempo_enabled"), (record or {}).get("class"),
                          (record or {}).get("reversible")))
    row["a16"] = "%s, reversible=%s (live)" % ((record or {}).get("class"),
                                               (record or {}).get("reversible"))
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
        ok &= rows.check("session.set_slot",
                         slot.get("type") == "midi" and slot.get("pattern") == 1 + track
                         and slot.get("name") == "slot-%d-%d" % (track, scene)
                         and slot.get("quantisation") == "global",
                         "cell (%d,%d) replied %r" % (track, scene, slot))
        if index == 0:
            record = transaction_for(session, "session.set_slot")
            ok &= rows.check("session.set_slot", record is not None,
                             "no A16 record was recorded for the edit")
            rows.row("session.set_slot")["a16"] = "%s, reversible=%s (live)" % (
                (record or {}).get("class"), (record or {}).get("reversible"))
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
    ok = rows.check("session.clear_slot",
                    slot.get("type") == "empty" and slot.get("track") == target[0]
                    and slot.get("scene") == target[1], "reply slot=%r" % slot)
    after = int(grid(state_of(session)).get("clips", -1))
    ok &= rows.check("session.clear_slot", after == before - 1,
                     "clips %d -> %d, not one fewer" % (before, after))
    record = transaction_for(session, "session.clear_slot")
    ok &= rows.check("session.clear_slot", record is not None, "no A16 record for the edit")
    undone = session.result("control.undo")
    restored = int(grid(state_of(session)).get("clips", -1))
    ok &= rows.check("session.clear_slot",
                     undone.get("undone") is True
                     and undone.get("undone_command") == "session.clear_slot"
                     and restored == before,
                     "one control.undo did not restore the cell: undo=%r clips=%d (wanted %d)"
                     % (undone, restored, before))
    row = rows.row("session.clear_slot")
    row["reply_ok"] = True
    row["observed"] = ("cell %r emptied (clips %d -> %d); A16 class=%s reversible=%s; one control.undo "
                       "restored it (clips back to %d, undone_command=%r)"
                       % (target, before, after, (record or {}).get("class"),
                          (record or {}).get("reversible"), restored,
                          undone.get("undone_command")))
    row["a16"] = "%s, reversible=%s (live)" % ((record or {}).get("class"),
                                               (record or {}).get("reversible"))
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok


def drive_clear(session, rows, instance):
    before = grid(state_of(session))
    reply = send(session, rows, "session.clear")
    after = grid(reply)
    ok = rows.check("session.clear",
                    int(after.get("tracks", -1)) == 0 and int(after.get("scenes", -1)) == 0
                    and int(after.get("clips", -1)) == 0, "reply grid=%r" % after)
    readback = grid(state_of(session))
    ok &= rows.check("session.clear", int(readback.get("clips", -1)) == 0,
                     "the read-back still reports clips: %r" % readback)
    record = transaction_for(session, "session.clear")
    ok &= rows.check("session.clear", record is not None, "no A16 record for the edit")
    undone = session.result("control.undo")
    restored = grid(state_of(session))
    ok &= rows.check("session.clear",
                     undone.get("undone") is True and undone.get("undone_command") == "session.clear"
                     and int(restored.get("clips", -1)) == int(before.get("clips", -2))
                     and int(restored.get("tracks", -1)) == int(before.get("tracks", -2)),
                     "one control.undo did not restore the whole grid: undo=%r grid=%r (wanted %r)"
                     % (undone, restored, before))
    # The engine's word is not evidence about the FILE: empty the grid again,
    # save and read the file back on the client side.
    session.result("session.clear")
    path = os.path.join(instance.tmp, "session-cleared.mmp")
    session.result("project.save", {"path": path})
    try:
        with open(path, "r", errors="replace") as handle:
            text = handle.read()
        ok &= rows.check("session.clear", "<session" not in text,
                         "a cleared grid still wrote a <session> block (%d bytes)" % len(text))
        file_evidence = "a cleared grid writes no <session> block (%d bytes saved)" % len(text)
    except OSError as error:
        ok &= rows.check("session.clear", False, "could not read the saved project back: %s" % error)
        file_evidence = "the saved project could not be read back: %s" % error
    row = rows.row("session.clear")
    row["reply_ok"] = True
    row["observed"] = ("grid %dx%d/%d clips -> %dx%d/0; A16 class=%s reversible=%s; one control.undo "
                       "restored %d clips; %s"
                       % (int(before.get("tracks", -1)), int(before.get("scenes", -1)),
                          int(before.get("clips", -1)), int(after.get("tracks", -1)),
                          int(after.get("scenes", -1)), (record or {}).get("class"),
                          (record or {}).get("reversible"), int(before.get("clips", -1)), file_evidence))
    row["a16"] = "%s, reversible=%s (live)" % ((record or {}).get("class"),
                                               (record or {}).get("reversible"))
    row["verdict"] = "MEASURED" if ok else "FAILED"
    return ok


# ---------------------------------------------------------------------------
# the refusals - a stub cannot refuse the illegal AND perform the legal call
# ---------------------------------------------------------------------------


def drive_schema_refusals(session, rows):
    """The SCHEMA layer, measured first because it needs no grid.

    Measured on run 1 and the reason this block exists separately: a value
    outside the declared argument bound is refused by the REGISTRY, before any
    handler runs, and its message names the bound ("track: 900 is above the
    maximum 255"). A stub-detector that only ever sent such a value would be
    measuring the schema and calling it the handler, so both layers are here.
    """
    rows.refusal("session.set_grid", {"tracks": 9000, "scenes": 2},
                 session.error("session.set_grid", {"tracks": 9000, "scenes": 2}), "tracks")
    rows.refusal("session.set_quantisation", {"quantisation": "whenever"},
                 session.error("session.set_quantisation", {"quantisation": "whenever"}),
                 "quantisation")
    rows.refusal("session.set_scene", {"scene": 900},
                 session.error("session.set_scene", {"scene": 900}), "above the maximum")
    rows.refusal("session.set_slot", {"track": 900, "scene": 0, "type": "empty"},
                 session.error("session.set_slot", {"track": 900, "scene": 0, "type": "empty"}),
                 "above the maximum")
    rows.refusal("session.launch_slot", {"track": 900, "scene": 0},
                 session.error("session.launch_slot", {"track": 900, "scene": 0}), "above the maximum")
    rows.refusal("session.launch_scene", {"scene": 900},
                 session.error("session.launch_scene", {"scene": 900}), "above the maximum")
    rows.refusal("session.clear_slot", {"track": 900, "scene": 0},
                 session.error("session.clear_slot", {"track": 900, "scene": 0}), "above the maximum")
    rows.refusal("session.stop_slot", {"track": 900, "scene": 0},
                 session.error("session.stop_slot", {"track": 900, "scene": 0}), "above the maximum")
    rows.refusal("session.clear", {"tracks": 1},
                 session.error("session.clear", {"tracks": 1}), "")


def drive_grid_refusals(session, rows, song_tracks):
    """The HANDLER layer: a value inside the schema bound but outside the grid.

    This is the half a refusal stub would also pass, which is why it is here
    BESIDE the eleven measured rows and not instead of them: the property a stub
    cannot have is refusing the illegal call AND performing the legal one.
    """
    # The rows above emptied the grid, so rebuild one. This runs LAST on
    # purpose: the eleven rows have already been read, so rebuilding cannot
    # launder any of them.
    columns = song_tracks + 1
    session.result("session.set_grid", {"tracks": columns, "scenes": GRID_SCENES})
    session.result("session.set_slot", {"track": 0, "scene": 1, "type": "midi", "pattern": 1,
                                        "name": "refusal-clip", "quantisation": "global"})
    session.result("session.set_slot", {"track": song_tracks, "scene": 1, "type": "midi",
                                        "pattern": 2, "name": "trackless-clip",
                                        "quantisation": "global"})
    live = grid(state_of(session))
    rows.check("session.set_grid", int(live.get("tracks", -1)) == columns,
               "the refusal grid was not rebuilt: %r" % live)

    rows.refusal("session.set_scene", {"scene": GRID_SCENES},
                 session.error("session.set_scene", {"scene": GRID_SCENES}), "is outside the grid")
    rows.refusal("session.set_slot", {"track": columns, "scene": 0, "type": "empty"},
                 session.error("session.set_slot", {"track": columns, "scene": 0, "type": "empty"}),
                 "is outside the grid")
    rows.refusal("session.set_slot", {"track": 0, "scene": 0, "type": "midi"},
                 session.error("session.set_slot", {"track": 0, "scene": 0, "type": "midi"}),
                 "needs 'pattern'")
    rows.refusal("session.clear_slot", {"track": columns, "scene": 0},
                 session.error("session.clear_slot", {"track": columns, "scene": 0}),
                 "is outside the grid")
    rows.refusal("session.stop_slot", {"track": columns, "scene": 0},
                 session.error("session.stop_slot", {"track": columns, "scene": 0}),
                 "is outside the grid")
    rows.refusal("session.launch_slot", {"track": 0, "scene": 0},
                 session.error("session.launch_slot", {"track": 0, "scene": 0}), "is empty")
    rows.refusal("session.launch_slot", {"track": song_tracks, "scene": 1},
                 session.error("session.launch_slot", {"track": song_tracks, "scene": 1}),
                 "has no song track")
    rows.refusal("session.launch_scene", {"scene": GRID_SCENES},
                 session.error("session.launch_scene", {"scene": GRID_SCENES}), "is outside the grid")


# ---------------------------------------------------------------------------
# reporting
# ---------------------------------------------------------------------------


def print_table(rows, binary_hash, instance, ticks_per_bar, drive_order):
    print("")
    print("==== session.* per-id proof table ====")
    print("binary:    %s" % rows.binary)
    print("sha256:    %s" % binary_hash)
    print("socket:    %s" % instance.socket_path)
    print("ticks/bar: %d" % ticks_per_bar)
    print("drive order: %s" % " -> ".join(drive_order))
    print("")
    header = ("%-24s %-10s %-6s %s" % ("id", "registered", "reply", "measured behaviour / A16 (live)"))
    print(header)
    print("-" * len(header))
    for command in IDS:
        row = rows.rows.get(command) or {}
        registered = "yes" if command in rows.live_ids else "NO"
        reply = {True: "ok", False: "error", None: "-"}[row.get("reply_ok")]
        print("%-24s %-10s %-6s %s" % (command, registered, reply, row.get("observed", "<no row built>")))
        print("%-24s %-10s %-6s   A16 %s" % ("", "", "", row.get("a16", "")))
        print("%-24s %-10s %-6s   VERDICT: %s" % ("", "", "", row.get("verdict", "UNMEASURED")))
    print("")
    print("==== measured refusals (each id refuses the ILLEGAL call) ====")
    for command, args, kind, passed in rows.refusals:
        print("  %-24s %-34s kind=%-14s %s" % (command, args, kind, "ok" if passed else "FAILED"))


def print_transcript(transcript, elide_below=2000):
    """The raw transcript, with the one payload that would drown it elided.

    `control.commands_list` answers with every registered command's schemas -
    measured at ~410 KB of a ~460 KB run, because the surface has 173 ids. It is
    elided HERE and only here, with the count and the reason printed in its
    place, because every one of its entries is a different id's registered
    reference and the table above is this file's answer for the eleven that
    matter. Nothing else is elided: the per-id requests, their replies, the
    refusal replies and the A16 transaction records are printed in full, because
    they ARE the evidence.
    """
    print("\n---- raw request/response transcript ----")
    for line in transcript.lines:
        if line.startswith("-< ") or line.startswith("<- "):
            if "commands_list" not in line and len(line) > elide_below:
                print("%s [%d bytes, printed in full below]" % (line[:120], len(line)))
                print("...%s" % line[-elide_below:])
                continue
        if "control.commands_list" in line and line.startswith("-> "):
            print(line)
            continue
        if '"commands":[' in line and line.startswith("<- "):
            print('<- {"id":%s,"ok":true,"result":{"commands":[<%d entries elided: every '
                  'registered command\'s schemas and requires declaration; the eleven this '
                  'file audits have their own rows in the table above>],"count":%d,"proto":1}}'
                  % (line.split('"id":', 1)[-1].split(",", 1)[0],
                     line.count('"group":'), line.count('"group":')))
            continue
        print(line)


def parse_argv(argv):
    """`<binary> [--out PATH]` - the artefact path is the only option."""
    binary = None
    out = None
    index = 1
    while index < len(argv):
        if argv[index] == "--out" and index + 1 < len(argv):
            out = argv[index + 1]
            index += 2
            continue
        binary = binary or argv[index]
        index += 1
    return binary, out


class Tee:
    """Writes the run to stdout and (when asked) to the artefact file."""

    def __init__(self, handle=None, console=None):
        self.handle = handle
        self.console = console if console is not None else sys.stdout

    def write(self, text):
        self.console.write(text)
        if self.handle is not None:
            self.handle.write(text)

    def flush(self):
        self.console.flush()
        if self.handle is not None:
            self.handle.flush()


def main(argv):
    binary, out = parse_argv(argv)
    if binary is None:
        print(__doc__)
        return 2
    console = sys.stdout
    handle = open(out, "w") if out else None
    tee = Tee(handle, console)
    sys.stdout = tee
    try:
        return run(os.path.abspath(binary), handle)
    finally:
        # Restore the REAL stdout and flush the artefact BEFORE closing it:
        # leaving a closed handle as sys.stdout makes the interpreter's own
        # final flush fail, and CPython turns that into exit code 120 on a run
        # whose every check held (measured - the first --out run reported 120
        # beside its own PASS line, which no ctest would have accepted).
        sys.stdout = console
        tee.flush()
        if handle is not None:
            handle.close()


def run(binary, handle):
    rows = Rows()
    rows.binary = binary
    drive_order = []
    transcript = H.Transcript()
    with H.start_instance(binary) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        version = session.result("control.version")
        listing = session.result("control.commands_list")
        rows.live_ids = {entry.get("id") for entry in listing.get("commands", [])}
        print("instance:   %s" % binary)
        print("socket:     %s" % instance.socket_path)
        print("version:    %r" % version)
        print("live ids:   %d" % len(rows.live_ids))

        missing = [command for command in IDS if command not in rows.live_ids]
        if missing:
            # No group at all: this is the WANT_SESSION_VIEW=OFF build. A test
            # that cannot run must be Skipped, never Passed.
            if len(missing) == len(IDS):
                print("this build registers no session.* id (WANT_SESSION_VIEW=OFF): "
                      "Skipped, never Passed")
                return 77
            rows.problems.add("registered ids missing from the live registry: %r" % missing)

        # The schema layer first: it needs no grid, and measuring it here keeps
        # it distinct from the handler layer at the bottom.
        drive_schema_refusals(session, rows)
        drive_order += ["(the schema-layer refusals)"]

        drive_get_state(session, rows)
        columns, song_tracks, _ = drive_set_grid(session, rows)
        drive_order += ["session.get_state", "session.set_grid", "session.set_quantisation",
                        "session.set_scene", "session.set_slot"]
        drive_set_quantisation(session, rows)
        drive_set_scene(session, rows)
        drive_set_slot(session, rows, columns, song_tracks)
        _, ticks_per_bar = drive_transport(session, rows)
        drive_order += ["session.launch_slot", "session.launch_scene", "session.stop_slot",
                        "session.stop_all", "session.clear_slot", "session.clear"]
        drive_launch_slot(session, rows)
        drive_launch_scene(session, rows)
        drive_stop_slot(session, rows)
        drive_stop_all(session, rows)
        drive_clear_slot(session, rows)
        drive_clear(session, rows, instance)

        # The five launch/stop verbs record NO transaction - their A16 claim.
        recorded = {record.get("command")
                    for record in (session.result("control.transactions").get("transactions") or [])}
        offending = sorted(command for command in NON_MUTATING if command in recorded)
        if offending:
            rows.problems.add("launch/stop verbs recorded a transaction: %r" % offending)
        for command in MUTATING:
            if command not in recorded:
                rows.problems.add("%s recorded no A16 transaction" % command)

        drive_grid_refusals(session, rows, song_tracks)
        drive_order += ["(the grid-layer refusals)"]

        # Every declared row must exist and be MEASURED.
        for command in IDS:
            row = rows.rows.get(command)
            if row is None:
                rows.problems.add("%s: no row was built" % command)
            elif row["verdict"] != "MEASURED":
                rows.problems.add("%s: verdict is %s" % (command, row["verdict"]))

        reply = session.call("control.quit")
        if reply.get("ok") is not True:
            rows.problems.add("control.quit did not answer ok: %r" % reply)
        else:
            session.client.close()
            exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
            if not (exited and code == 0):
                rows.problems.add("control.quit did not stop the instance: exited=%r code=%r"
                                  % (exited, code))

    print_table(rows, hash_of(binary), instance, ticks_per_bar, drive_order)
    print_transcript(transcript)
    if rows.problems:
        print("")
        rows.problems.report("session.* per-id proof table")
        return 1
    H.ok("session.* per-id proof table (every one of the eleven ids measured)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
