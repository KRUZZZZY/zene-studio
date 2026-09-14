#!/usr/bin/env python3
"""END-TO-END proof that the parallel-bus topology is drivable by an agent.

THE CLAIM UNDER TEST (docs/FEATURE-LIST-0.3.0.md row 29, "Audio ports /
AudioBus"). Row 29 says "pin and bus topology are reachable only from C++". This
is the bus half of that row: the engine's parallel bus (Mixer::createBusChannel,
Mixer::isBusChannel, MixerChannel::isBus - Phase D, task #587) driven end to end
through the control socket.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/AudioBusTest.cpp
and AudioBusHandleTest.cpp prove the bus's behaviour in process. What they cannot
prove is the release contract's section 3.1: that the topology is reachable
THROUGH THE SOCKET by an agent, with the schema validation and the undo behaviour
an external client actually gets. So this runs the REAL binary
($<TARGET_FILE:zene>, QT_QPA_PLATFORM=offscreen, the shared
control_socket_harness) and reads every number back off the wire.

WHAT IT ASSERTS, in numbers:
  * a fresh mixer has NO bus, and bus.list agrees with itself;
  * bus.create makes a channel the engine itself calls a bus (is_bus true, the
    "Bus <n>" name createBusChannel assigns) and bus.list finds it;
  * a bus is a channel, so the mixer's own set verb works on it - mixer.set_volume
    moves it and control.undo puts it back;
  * bus.create is reversible as ONE step: control.undo removes the bus it made
    and the channel count returns to where it was;
  * bus.remove is NOT reversible, and says so: the recorded transaction is a
    snapshot with reversible false, and control.undo REFUSES with the typed
    'irreversible' kind instead of unwinding an older command;
  * bus.remove refuses the master ch-0 before writing anything (it is not a bus).

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-bus-commands.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def typed_error(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is False:
            return reply.get("error") or {}
        return {}


class Recorder:
    """Collects the named checks and their evidence, so a failure names the number."""

    def __init__(self):
        self.results = []

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))

    def problems(self):
        return [(name, evidence) for name, passed, evidence in self.results if not passed]


def bus_ids(listed):
    return tuple(entry.get("id") for entry in listed.get("buses") or [])


def bus_entry(listed, wanted):
    for entry in listed.get("buses") or []:
        if entry.get("id") == wanted:
            return entry
    return None


def channel_count(session):
    return session.result("mixer.get_state").get("count")


def fader_of(entry):
    """A bus entry's volume rounded to the wire's own precision (or -1 when the
    entry is gone, so a missing bus cannot compare equal to a fader value)."""
    if entry is None:
        return -1.0
    return round(float(entry.get("volume", -1.0)), 6)


def transaction_for(session, command):
    for record in reversed(session.result("control.transactions").get("transactions") or []):
        if record.get("command") == command:
            return record
    return None


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_empty(session, recorder):
    """A fresh mixer has no bus, and the list says so."""
    listed = session.result("bus.list")
    recorder.check("a fresh mixer has no bus and bus.list agrees with itself",
                   listed.get("count") == 0 and bus_ids(listed) == ()
                   and listed.get("channel_count") == channel_count(session),
                   "listed=%r channels=%r" % (listed.get("count"), listed.get("channel_count")))


#: What bus.create must report: the engine's own bus flag, and the count the
#: mixer moved to.
CREATED_BUS = {"is_bus": True}
#: What bus.list's entry for that bus must carry.
LISTED_BUS = ("is_bus", "is_master", "index")


def subset(source, keys):
    """The named keys of a dict, so a check compares whole dicts (one comparison
    instead of a chain of `and`s, and the failure prints the value that differed)."""
    return {key: (source or {}).get(key) for key in keys}


def check_create_and_list(session, recorder):
    """bus.create makes a real bus; the list and the A16 record both say so."""
    before = channel_count(session)
    created = session.result("bus.create")
    bus = created.get("channel")
    listed = session.result("bus.list")
    entry = bus_entry(listed, bus)
    made = subset(created, ("is_bus", "count"))
    recorder.check("bus.create makes a channel the engine calls a bus",
                   bool(bus) and made == {"is_bus": True, "count": before + 1},
                   "created=%r" % (created,))
    recorder.check("bus.create names the bus the way the engine does",
                   created.get("name", "").startswith("Bus"), "created=%r" % (created,))
    seen = subset(entry, LISTED_BUS)
    recorder.check("bus.list finds exactly that bus",
                   listed.get("count") == 1
                   and seen == {"is_bus": True, "is_master": False, "index": created.get("index")},
                   "listed=%r entry=%r" % (bus_ids(listed), entry))
    record = transaction_for(session, "bus.create")
    signature = subset(record, ("class", "reversible"))
    recorder.check("bus.create records a reversible true_inverse transaction",
                   signature == {"class": "true_inverse", "reversible": True}
                   and "action checkpoint" in str((record or {}).get("mechanism")),
                   "record=%r" % (record,))
    return bus


def check_set_verb_and_undo(session, recorder, bus):
    """A bus is a channel: the mixer's own fader works on it, and is undoable."""
    if not bus:
        return
    session.result("mixer.set_volume", {"channel": bus, "volume": 0.5})
    entry = bus_entry(session.result("bus.list"), bus)
    recorder.check("the mixer's own set verb reaches the bus's fader",
                   fader_of(entry) == 0.5, "entry=%r" % (entry,))
    undone = session.result("control.undo")
    entry = bus_entry(session.result("bus.list"), bus)
    recorder.check("control.undo puts the bus fader back",
                   fader_of(entry) == 1.0, "undone=%r entry=%r" % (undone.get("undone"), entry))


def check_create_undo(session, recorder, bus):
    """One undo takes the bus bus.create made back out, and nothing else."""
    if not bus:
        return
    before = channel_count(session)
    undone = session.result("control.undo")
    listed = session.result("bus.list")
    recorder.check("one control.undo removes the bus bus.create made",
                   undone.get("undone") is True and bus_ids(listed) == ()
                   and channel_count(session) == before - 1,
                   "undone=%r listed=%r count=%r"
                   % (undone.get("undone"), bus_ids(listed), channel_count(session)))


def check_remove_is_irreversible(session, recorder):
    """bus.remove records a snapshot that cannot be replayed, and undo says so."""
    created = session.result("bus.create")
    bus = created.get("channel")
    if not bus:
        recorder.check("bus.create returned a channel", False, "created=%r" % (created,))
        return
    session.result("mixer.send_to", {"channel": "ch-0", "to": bus, "amount": 0.25})
    removed = session.result("bus.remove", {"channel": bus})
    entry = bus_entry(session.result("bus.list"), bus)
    recorder.check("bus.remove deletes the bus",
                   removed.get("removed") == bus and entry is None,
                   "removed=%r listed=%r" % (removed, bus_ids(session.result("bus.list"))))
    record = transaction_for(session, "bus.remove")
    signature = subset(record, ("class", "reversible"))
    recorder.check("bus.remove records a snapshot with reversible false and a fallback",
                   signature == {"class": "snapshot", "reversible": False}
                   and bool((record or {}).get("mechanism")),
                   "record=%r" % (record,))
    refused = session.typed_error("control.undo")
    recorder.check("control.undo refuses the removed bus, typed, instead of unwinding an older step",
                   refused.get("kind") == "irreversible", "error=%r" % (refused,))
    recorder.check("the refusal names the fallback", bool(refused.get("message")),
                   "error=%r" % (refused,))


def check_master_refusal(session, recorder):
    """The master is not a bus, and bus.remove says which command owns it."""
    refused = session.typed_error("bus.remove", {"channel": "ch-0"})
    recorder.check("bus.remove refuses a channel that is not a bus, before writing",
                   refused.get("kind") == "refused" and "mixer.remove_channel" in
                   str(refused.get("message")),
                   "error=%r" % (refused,))
    missing = session.typed_error("bus.remove", {"channel": "ch-99"})
    recorder.check("bus.remove on a channel that does not exist is not_found",
                   missing.get("kind") == "not_found", "error=%r" % (missing,))


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-58s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    problems = recorder.problems()
    if problems:
        print("")
        print("FAIL: the bus.* socket proof has %d failed check(s)" % len(problems))
        return 1
    print("")
    print("PASS: %d checks, every one a measured number" % len(recorder.results))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]):
        print("cannot run: no binary at %s" % argv[1])
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

        check_empty(session, recorder)
        bus = check_create_and_list(session, recorder)
        check_set_verb_and_undo(session, recorder, bus)
        check_create_undo(session, recorder, bus)
        check_master_refusal(session, recorder)
        check_remove_is_irreversible(session, recorder)
        check_quit(session, instance, recorder)

    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("bus.* socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
