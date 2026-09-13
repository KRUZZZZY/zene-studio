#!/usr/bin/env python3
"""RAW control-surface transcript for the transport.punch_* verbs (0.3.0).

The acceptance evidence for punch in/out, produced by driving the REAL `lmms`
binary headless and printing every request and reply verbatim.

What it drives, in order:

  1. `transport.punch_get_state`      a fresh session has NO region: unarmed,
                                      empty range, and the gate says no;
  2. `transport.punch_set`            the region is set and armed, and
                                      `punch_active` - the gate's own answer at
                                      the current play position - is FALSE
                                      because the play head is still at tick 0,
                                      outside [192, 384);
  3. `transport.seek` + get_state     THE GATE, at four positions: inside the
                                      region true, at `end` false (the range is
                                      half-open), at `start` true, past the end
                                      false. This is the real effect the
                                      command exists for, asserted on boundaries
                                      rather than on "the call succeeded";
  4. `transport.punch_set` again      the range is KEPT when the region is
                                      disarmed (`enabled: false`), which is the
                                      difference between a range and an arm;
  5. `project.save`                   the region is PROJECT STATE: the saved
                                      <timeline> element carries punch0pos /
                                      punch1pos / punchstate (read out of the
                                      file, not trusted from the reply);
  6. `project.open`                   the region survives the round trip: the
                                      same range, the same arm flag, and the
                                      gate answers the same way after the reload;
  7. `transport.punch_clear` +
     `project.save`                   a session that never punched writes NO
                                      punch attributes at all (the additive rule
                                      the tempo map and the modulation layer
                                      follow);
  8. `transport.punch_set` +
     `control.undo`                   SPEC A16: the region comes back off the
                                      Timeline's own checkpoint, INCLUDING the
                                      first punch of a project - which is what
                                      the reset-on-absence rule in
                                      Timeline::loadSettings is for;
  9. refusals                         an inverted range, a missing end, a
                                      negative start, an end past the timeline,
                                      clearing nothing - every one typed;
 10. `control.transactions`           the A16 records: the two writers are
                                      true_inverse and reversible, the inspector
                                      leaves no record at all.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path.

Usage: QT_QPA_PLATFORM=offscreen python3 control-punch-transcript.py <lmms>
Exit code 0 only when every assertion held.
"""

import os
import sys

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))

# The region every check uses: bar 2 to bar 3 at 4/4 (192 ticks to the bar).
PUNCH_START = 192
PUNCH_END = 384


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


def punch_state(session):
    return session.result("transport.punch_get_state")


def seek(session, ticks):
    session.result("transport.seek", {"ticks": ticks})


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------

def check_default_state(session, recorder):
    """A fresh session has no punch region, and the gate says so."""
    state = punch_state(session)
    recorder.check("a fresh session has no punch region",
                   state.get("punch_begin") == 0 and state.get("punch_end") == 0,
                   "state=%r" % state)
    recorder.check("a fresh session's region is unarmed and inactive",
                   state.get("punch_enabled") is False and state.get("punch_armed") is False
                   and state.get("punch_active") is False,
                   "enabled=%r armed=%r active=%r" % (state.get("punch_enabled"),
                                                      state.get("punch_armed"),
                                                      state.get("punch_active")))


def check_set(session, recorder):
    """The region is set, armed and reported; the gate is position-dependent."""
    set_reply = session.result("transport.punch_set",
                               {"start": PUNCH_START, "end": PUNCH_END})
    recorder.check("transport.punch_set reports the range it was given",
                   set_reply.get("punch_begin") == PUNCH_START
                   and set_reply.get("punch_end") == PUNCH_END,
                   "begin=%r end=%r" % (set_reply.get("punch_begin"), set_reply.get("punch_end")))
    recorder.check("transport.punch_set arms the region",
                   set_reply.get("punch_armed") is True
                   and set_reply.get("punch_enabled") is True,
                   "armed=%r enabled=%r" % (set_reply.get("punch_armed"),
                                            set_reply.get("punch_enabled")))
    # The play head is still where it started, so the gate must answer NO even
    # though the region is armed: the two are different questions.
    readback = punch_state(session)
    recorder.check("the gate says NO from outside the armed region",
                   readback.get("punch_active") is False
                   and readback.get("position_ticks") < PUNCH_START,
                   "active=%r position=%r" % (readback.get("punch_active"),
                                              readback.get("position_ticks")))
    return readback


def check_the_gate_at_positions(session, recorder):
    """THE REAL EFFECT: the gate flips with the transport position."""
    ask = [(PUNCH_START, True, "at `start` (the range is half-open, so begin is inside)"),
           (PUNCH_START + 96, True, "inside the region"),
           (PUNCH_END - 1, True, "the last tick inside"),
           (PUNCH_END, False, "at `end` (exclusive)"),
           (PUNCH_END + 1, False, "past the end"),
           (0, False, "before the region")]
    for ticks, expected, label in ask:
        seek(session, ticks)
        state = punch_state(session)
        recorder.check("the gate says %s %s" % ("YES" if expected else "NO", label),
                       state.get("punch_active") is expected
                       and state.get("position_ticks") == ticks,
                       "ticks=%r active=%r reported_position=%r"
                       % (ticks, state.get("punch_active"), state.get("position_ticks")))


def check_disarm_keeps_the_range(session, recorder):
    """A range without an arm is a range, not a region: the gate is off."""
    seek(session, PUNCH_START + 10)
    disarmed = session.result("transport.punch_set",
                              {"start": PUNCH_START, "end": PUNCH_END, "enabled": False})
    recorder.check("disarming keeps the range and reports it unarmed",
                   disarmed.get("punch_begin") == PUNCH_START
                   and disarmed.get("punch_end") == PUNCH_END
                   and disarmed.get("punch_armed") is False,
                   "begin=%r end=%r armed=%r" % (disarmed.get("punch_begin"),
                                                 disarmed.get("punch_end"),
                                                 disarmed.get("punch_armed")))
    recorder.check("an unarmed range captures nothing, even inside it",
                   disarmed.get("punch_active") is False,
                   "active=%r position=%r" % (disarmed.get("punch_active"),
                                              disarmed.get("position_ticks")))
    session.result("transport.punch_set", {"start": PUNCH_START, "end": PUNCH_END})


def read_saved(path):
    with open(path, errors="replace") as handle:
        return handle.read()


def check_save(session, recorder, outdir):
    """The region is project state: the saved file carries it."""
    saved_path = os.path.join(outdir, "punched.mmp")
    saved = session.result("project.save", {"path": saved_path})
    recorder.check("project.save wrote the punched session",
                   saved.get("file") == saved_path and os.path.exists(saved_path),
                   "file=%r" % saved.get("file"))
    if not os.path.exists(saved_path):
        return saved_path
    text = read_saved(saved_path)
    recorder.check("the saved project carries the punch region in the timeline XML",
                   "punch0pos" in text and "punch1pos" in text and "punchstate" in text,
                   "punch0pos=%r punchstate=%r"
                   % ("punch0pos" in text, "punchstate" in text))
    return saved_path


def check_load_round_trip(session, recorder, saved_path):
    """THE ROUND TRIP: what was saved comes back, and the gate works after it."""
    # Move the region somewhere else first, so a load that did nothing at all
    # could not pass this check by leaving the state as it was.
    session.result("transport.punch_clear")
    session.result("transport.punch_set", {"start": 0, "end": 96})
    cleared = punch_state(session)
    recorder.check("the session really moved away from the saved region",
                   cleared.get("punch_begin") == 0 and cleared.get("punch_end") == 96,
                   "begin=%r end=%r" % (cleared.get("punch_begin"), cleared.get("punch_end")))

    reopened = session.result("project.open", {"path": saved_path})
    recorder.check("project.open reloads the punched session",
                   reopened.get("file") == saved_path, "file=%r" % reopened.get("file"))
    state = punch_state(session)
    recorder.check("the punch region survives save/load",
                   state.get("punch_begin") == PUNCH_START and state.get("punch_end") == PUNCH_END
                   and state.get("punch_enabled") is True and state.get("punch_armed") is True,
                   "state=%r" % state)
    seek(session, PUNCH_START + 5)
    inside = punch_state(session)
    recorder.check("the gate still answers YES inside the reloaded region",
                   inside.get("punch_active") is True, "active=%r" % inside.get("punch_active"))
    seek(session, PUNCH_END + 5)
    outside = punch_state(session)
    recorder.check("the gate still answers NO outside the reloaded region",
                   outside.get("punch_active") is False, "active=%r" % outside.get("punch_active"))


def check_a_project_that_never_punched(session, recorder, outdir):
    """The additive rule: no region means no punch attributes in the file."""
    cleared = session.result("transport.punch_clear")
    recorder.check("transport.punch_clear disarms and empties the region",
                   cleared.get("punch_begin") == 0 and cleared.get("punch_end") == 0
                   and cleared.get("punch_armed") is False,
                   "state=%r" % cleared)
    plain_path = os.path.join(outdir, "never-punched.mmp")
    session.result("project.save", {"path": plain_path})
    recorder.check("a session that never punched writes a project file",
                   os.path.exists(plain_path), "path=%r" % plain_path)
    if not os.path.exists(plain_path):
        return
    text = read_saved(plain_path)
    recorder.check("a project that never punched carries NO punch attributes",
                   "punch0pos" not in text and "punch1pos" not in text and "punchstate" not in text,
                   "punch0pos=%r punchstate=%r" % ("punch0pos" in text, "punchstate" in text))
    recorder.check("the timeline element is still in the file (the check above is not vacuous)",
                   "<timeline" in text, "timeline element present=%r" % ("<timeline" in text))


def check_undo(session, recorder, outdir):
    """SPEC A16: the Timeline's own checkpoint takes the region back off."""
    session.result("transport.punch_set", {"start": PUNCH_START, "end": PUNCH_END})
    before = punch_state(session)
    undone = session.result("control.undo")
    after = punch_state(session)
    recorder.check("the region was armed before the undo",
                   before.get("punch_armed") is True, "armed=%r" % before.get("punch_armed"))
    recorder.check("one control.undo takes the FIRST punch region back off",
                   undone.get("undone") is True and after.get("punch_end") == 0
                   and after.get("punch_armed") is False,
                   "undone=%r state=%r" % (undone.get("undone"), after))
    # The undo's own restore is the reset-on-absence path: an empty range with no
    # attribute is what Timeline::loadSettings must translate back to "no region".
    saved_path = os.path.join(outdir, "undone.mmp")
    session.result("project.save", {"path": saved_path})
    text = read_saved(saved_path) if os.path.exists(saved_path) else ""
    recorder.check("the undone region leaves the session with no punch attribute",
                   text and "punch0pos" not in text,
                   "punch0pos present=%r" % ("punch0pos" in text))


def check_refusals(session, recorder):
    """Every refusal is typed, and a refusal writes nothing."""
    inverted = session.typed_error("transport.punch_set", {"start": PUNCH_END, "end": PUNCH_START})
    recorder.check("a range whose end is not past its start is invalid_args",
                   inverted.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (inverted.get("kind"), inverted.get("message")))
    empty = session.typed_error("transport.punch_set", {"start": 100, "end": 100})
    recorder.check("an empty range is invalid_args (it captures nothing)",
                   empty.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (empty.get("kind"), empty.get("message")))
    negative = session.typed_error("transport.punch_set", {"start": -1, "end": 96})
    recorder.check("a negative start is invalid_args", negative.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (negative.get("kind"), negative.get("message")))
    half = session.typed_error("transport.punch_set", {"start": 96})
    recorder.check("a range missing its end is invalid_args", half.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (half.get("kind"), half.get("message")))
    huge = session.typed_error("transport.punch_set",
                               {"start": 0, "end": 9999 * 192 + 1})
    recorder.check("an end past the timeline is invalid_args", huge.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (huge.get("kind"), huge.get("message")))
    rejected = session.typed_error("transport.punch_set", {"start": "192", "end": PUNCH_END})
    recorder.check("a string tick position is invalid_args", rejected.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (rejected.get("kind"), rejected.get("message")))
    state = punch_state(session)
    recorder.check("no refused call left a region behind",
                   state.get("punch_end") == 0, "state=%r" % state)
    nothing = session.typed_error("transport.punch_clear")
    recorder.check("clearing when there is no region is refused",
                   nothing.get("kind") == "refused",
                   "kind=%r message=%r" % (nothing.get("kind"), nothing.get("message")))


def punch_records(session):
    """The A16 records of this group, and only this group's."""
    return [r for r in (session.result("control.transactions").get("transactions") or [])
            if str(r.get("command", "")).startswith("transport.punch")]


def every_record_is_true_inverse(records):
    return (all(r.get("class") == "true_inverse" for r in records)
            and all(r.get("reversible") is True for r in records))


def every_record_names_a_timeline_checkpoint(records):
    return all("Timeline checkpoint" in (r.get("mechanism") or "") for r in records)


def check_transactions(session, recorder):
    """The A16 records: two writers, one inspector, no pretending."""
    records = punch_records(session)
    commands = sorted({r.get("command") for r in records})
    recorder.check("both punch writers left a record and the inspector did not",
                   commands == ["transport.punch_clear", "transport.punch_set"],
                   "commands=%s" % commands)
    recorder.check("every punch record is true_inverse and reversible",
                   bool(records) and every_record_is_true_inverse(records),
                   "classes=%s" % [r.get("class") for r in records])
    recorder.check("the mechanism names the Timeline checkpoint",
                   bool(records) and every_record_names_a_timeline_checkpoint(records),
                   "mechanisms=%s" % [r.get("mechanism") for r in records][:2])


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


def run_checks(session, instance, recorder, outdir):
    check_default_state(session, recorder)
    check_set(session, recorder)
    check_the_gate_at_positions(session, recorder)
    check_disarm_keeps_the_range(session, recorder)
    saved_path = check_save(session, recorder, outdir)
    check_load_round_trip(session, recorder, saved_path)
    check_a_project_that_never_punched(session, recorder, outdir)
    check_undo(session, recorder, outdir)
    check_refusals(session, recorder)
    check_transactions(session, recorder)


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
        run_checks(session, instance, recorder, instance.tmp)
        check_quit(session, instance, recorder)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("transport.punch_* control-surface transcript")
        return 1
    H.ok("transport.punch_* control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
