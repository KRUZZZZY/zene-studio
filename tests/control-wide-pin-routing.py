#!/usr/bin/env python3
"""Q050-155.2: 64 channel pins and a mixer route on discrete-64 buses, over the control socket.

THE CLAIM UNDER TEST. A device in a mixer channel set to discrete-N addresses all N channels by index through
its pins, and the agent configures it with the registered commands and reads it back:

  * two channels set to discrete-64 (mixer.channel_format), the first routed into the second (mixer.route_to);
    mixer.get_state and routing.matrix both report that route as carrying 64 channels by index, and one
    control.undo takes the route back;
  * 32 of the build's amplifiers on the first channel, amplifier k pinned (port.set_pins) onto channels 2k and
    2k + 1, input and output: 64 channel pins. port.get_state reads every device back on its own pair, and its
    `bus` says the device addresses 64 channels;
  * one control.undo moves only the last device back to the pair it came from - one step, nothing else moved;
  * every refusal (a channel past the bus, a channel past a stereo bus) is typed and writes nothing.

The REAL binary ($<TARGET_FILE:zene>, QT_QPA_PLATFORM=offscreen, the shared control_socket_harness). The render
half of the unit - that the pinned devices and the route keep every channel's index - is
tests/src/core/WidePinRoutingTest.cpp.

Usage: control-wide-pin-routing.py <path-to-zene>
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

PAIRS = 32


class Session:
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


class Recorder:
    def __init__(self):
        self.results = []

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))


def onto(first):
    """port.set_pins' list moving a stereo device from the pair onto channels first, first + 1."""
    pins = []
    for direction in ("in", "out"):
        pins += [{"direction": direction, "track_channel": 0, "processor_channel": 0, "enabled": False},
                 {"direction": direction, "track_channel": 1, "processor_channel": 1, "enabled": False},
                 {"direction": direction, "track_channel": first, "processor_channel": 0, "enabled": True},
                 {"direction": direction, "track_channel": first + 1, "processor_channel": 1, "enabled": True}]
    return pins


def pins_of(state, direction):
    return [(p.get("track_channel"), p.get("processor_channel"))
            for p in ((state.get("ports") or {}).get(direction) or {}).get("pins") or []]


def send_from(session, command, key, source, receiver):
    for channel in session.result(command).get("channels") or []:
        if channel.get(key) != source:
            continue
        for send in channel.get("sends") or []:
            if send.get("to") == receiver:
                return send
    return {}


def amplifier(session):
    for entry in session.result("plugin.list", {"kind": "effect"}).get("devices") or []:
        if entry.get("name") == "amplifier" and entry.get("loadable") is True:
            return entry.get("id")
    return None


def check_route(session, recorder, a, b):
    for channel in (a, b):
        reply = session.result("mixer.channel_format", {"channel": channel, "format": "discrete-64"})
        recorder.check("%s is set to discrete-64" % channel, reply.get("format") == "discrete-64", "%r" % reply)
    routed = session.result("mixer.route_to", {"channel": a, "to": b})
    recorder.check("mixer.route_to routes the first bus into the second", routed.get("to") == b, "%r" % routed)
    for command, key in (("mixer.get_state", "id"), ("routing.matrix", "channel")):
        send = send_from(session, command, key, a, b)
        recorder.check("%s reports the route carrying 64 channels by index" % command,
                       send.get("channels") == 64 and send.get("channel_routing") == "index", "%r" % send)
    undone = session.result("control.undo")
    gone = send_from(session, "routing.matrix", "channel", a, b)
    recorder.check("one control.undo takes the route back", undone.get("undone") is True and not gone,
                   "undo=%r send=%r" % (undone, gone))
    again = session.result("mixer.route_to", {"channel": a, "to": b})
    recorder.check("the route is made again", again.get("to") == b, "%r" % again)


def load_and_pin(session, recorder, a, device):
    devices = []
    for k in range(PAIRS):
        fx = session.result("plugin.load", {"target": a, "device": device}).get("id")
        devices.append(fx)
        reply = session.result("port.set_pins", {"target": a, "device": fx, "pins": onto(2 * k)})
        if "error" in reply:
            recorder.check("port.set_pins pins amplifier %d onto channels %d-%d" % (k, 2 * k + 1, 2 * k + 2),
                           False, "%r" % reply)
    return devices


def misplaced(state, k):
    want = [(2 * k, 0), (2 * k + 1, 1)]
    bus = state.get("bus") or {}
    good = pins_of(state, "in") == want and pins_of(state, "out") == want
    good = good and bus.get("channels") == 64 and bus.get("reached_track_channels") == 64
    return None if good else (k, pins_of(state, "in"), pins_of(state, "out"), bus)


def check_pins(session, recorder, a, device):
    devices = load_and_pin(session, recorder, a, device)
    out_channels = set()
    wrong = []
    for k, fx in enumerate(devices):
        state = session.result("port.get_state", {"target": a, "device": fx})
        wrong += [w for w in [misplaced(state, k)] if w]
        out_channels |= {tc for tc, _ in pins_of(state, "out")}
    recorder.check("port.get_state reads every amplifier back on its own pair of a 64-channel bus",
                   not wrong and len(devices) == PAIRS, "wrong=%r" % wrong[:3])
    recorder.check("the 32 devices' pins cover the 64 channels exactly once",
                   sorted(out_channels) == list(range(64)), "%r" % sorted(out_channels))
    return devices


def check_undo(session, recorder, a, devices):
    undone = session.result("control.undo")
    last = session.result("port.get_state", {"target": a, "device": devices[-1]})
    before_last = session.result("port.get_state", {"target": a, "device": devices[-2]})
    recorder.check("one control.undo puts only the last device back on the pair",
                   undone.get("undone") is True and pins_of(last, "out") == [(0, 0), (1, 1)]
                   and pins_of(last, "in") == [(0, 0), (1, 1)]
                   and pins_of(before_last, "out") == [(60, 0), (61, 1)],
                   "undo=%r last=%r before=%r" % (undone, pins_of(last, "out"), pins_of(before_last, "out")))


def check_refusals(session, recorder, a, c, device, devices):
    fx = devices[0]
    past = session.call("port.set_pins", {"target": a, "device": fx, "pins": [
        {"direction": "out", "track_channel": 64, "processor_channel": 0, "enabled": True}]})
    still = session.result("port.get_state", {"target": a, "device": fx})
    recorder.check("a channel past the 64-channel bus is refused, typed, and writes nothing",
                   past.get("ok") is False and (past.get("error") or {}).get("kind") == "invalid_args"
                   and pins_of(still, "out") == [(0, 0), (1, 1)], "%r" % past)
    stereo = session.result("plugin.load", {"target": c, "device": device}).get("id")
    narrow = session.call("port.set_pins", {"target": c, "device": stereo, "pins": onto(2)})
    state = session.result("port.get_state", {"target": c, "device": stereo})
    recorder.check("a stereo channel's device cannot be pinned past its pair",
                   narrow.get("ok") is False and (state.get("bus") or {}).get("channels") == 2
                   and pins_of(state, "out") == [(0, 0), (1, 1)], "%r %r" % (narrow, state.get("bus")))


def run_checks(binary, recorder, transcript):
    with H.start_instance(binary) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        device = amplifier(session)
        if device is None:
            return False
        # Strict one-command-per-undo granularity, so "one step" is measured, not merged by the coalescing window.
        session.result("control.set_undo_coalescing", {"window_ms": 0})
        a, b, c = (session.result("mixer.add_channel").get("channel") for _ in range(3))
        check_route(session, recorder, a, b)
        devices = check_pins(session, recorder, a, device)
        check_undo(session, recorder, a, devices)
        check_refusals(session, recorder, a, c, device, devices)
        reply = session.call("control.quit")
        session.client.close()
        exited, code, _ = instance.wait_for_exit(H.QUIT_TIMEOUT)
        recorder.check("control.quit stops the instance", reply.get("ok") is True and exited and code == 0,
                       "code=%r" % code)
    return True


def report(recorder):
    failed = [name for name, passed, _ in recorder.results if not passed]
    print("\n==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-78s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    print("\nFAIL: %d failed check(s)" % len(failed) if failed else "\nPASS: %d checks" % len(recorder.results))
    return 1 if failed else 0


def main(argv):
    if len(argv) < 2 or not os.path.exists(argv[1]):
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    if not run_checks(argv[1], recorder, transcript):
        print("this build has no amplifier module - Skipped, never Passed")
        return 77
    transcript.dump()
    return report(recorder)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
