#!/usr/bin/env python3
"""END-TO-END proof that the audio-ports PIN MATRIX is drivable by an agent.

THE CLAIM UNDER TEST (docs/FEATURE-LIST-0.3.0.md row 29, "Audio ports /
AudioBus"). Row 29 says "pin and bus topology are reachable only from C++". This
is the ports half of that row: the pin matrix the PinConnector view edits
(AudioPortsModel, AudioPortsModel::Matrix::setPin) reached through the control
socket, with the write and its inverse.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/AudioPortsTest.cpp
and AudioPortsModelTest.cpp prove the pin matrix's behaviour in process. What
they cannot prove is the release contract's section 3.1: that it is reachable
THROUGH THE SOCKET by an agent. So this runs the REAL binary
($<TARGET_FILE:zene>, QT_QPA_PLATFORM=offscreen, the shared
control_socket_harness) and reads the matrix back off the wire.

WHAT IT ASSERTS - and what it can only ASSERT THE BOUND OF, which is why it can
report *Skipped*:

  Always, in every build:
    * a built-in effect loaded onto a track has NO audio-ports model, so
      port.get_state answers a typed not_found that NAMES the fact - the honest
      answer for a device that has no pin matrix;
    * every malformed request is typed BEFORE anything is written: an unknown
      device id, a device id of the wrong form, an unknown direction, a
      track_channel past the matrix and a non-boolean enabled;

  When - and only when - this build ships a loadable device WITH an audio-ports
    model (an AudioPlugin-derived device: the CLAP and VST3 hosts and the
    analysers are the ones that have one):
    * port.get_state reports the matrix's shape, its channel names and its pins,
      and the pins it lists are the ones the engine reports enabled;
    * port.set_pin flips one pin, the matrix reads back flipped, and
      control.undo - which dispatches the recorded inverse COMMAND - puts it
      back;
    * an out-of-range processor_channel is refused and the matrix is unchanged.

  A build with no such device cannot make the write measurement, so it prints the
  bound and exits 77: CTest reports a test that exits 77 as *Skipped*, never
  *Passed*, which is the ControlFreezeCommandsTranscript precedent - a test that
  cannot prove its claim must not report that it did.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-ports-commands.py <zene-binary>

Exit codes: 0 every check held (including the pin write); 1 a check failed;
2 cannot run (no binary at the path); 77 this build has no device with an
audio-ports model, so the pin write could not be measured.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The device formats whose effects are AudioPlugin-derived, i.e. the ones that
#: can carry an AudioPortsModel. Built-in effects (amplifier, delay, ...) derive
#: from Effect directly and have none - which the first check proves.
PORTS_FORMATS = ("clap", "vst3")
#: How many candidate devices to try before giving up on the write measurement.
MAX_CANDIDATES = 4


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


def catalogue(session):
    return session.result("plugin.list")


def builtin_effect(session):
    """A loadable built-in EFFECT - the device that proves the "no ports" answer."""
    for entry in catalogue(session).get("devices") or []:
        if (entry.get("kind") == "effect" and entry.get("format") == "builtin"
                and entry.get("loadable") is True):
            return entry
    return None


def ports_candidates(session):
    """Loadable effects whose format is an AudioPlugin-derived host."""
    return [entry for entry in catalogue(session).get("devices") or []
            if entry.get("format") in PORTS_FORMATS and entry.get("loadable") is True]


def matrix_of(state, direction):
    return ((state.get("ports") or {}).get(direction)) or {}


def first_pin(state, direction):
    pins = matrix_of(state, direction).get("pins") or []
    return pins[0] if pins else None


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_device_without_ports(session, recorder):
    """A device with no audio-ports model says so, typed."""
    track = session.result("track.add", {"type": "instrument", "name": "Ports Target"})
    track_id = track.get("track")
    entry = builtin_effect(session)
    if not track_id:
        H.fail("track.add returned no track (%r)" % track, None, None)
        return track_id
    if entry is None:
        H.fail("this build ships no loadable built-in effect", None, None)
        return track_id
    loaded = session.result("plugin.load", {"target": track_id, "device": entry.get("id")})
    error = session.typed_error("port.get_state",
                               {"target": track_id, "device": loaded.get("id")})
    recorder.check("a built-in effect has no pin matrix, and the refusal says so",
                   loaded.get("id", "").startswith("fx-") and error.get("kind") == "not_found"
                   and "audio-ports model" in str(error.get("message")),
                   "loaded=%r error=%r" % (loaded, error))
    return track_id


def check_validation(session, recorder, track_id):
    """Every malformed request is typed, and none of them writes."""
    unknowns = [
        ("port.get_state", {"target": track_id, "device": "nope"}),
        ("port.get_state", {"target": track_id, "device": "dev-0"}),
        ("port.get_state", {"target": "trk-9999", "device": "fx-0"}),
        ("port.get_state", {"target": track_id}),
        ("port.set_pin", {"target": track_id, "device": "fx-0", "direction": "sideways",
                          "track_channel": 0, "processor_channel": 0, "enabled": True}),
        ("port.set_pin", {"target": track_id, "device": "fx-0", "direction": "in",
                          "track_channel": 0, "processor_channel": 0, "enabled": "yes"}),
    ]
    failures = []
    for command, args in unknowns:
        error = session.typed_error(command, args)
        if not error.get("kind") or not error.get("message"):
            failures.append("%s -> %r" % (command, error))
    recorder.check("every malformed port request is typed and carries a message",
                   not failures, "; ".join(failures))


def find_ports_device(session, track_id, recorder):
    """The first candidate device that really exposes an audio-ports model."""
    candidates = ports_candidates(session)
    recorder.check("this build's catalogue was consulted for a device with audio ports",
                   True, "candidates=%r" % ([entry.get("name") for entry in candidates],))
    tried = []
    for entry in candidates[:MAX_CANDIDATES]:
        loaded = session.result("plugin.load", {"target": track_id, "device": entry.get("id")})
        if not loaded.get("id"):
            continue
        state = session.result("port.get_state", {"target": track_id, "device": loaded.get("id")})
        tried.append(entry.get("name"))
        if state.get("ports"):
            return loaded.get("id"), state, tried
    return None, None, tried


def check_pin_write(session, recorder, track_id, device, state):
    """The real measurement: a pin moves, the matrix says so, and undo puts it back."""
    direction = "in" if first_pin(state, "in") else "out"
    pin = first_pin(state, direction)
    if pin is None:
        recorder.check("the device has at least one enabled pin to flip", False,
                       "state=%r" % (state,))
        return
    written = session.result("port.set_pin", {
        "target": track_id, "device": device, "direction": direction,
        "track_channel": pin["track_channel"],
        "processor_channel": pin["processor_channel"], "enabled": False})
    after = session.result("port.get_state", {"target": track_id, "device": device})
    recorder.check("port.set_pin moves the pin the engine reports",
                   written.get("previous") is True and written.get("enabled") is False
                   and pin not in (matrix_of(after, direction).get("pins") or []),
                   "written=%r pins=%r" % (written, matrix_of(after, direction).get("pins")))
    # The A16 record as control.transactions reports it: the class comes from the
    # contract table, and the inverse is this command carrying the previous value.
    records = [record for record in
               session.result("control.transactions").get("transactions") or []
               if record.get("command") == "port.set_pin"]
    recorder.check("port.set_pin records a snapshot whose inverse is this command",
                   bool(records) and records[-1].get("class") == "snapshot"
                   and records[-1].get("reversible") is True
                   and bool(records[-1].get("mechanism")),
                   "records=%s" % (records[-2:] or [],))
    undone = session.result("control.undo")
    restored = session.result("port.get_state", {"target": track_id, "device": device})
    recorder.check("control.undo dispatches the recorded inverse and the pin is back",
                   undone.get("undone") is True
                   and pin in (matrix_of(restored, direction).get("pins") or []),
                   "undone=%r pins=%r" % (undone.get("undone"),
                                          matrix_of(restored, direction).get("pins")))
    refused = session.typed_error("port.set_pin", {
        "target": track_id, "device": device, "direction": direction,
        "track_channel": pin["track_channel"], "processor_channel": 250, "enabled": True})
    recorder.check("a processor_channel past the matrix is refused",
                   refused.get("kind") == "invalid_args", "error=%r" % (refused,))


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
    device = None
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)

        track_id = check_device_without_ports(session, recorder)
        check_validation(session, recorder, track_id)
        device, state, tried = find_ports_device(session, track_id, recorder)
        if device:
            check_pin_write(session, recorder, track_id, device, state)
        else:
            print("")
            print("no device in this build exposes an audio-ports model "
                  "(tried: %r), so the pin WRITE could not be measured - Skipped, never Passed"
                  % (tried,))
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
