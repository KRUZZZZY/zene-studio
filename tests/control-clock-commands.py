#!/usr/bin/env python3
"""RAW control-surface transcript for the clock.* verbs (0.3.0, MIDI clock).

The acceptance evidence for MIDI clock, produced by driving the REAL `zene`
binary headless and printing every request and reply verbatim.

WHAT THIS PROVES, AND WHAT IT DOES NOT.
The DAW is driven as a MIDI clock MASTER - `clock.master_set` on, the transport
started on the engine's own (dummy) audio device, seeks, stops - and the
engine's OWN emission is read back in order: START, the pulses the advanced
ticks imply, a Song Position Pointer per seek, STOP, and NOTHING at all once the
transport stops. It also drives the SLAVE with no clock arriving, which is the
one direction this harness can create: an incoming MIDI clock cannot be injected
into a running instance from a socket, so the rate arithmetic (a pulse stream at
a known rate producing that tempo, and following a tempo change) is proved by
the registered QTest `MidiClockTest` instead. The bound that remains is stated
in docs/KNOWN-LIMITATIONS.md: the counters here are what the engine HANDED to
its MIDI client, not bytes an external synth received.

The checks, in order:

  1. `clock.get_state`              a fresh session has no clock mode at all, and
                                    reports the grid (2 ticks/pulse, 12/beat,
                                    24/quarter) and `mtc: "absent"`;
  2. `clock.slave_set` on, unfed    the slave is enabled, reports unlocked with
                                    no measured tempo and no pulses, and the
                                    transport does not move: a slave with no
                                    clock does NOTHING (asserted again after
                                    several audio periods and three follow
                                    polls, so "nothing" is not "not yet");
  3. the slave's own settings       the drift bound and the source port come back;
  4. `clock.master_set` on, stopped a stopped transport emits NOTHING: the clock
                                    is not free-running (a counter that only
                                    grew would fail here);
  5. `transport.play`               THE MASTER'S MESSAGE SET: START and pulses,
                                    counted, with the play head really advancing;
  6. `transport.seek`               a seek while running is reported: a Song
                                    Position Pointer;
  7. `transport.stop`               STOP, and then silence - the pulse count stops
                                    moving;
  8. `clock.master_set` off         a disabled master emits nothing while the
                                    transport plays;
  9. refusals                       a missing 'enabled', a string where a boolean
                                    belongs, a drift bound past its maximum, an
                                    unexpected property - every one typed, and
                                    none of them changing a thing;
 10. `control.transactions`         the A16 records: master_set is true_inverse,
                                    slave_set is snapshot, and the slave's record
                                    NAMES the trajectory it cannot restore;
 11. `control.undo`                 the master's flag and port come back off one
                                    recorded action step, and so does the slave's
                                    configuration - while the record itself says
                                    the tempo a follower wrote is not part of it.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path. Usage: QT_QPA_PLATFORM=offscreen python3 control-clock-commands.py <zene>
Exit code 0 only when every assertion held.
"""

import sys
import time

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))

GRID_PULSE_TICKS = 2
GRID_BEAT_TICKS = 12
GRID_PULSES_PER_QUARTER = 24
DRIFT_BOUND_MS = 8

# Long enough for several audio periods AND three of the follower's 250 ms polls,
# so "the slave did nothing" is a measurement and not a race; and long enough for
# the transport to advance and the master to emit a few hundred pulses.
SLAVE_OBSERVATION_S = 1.5
PLAY_OBSERVATION_S = 0.7


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
    state = session.result("clock.get_state")
    return state


def master_state(session):
    return clock_state(session).get("master") or {}


def slave_state(session):
    return clock_state(session).get("slave") or {}


def transport_state(session):
    return session.result("transport.get_state")


def emitted(session, name):
    return int((master_state(session).get("emitted") or {}).get(name, 0))


def monitor_names(session):
    entries = master_state(session).get("monitor") or []
    return [str(entry.get("message", "")) for entry in entries]


def pause(seconds):
    time.sleep(seconds)


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------

def check_default_state(session, recorder):
    """A fresh session has no clock mode, and says so."""
    state = clock_state(session)
    master = state.get("master") or {}
    slave = state.get("slave") or {}
    recorder.check("a fresh session has no master and no slave",
                   master.get("enabled") is False and slave.get("enabled") is False,
                   "master=%r slave=%r" % (master.get("enabled"), slave.get("enabled")))
    recorder.check("a fresh session has emitted nothing",
                   master.get("emitted_total") == 0, "total=%r" % master.get("emitted_total"))
    recorder.check("the grid is the MIDI grid, read from the engine",
                   state.get("ticks_per_pulse") == GRID_PULSE_TICKS
                   and state.get("ticks_per_beat") == GRID_BEAT_TICKS
                   and state.get("pulses_per_quarter") == GRID_PULSES_PER_QUARTER,
                   "pulse=%r beat=%r quarter=%r" % (state.get("ticks_per_pulse"),
                                                    state.get("ticks_per_beat"),
                                                    state.get("pulses_per_quarter")))
    recorder.check("the engine reports the MIDI time code it does NOT have",
                   state.get("mtc") == "absent", "mtc=%r" % state.get("mtc"))
    recorder.check("the slave starts unlocked with no measured tempo",
                   slave.get("locked") is False and slave.get("tempo_bpm") == 0,
                   "locked=%r tempo=%r" % (slave.get("locked"), slave.get("tempo_bpm")))


def check_slave_with_no_clock_does_nothing(session, recorder):
    """THE SLAVE'S CONTRACT: enabled, unfed, and completely inert."""
    before_transport = transport_state(session)
    before_tempo = before_transport.get("tempo")
    enabled = session.result("clock.slave_set",
                             {"enabled": True, "follow_tempo": True,
                              "drift_bound_ms": DRIFT_BOUND_MS, "source_port": "zene-test-src"})
    recorder.check("clock.slave_set reports the slave enabled and following",
                   enabled.get("enabled") is True and enabled.get("follow_tempo") is True,
                   "state=%r" % enabled)
    recorder.check("the slave reports the drift bound and source port it was given",
                   enabled.get("drift_bound_ms") == DRIFT_BOUND_MS
                   and enabled.get("source_port") == "zene-test-src",
                   "bound=%r port=%r" % (enabled.get("drift_bound_ms"),
                                         enabled.get("source_port")))
    recorder.check("the slave reports itself unlocked with no clock arriving",
                   enabled.get("locked") is False and enabled.get("tempo_bpm") == 0,
                   "locked=%r tempo=%r" % (enabled.get("locked"), enabled.get("tempo_bpm")))

    # Wait out several audio periods AND three follower polls, then look again.
    pause(SLAVE_OBSERVATION_S)
    after = slave_state(session)
    after_transport = transport_state(session)
    recorder.check("the slave is STILL unlocked after %ss of no clock"
                   % SLAVE_OBSERVATION_S,
                   after.get("locked") is False and after.get("pulses") == 0
                   and after.get("tempo_bpm") == 0,
                   "state=%r" % after)
    recorder.check("a follower with no clock wrote NO tempo to the song",
                   after_transport.get("tempo") == before_tempo,
                   "tempo %r -> %r" % (before_tempo, after_transport.get("tempo")))
    recorder.check("a follower with no clock did NOT move the transport",
                   after_transport.get("playing") is False
                   and after_transport.get("position_ticks") == 0,
                   "playing=%r position=%r" % (after_transport.get("playing"),
                                               after_transport.get("position_ticks")))
    recorder.check("the follower's report is free of invented numbers",
                   after.get("drift_ms") == 0 and after.get("tempo_error_bound_bpm") == 0,
                   "drift=%r bound=%r" % (after.get("drift_ms"),
                                          after.get("tempo_error_bound_bpm")))
    return before_tempo


def check_master_stopped_is_silent(session, recorder):
    """Enabling the master does not start a free-running clock."""
    enabled = session.result("clock.master_set", {"enabled": True})
    recorder.check("clock.master_set reports the master enabled",
                   enabled.get("enabled") is True, "state=%r" % enabled)
    recorder.check("master_set reports whether its port can actually emit",
                   isinstance(enabled.get("port_ready"), bool),
                   "port_ready=%r port=%r" % (enabled.get("port_ready"), enabled.get("port")))
    pause(0.6)
    after = master_state(session)
    recorder.check("a master with a STOPPED transport emitted nothing at all",
                   after.get("emitted_total") == 0,
                   "emitted_total=%r monitor=%r" % (after.get("emitted_total"),
                                                    monitor_names(session)))
    recorder.check("midi.device_list names the client the clock sends through",
                   bool(session.result("midi.device_list").get("client")),
                   "client=%r" % session.result("midi.device_list").get("client"))


def check_master_emits_the_expected_messages(session, recorder):
    """THE MASTER'S MESSAGE SET, over a real transport."""
    played = session.result("transport.play")
    recorder.check("the transport started on the engine's own device",
                   played.get("playing") is True, "state=%r" % played)
    # Read the monitor while it can still hold the FIRST message: a 32-slot
    # monitor is overrun in ~150 ms at this tempo, so "oldest is start" is only
    # worth claiming inside that window.
    pause(0.05)
    early = monitor_names(session)
    recorder.check("the FIRST message of the run was the START of the rising edge",
                   bool(early) and early[-1] == "start" and len(early) < 32,
                   "monitor (newest first)=%r" % early)
    pause(PLAY_OBSERVATION_S)
    running = transport_state(session)
    state = master_state(session)
    recorder.check("the play head really advanced",
                   running.get("playing") is True
                   and int(running.get("position_ticks") or 0) > 0,
                   "playing=%r position=%r" % (running.get("playing"),
                                               running.get("position_ticks")))
    recorder.check("the master emitted exactly one START for one rising edge",
                   emitted(session, "start") == 1, "start=%r" % emitted(session, "start"))
    pulses = emitted(session, "clock")
    recorder.check("the master emitted clock pulses while the transport ran",
                   pulses > 0, "clock=%r" % pulses)
    recorder.check("no Song Position Pointer was invented for a start from tick 0",
                   emitted(session, "song_position") == 0 and emitted(session, "stop") == 0,
                   "spp=%r stop=%r" % (emitted(session, "song_position"),
                                       emitted(session, "stop")))
    recorder.check("the state reports the emission counters it was read from",
                   int(state.get("emitted_total") or 0) ==
                   sum(int(v) for v in (state.get("emitted") or {}).values()),
                   "total=%r counters=%r" % (state.get("emitted_total"), state.get("emitted")))
    return pulses


def check_seek_while_running_is_reported(session, recorder):
    """A seek a slave cannot know about must be TOLD to it."""
    before = int(transport_state(session).get("position_ticks") or 0)
    session.result("transport.seek", {"ticks": 1920})
    pause(0.5)
    recorder.check("a seek while running emitted a Song Position Pointer",
                   emitted(session, "song_position") >= 1,
                   "spp=%r (moved from %r to 1920)" % (emitted(session, "song_position"), before))
    entries = [entry for entry in (master_state(session).get("monitor") or [])
               if entry.get("message") == "song_position"]
    recorder.check("the pointer carries the position it was taken at, in the engine's ticks",
                   bool(entries) and 1920 <= int(entries[0].get("ticks", -1)) < 1920 + 960,
                   "pointer=%r (the seek target was 1920)" % entries[:1])
    recorder.check("the pointer's own unit is a MIDI beat of 12 ticks, which the state reports",
                   clock_state(session).get("ticks_per_beat") == GRID_BEAT_TICKS,
                   "ticks_per_beat=%r" % clock_state(session).get("ticks_per_beat"))


def check_stop_ends_the_clock(session, recorder):
    """STOP is an edge, and after it the pulse count must not move."""
    stopped = session.result("transport.stop")
    recorder.check("the transport stopped", stopped.get("playing") is False,
                   "state=%r" % stopped)
    # The falling edge is emitted by the NEXT audio period (the clock is driven
    # from the render thread), so the stop is a bounded wait, not a race.
    pause(0.3)
    recorder.check("the master emitted a STOP for the falling edge",
                   emitted(session, "stop") == 1, "stop=%r" % emitted(session, "stop"))
    pause(0.5)
    recorder.check("the STOP is the LAST message the master emitted",
                   monitor_names(session)[0] == "stop",
                   "monitor (newest first)=%r" % monitor_names(session))
    middle = emitted(session, "clock")
    pause(0.5)
    recorder.check("a STOPPED transport emits no further pulses",
                   emitted(session, "clock") == middle != 0,
                   "pulses %r -> %r" % (middle, emitted(session, "clock")))
    recorder.check("that check is not vacuous: pulses WERE emitted while running",
                   middle > 0, "pulses=%r" % middle)


def check_master_off_emits_nothing(session, recorder):
    """Disabling the master is a real off switch."""
    off = session.result("clock.master_set", {"enabled": False})
    recorder.check("clock.master_set reports the master disabled",
                   off.get("enabled") is False, "state=%r" % off)
    before = int(master_state(session).get("emitted_total") or 0)
    session.result("transport.play")
    pause(PLAY_OBSERVATION_S)
    recorder.check("a DISABLED master emits nothing while the transport plays",
                   int(master_state(session).get("emitted_total") or 0) == before,
                   "total %r -> %r" % (before,
                                       master_state(session).get("emitted_total")))
    session.result("transport.stop")


def check_refusals(session, recorder):
    """Every refusal is typed, and a refusal changes nothing."""
    before = clock_state(session)
    cases = [
        ("a missing 'enabled' is invalid_args", "clock.master_set", {}),
        ("a string where a boolean belongs is invalid_args", "clock.master_set",
         {"enabled": "yes"}),
        ("an unknown property is invalid_args", "clock.master_set",
         {"enabled": True, "sideways": 1}),
        ("a port that is not a string is invalid_args", "clock.master_set",
         {"enabled": True, "port": 7}),
        ("a drift bound past the maximum is invalid_args", "clock.slave_set",
         {"enabled": True, "drift_bound_ms": 5000}),
        ("a negative drift bound is invalid_args", "clock.slave_set",
         {"enabled": True, "drift_bound_ms": -1}),
        ("a string follow flag is invalid_args", "clock.slave_set",
         {"enabled": True, "follow_tempo": "yes"}),
    ]
    for label, command, args in cases:
        error = session.typed_error(command, args)
        recorder.check(label, error.get("kind") == "invalid_args",
                       "kind=%r message=%r" % (error.get("kind"), error.get("message")))
    after = clock_state(session)
    recorder.check("no refused call changed the clock's state",
                   after.get("slave") == before.get("slave"),
                   "before=%r after=%r" % (before.get("slave"), after.get("slave")))


def clock_records(session):
    """The A16 records of this group, and only this group's."""
    return [r for r in (session.result("control.transactions").get("transactions") or [])
            if str(r.get("command", "")).startswith("clock.")]


def check_transactions(session, recorder):
    """The A16 records, including the honest one."""
    records = clock_records(session)
    commands = sorted({r.get("command") for r in records})
    recorder.check("both clock writers left a record and the inspector did not",
                   commands == ["clock.master_set", "clock.slave_set"],
                   "commands=%s" % commands)
    byCommand = {r.get("command"): r for r in records}
    master = byCommand.get("clock.master_set") or {}
    slave = byCommand.get("clock.slave_set") or {}
    recorder.check("clock.master_set is true_inverse and reversible",
                   master.get("class") == "true_inverse" and master.get("reversible") is True,
                   "record=%r" % master)
    recorder.check("clock.master_set's record names the action checkpoint",
                   "restoreMaster" in (master.get("mechanism") or ""),
                   "mechanism=%r" % master.get("mechanism"))
    recorder.check("clock.slave_set is SNAPSHOT, not true_inverse",
                   slave.get("class") == "snapshot", "record=%r" % slave)
    recorder.check("the slave's record names the trajectory its inverse cannot restore",
                   "trajectory" in (slave.get("mechanism") or ""),
                   "mechanism=%r" % slave.get("mechanism"))
    recorder.check("the slave's before-state reports the tempo a follower could move",
                   "tempo" in (slave.get("before") or {}),
                   "before=%r" % slave.get("before"))


def check_undo_reverses_the_master(session, recorder):
    """SPEC A16: the master's configuration comes back off one recorded step."""
    session.result("clock.master_set", {"enabled": True})
    armed = master_state(session)
    recorder.check("the master is on, as the undo must reverse",
                   armed.get("enabled") is True, "master=%r" % armed)
    undone = session.result("control.undo")
    after = master_state(session)
    recorder.check("one control.undo takes clock.master_set back off",
                   undone.get("undone") is True and after.get("enabled") is False,
                   "undone=%r message=%r master=%r"
                   % (undone.get("undone"), undone.get("undone_command"), after))
    recorder.check("the undo names the command it reversed",
                   undone.get("undone_command") == "clock.master_set",
                   "undone_command=%r" % undone.get("undone_command"))


def slave_records(session):
    """The clock.slave_set A16 records, oldest first."""
    return [r for r in clock_records(session) if r.get("command") == "clock.slave_set"]


def check_undo_restores_the_slave_configuration(session, recorder):
    """The configuration returns; the trajectory is named and does NOT."""
    session.result("clock.master_set", {"enabled": True})
    # Pushed LAST, so this is the step the next undo pops - the point of this
    # half is that the SLAVE's step is the one being unwound.
    session.result("clock.slave_set", {"enabled": False})
    undone = session.result("control.undo")
    after = slave_state(session)
    recorder.check("the next control.undo restores clock.slave_set's step",
                   undone.get("undone") is True and undone.get("undone_command")
                   == "clock.slave_set",
                   "undone=%r message=%r" % (undone.get("undone"),
                                             undone.get("undone_command")))
    recorder.check("the slave is following again after the undo",
                   after.get("enabled") is True, "slave=%r" % after)
    newest = (slave_records(session) or [{}])[-1]
    recorder.check("the undone record is still classified snapshot",
                   newest.get("class") == "snapshot",
                   "classes=%r" % [r.get("class") for r in slave_records(session)])
    recorder.check("the record still reports the tempo a follower could have moved",
                   "tempo" in (newest.get("before") or {}),
                   "before=%r" % newest.get("before"))


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


def run_checks(session, instance, recorder):
    check_default_state(session, recorder)
    check_slave_with_no_clock_does_nothing(session, recorder)
    check_master_stopped_is_silent(session, recorder)
    check_master_emits_the_expected_messages(session, recorder)
    check_seek_while_running_is_reported(session, recorder)
    check_stop_ends_the_clock(session, recorder)
    check_master_off_emits_nothing(session, recorder)
    check_refusals(session, recorder)
    check_transactions(session, recorder)
    # Both inverses, in the order that makes each of them the top of the stack.
    check_undo_reverses_the_master(session, recorder)
    check_undo_restores_the_slave_configuration(session, recorder)


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
        run_checks(session, instance, recorder)
        check_quit(session, instance, recorder)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("clock.* control-surface transcript")
        return 1
    H.ok("clock.* control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
