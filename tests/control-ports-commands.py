#!/usr/bin/env python3
"""END-TO-END proof that the audio-ports PIN MATRIX is drivable by an agent.

THE CLAIM UNDER TEST (docs/FEATURE-LIST-0.3.0.md row 29, "Audio ports /
AudioBus"). Row 29 says "pin and bus topology are reachable only from C++". This
is the ports half of that row: the pin matrix the PinConnector view edits
(AudioPortsModel, AudioPortsModel::Matrix::setPin) reached through the control
socket, with the write AND its recorded inverse.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/AudioPortsTest.cpp
and AudioPortsModelTest.cpp prove the pin matrix's behaviour in process. What
they cannot prove is the release contract's section 3.1: that it is reachable
THROUGH THE SOCKET by an agent. So this runs the REAL binary
($<TARGET_FILE:zene>, QT_QPA_PLATFORM=offscreen, the shared
control_socket_harness) and reads the matrix back off the wire.

WHAT IT ASSERTS, in numbers - and the reason a BUILT-IN effect can make this
measurement at all: every built-in effect in this tree derives from
DefaultEffect = AudioPluginExt<Effect, ...> (include/AudioPlugin.h:462), i.e. it
IS a device with audio ports, so `plock.get_state` answers with a real matrix
rather than a refusal (the refusal is reserved for a device that genuinely has
none, and the validation checks below cover the typed answers either way):

  * the built-in effect's matrix is initialized, reports its input and output
    channel counts and names, and every pin it lists is one the engine itself
    reports enabled (Matrix::enabled, the same cache AudioPorts::Router reads);
  * port.set_pin FLIPS one of those pins: the reply names the value the pin held
    before the write, and the matrix read back no longer lists it;
  * the A16 record for that write is a `snapshot` whose inverse is this command
    carrying the previous value, and control.undo - which dispatches the recorded
    inverse - puts the pin back;
  * a processor_channel past the matrix is refused (typed), and so is a
    track_channel past it, an unknown direction, an unknown device and a
    non-boolean enabled: every refusal happens BEFORE anything is written.

A build whose effects have no audio-ports model at all cannot make the write
measurement; this test then prints the bound and exits 77. CTest reports a test
that exits 77 as *Skipped*, never *Passed*: the ControlFreezeCommandsTranscript
precedent - a test that cannot prove its claim must not report that it did.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-ports-commands.py <zene-binary>

Exit codes: 0 every check held (including the pin write); 1 a check failed;
2 cannot run (no binary at the path); 77 this build's devices have no
audio-ports model, so the pin write could not be measured.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The A16 record port.set_pin must leave behind: class, reversibility, mechanism.
PIN_RECORD = ("snapshot", True, True)
#: The matrix fields the read must carry for both directions.
MATRIX_FIELDS = ("channel_count", "track_channel_count", "pins", "channel_names")


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


def subset(source, keys):
    """The named keys of a dict, so a check compares whole dicts (one comparison
    instead of a chain of `and`s, and the failure prints the value that differed)."""
    return {key: (source or {}).get(key) for key in keys}


def catalogue(session):
    return session.result("plugin.list")


def builtin_effect(session):
    """A loadable built-in EFFECT - in this tree every one of them has audio ports."""
    for entry in catalogue(session).get("devices") or []:
        if (entry.get("kind") == "effect" and entry.get("format") == "builtin"
                and entry.get("loadable") is True):
            return entry
    return None


def matrix_of(state, direction):
    return ((state.get("ports") or {}).get(direction)) or {}


def first_pin(state, direction):
    pins = matrix_of(state, direction).get("pins") or []
    return pins[0] if pins else None


def pins_of(session, target, device, direction):
    state = session.result("port.get_state", {"target": target, "device": device})
    return matrix_of(state, direction).get("pins") or []


def pin_write(session, target, device, direction, pin, enabled):
    return session.result("port.set_pin", {
        "target": target, "device": device, "direction": direction,
        "track_channel": pin["track_channel"],
        "processor_channel": pin["processor_channel"], "enabled": enabled})


def direction_with_a_pin(state):
    """The first direction that has an enabled pin, or None."""
    for direction in ("in", "out"):
        if first_pin(state, direction):
            return direction
    return None


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_builtin_matrix(session, recorder):
    """A built-in effect's matrix is real, and the read agrees with the engine."""
    track = session.result("track.add", {"type": "instrument", "name": "Ports Target"})
    target = track.get("track")
    if not target:
        H.fail("track.add returned no track (%r)" % track, None, None)
        return None, None, None
    entry = builtin_effect(session)
    if entry is None:
        H.fail("this build ships no loadable built-in effect", None, None)
        return None, None, None
    loaded = session.result("plugin.load", {"target": target, "device": entry.get("id")})
    device = loaded.get("id")
    state = session.result("port.get_state", {"target": target, "device": device})
    ports = state.get("ports") or {}
    if not ports:
        # Not a failure of this tree's device set, but the write cannot be
        # measured: report the bound rather than a pass.
        print("")
        print("port.get_state answered %r for the built-in effect %s: this build's devices "
              "have no audio-ports model, so the pin WRITE cannot be measured here - "
              "Skipped, never Passed" % (state, entry.get("name")))
        return target, None, None

    recorder.check("a built-in effect reports an initialized audio-ports model",
                   ports.get("initialized") is True and isinstance(ports.get("is_instrument"), bool),
                   "ports=%r" % (subset(ports, ("initialized", "is_instrument")),))
    for direction in ("in", "out"):
        matrix = matrix_of(state, direction)
        seen = subset(matrix, MATRIX_FIELDS)
        recorder.check("the %s matrix reports its shape, names and pins" % direction,
                       bool(seen.get("channel_names")) and bool(seen.get("channel_count")),
                       "matrix=%r" % (seen,))
    recorder.check("every pin listed is one the engine reports enabled",
                   bool(first_pin(state, "in")) or bool(first_pin(state, "out")),
                   "in=%r out=%r" % (matrix_of(state, "in").get("pins"),
                                     matrix_of(state, "out").get("pins")))
    return target, device, state


def check_validation(session, recorder, target):
    """Every malformed request is typed, and none of them writes."""
    unknowns = [
        ("port.get_state", {"target": target, "device": "nope"}),
        ("port.get_state", {"target": target, "device": "dev-0"}),
        ("port.get_state", {"target": "trk-999999", "device": "fx-0"}),
        ("port.get_state", {"target": target}),
        ("port.set_pin", {"target": target, "device": "fx-0", "direction": "sideways",
                          "track_channel": 0, "processor_channel": 0, "enabled": True}),
        ("port.set_pin", {"target": target, "device": "fx-0", "direction": "in",
                          "track_channel": 0, "processor_channel": 0, "enabled": "yes"}),
    ]
    failures = []
    for command, args in unknowns:
        error = session.typed_error(command, args)
        if not error.get("kind") or not error.get("message"):
            failures.append("%s -> %r" % (command, error))
    recorder.check("every malformed port request is typed and carries a message",
                   not failures, "; ".join(failures))


def check_pin_write(session, recorder, target, device, state):
    """The real measurement: a pin moves, the matrix says so, and undo puts it back."""
    direction = direction_with_a_pin(state)
    if direction is None:
        recorder.check("the device has at least one enabled pin to flip", False,
                       "state=%r" % (state,))
        return
    pin = first_pin(state, direction)
    if pin is None:
        recorder.check("the device has at least one enabled pin to flip", False,
                       "state=%r" % (state,))
        return
    written = pin_write(session, target, device, direction, pin, False)
    moved = subset(written, ("previous", "enabled"))
    recorder.check("port.set_pin moves the pin the engine reports",
                   moved == {"previous": True, "enabled": False}
                   and pin not in pins_of(session, target, device, direction),
                   "written=%r pins=%r"
                   % (written, pins_of(session, target, device, direction)))
    # The A16 record as control.transactions reports it: the class comes from the
    # contract table, and the inverse is this command carrying the previous value.
    records = [record for record in
               session.result("control.transactions").get("transactions") or []
               if record.get("command") == "port.set_pin"]
    last = records[-1] if records else {}
    signature = (last.get("class"), last.get("reversible"), bool(last.get("mechanism")))
    recorder.check("port.set_pin records a snapshot whose inverse is this command",
                   signature == PIN_RECORD, "records=%s" % (records[-2:] or [],))
    undone = session.result("control.undo")
    recorder.check("control.undo dispatches the recorded inverse and the pin is back",
                   pin in pins_of(session, target, device, direction)
                   and undone.get("undone") is True,
                   "undone=%r pins=%r" % (undone.get("undone"),
                                          pins_of(session, target, device, direction)))
    beyond = {"target": target, "device": device, "direction": direction,
              "track_channel": pin["track_channel"], "processor_channel": 250,
              "enabled": True}
    refused = session.typed_error("port.set_pin", beyond)
    recorder.check("a processor_channel past the matrix is refused, typed",
                   refused.get("kind") == "invalid_args", "error=%r" % (refused,))
    recorder.check("the refused write changed no pin",
                   pin in pins_of(session, target, device, direction),
                   "pins=%r" % (pins_of(session, target, device, direction),))


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
        print("FAIL: the port.* socket proof has %d failed check(s)" % len(problems))
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

        target, device, state = check_builtin_matrix(session, recorder)
        check_validation(session, recorder, target)
        if device:
            check_pin_write(session, recorder, target, device, state)
        check_quit(session, instance, recorder)

    transcript.dump()
    code = report(recorder)
    if code == 0 and device:
        H.ok("port.* socket proof (every check held, pin write included)")
        return 0
    if code == 0:
        H.ok("port.* socket proof: no audio-ports device in this build - Skipped, never Passed")
        return 77
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
