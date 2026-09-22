#!/usr/bin/env python3
"""RAW control-surface transcript for the livecode.* verbs (board card #708).

The acceptance evidence for SCHEDULED Lua evaluation - the idle and transport
hooks the Lua engine did not have - produced by driving the REAL `zene` binary
headless and printing every request and reply verbatim.

WHAT THIS PROVES, AND WHAT IT DOES NOT.
The clock is driven with the transport running on the engine's own (dummy)
audio device: a script is scheduled ONCE and then edits a mixer value on every
bar without anything re-triggering it, a second script rides the transport
edges, and a runaway script hits its per-fire instruction budget while the
audio keeps running and the clock keeps ticking. The boundary ARITHMETIC
(seeks, the per-poll cap, edge detection) is proved without an audio device by
the registered QTest `ScriptClockTest`; this transcript proves the wiring. The
bound that remains is stated in docs/KNOWN-LIMITATIONS.md: a fire lands up to
one 25 ms poll after its boundary, and the mixer edit is observed through the
surface (mixer.get_state), not as DSP output.

The checks, in order:

  1. script.run (v0 UNCHANGED)    an existing-shape v0 script runs through the
                                  EXPLICIT trigger exactly as before this card
                                  (data/scripts/create-pattern.lua, `ran`, its
                                  log, and the track it creates);
  2. livecode.get_state, fresh    a new session schedules nothing and reports
                                  zero counters;
  3. livecode.schedule once      ONE schedule call per hook, then only READS
     each (bar + transport)       while the play head crosses bars: fires grow,
     + transport.play             the mixer value flips per bar, errors stay 0;
  4. livecode.schedule           the transport hook: one fire per edge, and a
     (hook=transport)             STOPPED transport freezes the bar clock -
                                  the hooks are bound to the transport, not
                                  to the wall clock;
  5. livecode.schedule           NEGATIVE CONTROL: `while true do end` under
     (budget, runaway)            a typed budget hits 'instruction budget
                                  exceeded', the audio keeps running across
                                  the overrun, and the clock keeps firing;
  6. refusals                     every junk argument is typed (invalid_args /
                                  not_found) and NO refusal changes a schedule;
  7. control.transactions/undo    the A16 records: the writers are snapshot
                                  rows, the read leaves none, and one
                                  control.undo removes a schedule or rearms
                                  one that was taken off;
  8. livecode.schedule (replace)  the LIVE EDIT: re-scheduling the same id
                                  replaces its script in place, one slot.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path. Usage: QT_QPA_PLATFORM=offscreen python3
control-livecode-commands.py <zene> <create-pattern.lua>
Exit code 0 only when every assertion held.
"""

import sys
import time

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))

# The engine's grid: 192 ticks to a bar at the default 4/4 meter (TimePos.h),
# default tempo 140 (Song.h DefaultTempo) -> a bar is ~1.7 s of playback.
TICKS_PER_BAR = 192
# A fire happens when the control thread's 25 ms poll observes a crossing: at
# 140 bpm that is under 5 ticks of overshoot plus an audio period, so a fire
# within 12 ticks of the line is ON the line and one far outside is not.
BAR_LINE_TOLERANCE_TICKS = 12
# Two bars of playback are ~3.4 s; three fires need ~4.5 s of the play head.
FIRE_WAIT_S = 25.0
EDGE_WAIT_S = 5.0
OVERRUN_WAIT_S = 15.0


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        return self.client.call(next(REQUEST_IDS), command, args, transcript=self.transcript)

    def result(self, command, args=None):
        """The reply, or {'error': ...} so a failed call is visible in a check."""
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def typed_error(self, command, args=None):
        reply = self.call(command, args)
        return (reply.get("error") or {}) if reply.get("ok") is False else {}


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
def clock_state(session):
    return session.result("livecode.get_state")


def schedule_entry(state, sched_id):
    for one in state.get("schedules") or []:
        if one.get("id") == sched_id:
            return one
    return {}


def fires_of(state, sched_id):
    return int(schedule_entry(state, sched_id).get("fires") or 0)


def channel0_volume(session):
    channels = session.result("mixer.get_state").get("channels") or []
    first = channels[0] if channels else {}
    return round(float(first.get("volume") or 0.0), 4)


def position_ticks(session):
    return int(session.result("transport.get_state").get("position_ticks") or 0)


def schedule_shape(state):
    """The schedules minus their fire counters: what a REFUSAL must not change."""
    return [{"id": one.get("id"), "hook": one.get("hook"), "budget": one.get("budget"),
             "source_bytes": one.get("source_bytes")}
            for one in state.get("schedules") or []]


def wait_until(session, predicate, timeout_s):
    """Poll livecode.get_state until predicate holds or the budget expires."""
    deadline = time.time() + timeout_s
    state = clock_state(session)
    while time.time() < deadline and not predicate(state):
        time.sleep(0.15)
        state = clock_state(session)
    return state


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------
def check_v0_explicit_run_unchanged(session, recorder, script_path):
    """HARD CONSTRAINT: the EXPLICIT-trigger path keeps working, unchanged."""
    run = session.result("script.run", {"path": script_path})
    recorder.check("an existing-shape v0 script still runs on script.run",
                   run.get("ran") is True,
                   "ran=%r log_lines=%r error=%r" % (run.get("ran"), run.get("log_lines"),
                                                     run.get("error")))
    recorder.check("the v0 run reported its log lines, not a silent success",
                   int(run.get("log_lines") or 0) >= 1, "log=%r" % (run.get("log"),))
    recorder.check("the v0 run was not budget-overridden by anything the clock does",
                   run.get("budget_overridden") is False
                   and int(run.get("instruction_budget") or 0) > 0,
                   "budget=%r overridden=%r" % (run.get("instruction_budget"),
                                                run.get("budget_overridden")))
    # What the script CREATED lives in the pattern store's nested container
    # (docs/KNOWN-LIMITATIONS.md: a nested track is deliberately not in
    # track.list's song-container view), so read it back the way a second v0
    # script would: through the EXPLICIT path, over the store's own bindings.
    probe_source = (
        "local s = zene.song():patternStore()\n"
        "local clipHit, trackHit = false, false\n"
        "for p = 0, s:patternCount() - 1 do for t = 0, s:trackCount() - 1 do\n"
        "  local c = s:patternClip(p, t)\n"
        "  if c:isValid() and c:name() == 'Hi-Hat 16' and c:noteCount() == 16 then clipHit = true end\n"
        "end end\n"
        "for t = 0, s:trackCount() - 1 do\n"
        "  local k = s:track(t)\n"
        "  if k:isValid() and k:name() == 'Hi-Hat' then trackHit = true end\n"
        "end\n"
        "zene.log():info('probe clip=' .. tostring(clipHit) .. ' track=' .. tostring(trackHit))\n")
    probe = session.result("script.run", {"source": probe_source})
    log = " ".join(str(one) for one in (probe.get("log") or []))
    recorder.check("the v0 run mutated the session: probe reads its Hi-Hat pattern back",
                   probe.get("ran") is True and "probe clip=true track=true" in log,
                   "ran=%r log=%r" % (probe.get("ran"), log))


def check_fresh_clock_state(session, recorder):
    """A new session schedules nothing, fires nothing, counts nothing."""
    state = clock_state(session)
    empty = (state.get("count") == 0 and (state.get("schedules") or []) == []
             and state.get("playing") is False)
    recorder.check("a fresh session has no schedules at all", empty, "state=%r" % state)
    recorder.check("a fresh session's counters are all zero",
                   state.get("counters") == {"fires": 0, "errors": 0, "budget_exceeded": 0,
                                             "busy_skips": 0, "regrids": 0, "coalesced": 0},
                   "counters=%r" % state.get("counters"))
    recorder.check("the clock declares its poll interval",
                   state.get("poll_ms") == 25, "poll_ms=%r" % state.get("poll_ms"))


def check_schedule_reported(scheduled, recorder):
    """What livecode.schedule must say about the id it armed."""
    recorder.check("livecode.schedule reports the id it armed",
                   scheduled.get("id") == "lc-bar" and scheduled.get("scheduled") is True
                   and scheduled.get("replaced") is False,
                   "result=%r" % scheduled)


def check_fire_book(entry, recorder):
    """The bar schedule's own book: fires, no failures, on the line."""
    recorder.check("the scheduled script FIRED on three bars with no re-trigger",
                   int(entry.get("fires") or 0) >= 3, "entry=%r" % entry)
    recorder.check("every fire succeeded: no errors, no budget refusals",
                   int(entry.get("errors") or 0) == 0
                   and int(entry.get("budget_exceeded") or 0) == 0,
                   "errors=%r budget=%r last_error=%r" % (entry.get("errors"),
                                                          entry.get("budget_exceeded"),
                                                          entry.get("last_error")))
    last_fire = int(entry.get("last_fire_ticks") or -1)
    off_line = min(last_fire % TICKS_PER_BAR, TICKS_PER_BAR - (last_fire % TICKS_PER_BAR))
    recorder.check("the last fire was ON its bar line, not merely after it",
                   last_fire >= 0 and off_line <= BAR_LINE_TOLERANCE_TICKS,
                   "last_fire_ticks=%r off_line=%r" % (last_fire, off_line))


def collect_volume_flips(session, recorder, timeout_s=4.0):
    """Sample mixer ch-0 until the scheduled script's edit actually flips it:
    a bar is ~1.7 s, so one fixed window could land between two flips."""
    volumes = set()
    deadline = time.time() + timeout_s
    while time.time() < deadline and len(volumes) < 2:
        volumes.add(channel0_volume(session))
        time.sleep(0.2)
    recorder.check("the mixer value the script edits CHANGED while it fired",
                   len(volumes) >= 2 and volumes <= {0.5, 1.0},
                   "volumes=%r" % sorted(volumes))


def check_bar_edits_without_retrigger(session, recorder):
    """THE CARD: scheduled ONCE each, then a mixer edit on every bar, only reads."""
    source = ("local channel = zene.mixer():channel(0)\n"
              "if channel:gain() > 0.9 then channel:setGain(0.5) else channel:setGain(1.0) end\n")
    scheduled = session.result("livecode.schedule",
                               {"id": "lc-bar", "hook": "bar", "source": source})
    check_schedule_reported(scheduled, recorder)
    # The transport hook is armed BEFORE the play, so the edge it must catch is
    # the one this call raises.
    session.result("livecode.schedule",
                   {"id": "lc-edge", "hook": "transport", "source": "return 1"})
    started = session.result("transport.play")
    recorder.check("the transport started", started.get("playing") is True,
                   "state=%r" % started)

    # From here on ONLY reads cross the socket: livecode.get_state,
    # transport.get_state and mixer.get_state. No re-trigger exists to find.
    state = wait_until(session, lambda s: fires_of(s, "lc-bar") >= 3, FIRE_WAIT_S)
    entry = schedule_entry(state, "lc-bar")
    check_fire_book(entry, recorder)
    collect_volume_flips(session, recorder)
    recorder.check("the play head really advanced across the fires",
                   position_ticks(session) > 0, "position=%r" % position_ticks(session))
    return int(entry.get("fires") or 0)


def check_transport_edges(session, recorder):
    """The transport hook: one fire per edge; a stopped clock fires no bars."""
    state = clock_state(session)
    recorder.check("the transport hook fired ONCE for the play edge",
                   fires_of(state, "lc-edge") == 1, "state=%r" % schedule_entry(state, "lc-edge"))

    session.result("transport.stop")
    state = wait_until(session, lambda s: fires_of(s, "lc-edge") >= 2, EDGE_WAIT_S)
    recorder.check("the transport hook fired ONCE MORE for the stop edge",
                   fires_of(state, "lc-edge") >= 2, "state=%r" % schedule_entry(state, "lc-edge"))

    # Let whatever the stop itself moves (the rewind, the last audio period)
    # settle, THEN take the baseline the freeze is measured against.
    time.sleep(0.5)
    frozen_fires = fires_of(clock_state(session), "lc-bar")
    frozen_pos = position_ticks(session)
    time.sleep(1.0)
    after = clock_state(session)
    recorder.check("a STOPPED transport freezes the bar clock (the hooks follow "
                   "the transport, not the wall clock)",
                   fires_of(after, "lc-bar") == frozen_fires
                   and position_ticks(session) == frozen_pos,
                   "fires %r -> %r position %r -> %r"
                   % (frozen_fires, fires_of(after, "lc-bar"), frozen_pos,
                      position_ticks(session)))


def check_overrun_hits_the_bound(session, recorder, bars_so_far):
    """NEGATIVE CONTROL: a runaway hits the typed budget; audio keeps running."""
    scheduled = session.result("livecode.schedule",
                               {"id": "lc-run", "hook": "bar", "budget": 2000000,
                                "source": "while true do end"})
    recorder.check("the runaway schedule was accepted (it compiles; it is valid Lua)",
                   scheduled.get("id") == "lc-run" and scheduled.get("budget") == 2000000,
                   "result=%r" % scheduled)

    session.result("transport.play")
    # Sampled AFTER the play, so the comparison is made purely across the
    # window that contains the overrun - whatever the stop left behind.
    before_ticks = position_ticks(session)
    state = wait_until(session,
                       lambda s: int(schedule_entry(s, "lc-run").get("budget_exceeded") or 0) >= 1,
                       OVERRUN_WAIT_S)
    run = schedule_entry(state, "lc-run")
    recorder.check("the runaway hit its instruction budget, typed",
                   int(run.get("budget_exceeded") or 0) >= 1
                   and int(run.get("errors") or 0) >= 1,
                   "run=%r" % run)
    recorder.check("the reported failure names the bound it hit",
                   "instruction budget exceeded" in str(run.get("last_error") or "")
                   and run.get("last_error_kind") == "budget",
                   "kind=%r error=%r" % (run.get("last_error_kind"), run.get("last_error")))
    recorder.check("the audio kept RUNNING across the overrun",
                   position_ticks(session) > before_ticks,
                   "position %r -> %r" % (before_ticks, position_ticks(session)))
    recorder.check("the clock kept firing through and after the overrun",
                   fires_of(state, "lc-bar") > bars_so_far,
                   "bar fires %r -> %r" % (bars_so_far, fires_of(state, "lc-bar")))
    recorder.check("the overrun did not leak into the other schedules' books",
                   int(schedule_entry(state, "lc-bar").get("errors") or 0) == 0,
                   "lc-bar=%r" % schedule_entry(state, "lc-bar"))


def check_refusals_are_typed(session, recorder):
    """Every junk argument is typed, and NO refusal changes a schedule."""
    before = schedule_shape(clock_state(session))
    cases = [
        ("no source and no path is invalid_args", "livecode.schedule", {}, "invalid_args"),
        ("source and path together is invalid_args", "livecode.schedule",
         {"source": "return 1", "path": "/no/such/script.lua"}, "invalid_args"),
        ("an empty source is invalid_args", "livecode.schedule", {"source": ""}, "invalid_args"),
        ("a source that does not compile is invalid_args", "livecode.schedule",
         {"source": "if then end"}, "invalid_args"),
        ("an unknown hook is invalid_args", "livecode.schedule",
         {"source": "return 1", "hook": "thursday"}, "invalid_args"),
        ("a budget below its floor is invalid_args", "livecode.schedule",
         {"source": "return 1", "budget": -1}, "invalid_args"),
        ("a budget that is not an integer is invalid_args", "livecode.schedule",
         {"source": "return 1", "budget": 1.5}, "invalid_args"),
        ("an unknown property is invalid_args", "livecode.schedule",
         {"clip": "nope"}, "invalid_args"),
        ("unschedule without an id is invalid_args", "livecode.unschedule", {}, "invalid_args"),
        ("unschedule of an empty id is invalid_args", "livecode.unschedule",
         {"id": ""}, "invalid_args"),
        ("unschedule of an unknown id is not_found", "livecode.unschedule",
         {"id": "lc-nope"}, "not_found"),
    ]
    for label, command, args, kind in cases:
        error = session.typed_error(command, args)
        recorder.check(label, error.get("kind") == kind,
                       "kind=%r message=%r" % (error.get("kind"), error.get("message")))
    after = schedule_shape(clock_state(session))
    recorder.check("no refused call changed a single schedule", after == before,
                   "before=%r after=%r" % (before, after))


def livecode_records(session):
    """This group's A16 records, oldest first."""
    transactions = session.result("control.transactions").get("transactions") or []
    return [one for one in transactions
            if str(one.get("command", "")).startswith("livecode.")]


def check_livecode_records(session, recorder):
    """The A16 classes: the writers are snapshot rows, the read leaves none."""
    records = livecode_records(session)
    recorder.check("every livecode record carries a snapshot class",
                   records and all(one.get("class") == "snapshot" for one in records),
                   "classes=%r" % [(one.get("command"), one.get("class")) for one in records])
    recorder.check("the read left no transaction to reverse",
                   all(one.get("command") != "livecode.get_state" for one in records),
                   "commands=%r" % sorted({one.get("command") for one in records}))
    recorder.check("the schedule records carry their before-state and inverse",
                   all(one.get("before") is not None and one.get("inverse")
                       for one in records if one.get("command") == "livecode.schedule"),
                   "records=%r" % records)


def check_undo_new_schedule(session, recorder):
    """Undo of a NEW schedule: the id leaves the clock."""
    session.result("livecode.schedule",
                   {"id": "lc-tx", "hook": "beat", "source": "return 1"})
    undone = session.result("control.undo")
    entry = schedule_entry(clock_state(session), "lc-tx")
    recorder.check("one control.undo takes a new schedule off the clock",
                   undone.get("undone_command") == "livecode.schedule" and not entry,
                   "undone=%r entry=%r" % (undone.get("undone_command"), entry))


def check_undo_rearms_an_unschedule(session, recorder):
    """Undo of an UNSCHEDULE: the definition comes BACK."""
    session.result("livecode.schedule",
                   {"id": "lc-ud", "hook": "beat", "source": "return 'kept'"})
    session.result("livecode.unschedule", {"id": "lc-ud"})
    undone = session.result("control.undo")
    rearmed = schedule_entry(clock_state(session), "lc-ud")
    # get_state reports source_bytes, not the source text: 13 is `return 'kept'`.
    recorder.check("one control.undo rearms a schedule that was taken off",
                   undone.get("undone_command") == "livecode.unschedule"
                   and rearmed.get("source_bytes") == len("return 'kept'"),
                   "undone=%r entry=%r" % (undone.get("undone_command"), rearmed))


def check_transactions_and_undo(session, recorder):
    """The A16 classes, the read's absence from the log, and BOTH inverses."""
    check_livecode_records(session, recorder)
    check_undo_new_schedule(session, recorder)
    check_undo_rearms_an_unschedule(session, recorder)


def check_live_edit_replaces_in_place(session, recorder):
    """Re-scheduling an id is the live edit: one slot, the new script."""
    first = session.result("livecode.schedule",
                           {"id": "lc-edit", "hook": "bar", "source": "return 'draft-1'"})
    second = session.result("livecode.schedule",
                            {"id": "lc-edit", "hook": "beat", "source": "return 'draft-22'"})
    entry = schedule_entry(clock_state(session), "lc-edit")
    ids = [one.get("id") for one in schedule_shape(clock_state(session))]
    recorder.check("the second schedule reports it REPLACED the first",
                   first.get("replaced") is False and second.get("replaced") is True,
                   "first=%r second=%r" % (first.get("replaced"), second.get("replaced")))
    recorder.check("the id now holds the new script and hook, still ONE slot",
                   entry.get("hook") == "beat"
                   and entry.get("source_bytes") == second.get("source_bytes")
                   and ids.count("lc-edit") == 1,
                   "entry=%r ids=%r" % (entry, ids))


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
        print("  %-64s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)


def run_checks(session, instance, recorder, script_path):
    check_v0_explicit_run_unchanged(session, recorder, script_path)
    check_fresh_clock_state(session, recorder)
    bars_before = check_bar_edits_without_retrigger(session, recorder)
    check_transport_edges(session, recorder)
    check_overrun_hits_the_bound(session, recorder, bars_before)
    check_refusals_are_typed(session, recorder)
    check_transactions_and_undo(session, recorder)
    check_live_edit_replaces_in_place(session, recorder)


def main(argv):
    if len(argv) < 3:
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
        run_checks(session, instance, recorder, argv[2])
        check_quit(session, instance, recorder)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("livecode.* control-surface transcript")
        return 1
    H.ok("livecode.* control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
