#!/usr/bin/env python3
"""Q050-183.1: the audio status and the test tone on a normal, healthy start.

control-no-audio-device.py proves the same commands on the dummy FALLBACK, where the
configured backend failed to open. This test proves them on the path every user
takes: the configured backend (the dummy, by name) opens, so audio.device_list
reports no fallback and no failure, and the running device is the configured one.

It also proves the test tone is harmless to the project: it plays on the running
device, stops on request, and leaves transport.get_state exactly as it was. The
refusals are typed `invalid_args`.

Hermetic: the dummy backend is the same on every machine, nothing is set in the
environment, and no plug-in is needed. Every wait is bounded.

Usage: control-audio-test-tone.py <lmms-binary>
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_harness import (  # noqa: E402
    DUMMY_DEVICE, Client, Instance, Problems, QUIT_TIMEOUT, Timeout, dump, finish, wait_ready,
)

CONNECT_TIMEOUT = 60.0
STDERR_DUMP_LIMIT = 4000
TONE_SECONDS = 5.0
# transport.get_state fields that legitimately move with the wall clock are not
# part of "the transport was left alone"; everything else must be identical.
WALL_CLOCK_MARKERS = ("wall", "clock", "timestamp", "elapsed")
REFUSALS = ({"frequency": 5}, {"seconds": 0}, {"level_db": 0}, {"frequency": "x"})
SHORT_TONE_SECONDS = 0.1
# A tone that ends by itself is waited out for this long before it is stopped.
SHORT_TONE_WAIT = 0.8
# The module holds 8 slots, so 12 tones only pass if finished tones are reclaimed.
REPEAT_TONE_COUNT = 12
REPEAT_TONE_GAP = 0.3


def add_step(steps, name):
    problems = Problems()
    steps.append((name, problems))
    return problems


def stable_view(state):
    """transport.get_state without its wall-clock fields."""
    return {key: value for key, value in state.items()
            if not any(marker in key.lower() for marker in WALL_CLOCK_MARKERS)}


def check_device_list(client, problems):
    """(a) the configured backend opened: running == requested, no fallback, no failure."""
    listed = client.call(10, "audio.device_list")
    problems.require(listed.get("ok") is True, "audio.device_list failed: %r" % listed)
    if listed.get("ok") is not True:
        return {}
    status = listed.get("result") or {}
    problems.require(status.get("current") == DUMMY_DEVICE,
                     "audio.device_list current is %r, expected %r"
                     % (status.get("current"), DUMMY_DEVICE))
    problems.require(status.get("requested") == status.get("current"),
                     "audio.device_list requested %r must equal current %r on a normal start"
                     % (status.get("requested"), status.get("current")))
    problems.require(status.get("fallback") is False,
                     "audio.device_list fallback is %r, expected false" % status.get("fallback"))
    problems.require(status.get("start_failed") is False,
                     "audio.device_list start_failed is %r, expected false"
                     % status.get("start_failed"))
    problems.require(status.get("failure_reason") == "",
                     "audio.device_list failure_reason is %r, expected an empty string"
                     % status.get("failure_reason"))
    print("audio.device_list: current=%r requested=%r fallback=%r start_failed=%r failure_reason=%r"
          % (status.get("current"), status.get("requested"), status.get("fallback"),
             status.get("start_failed"), status.get("failure_reason")))
    return status


def get_state(client, request_id, problems):
    """transport.get_state as a dict; {} (with a problem recorded) when it fails."""
    reply = client.call(request_id, "transport.get_state")
    problems.require(reply.get("ok") is True, "transport.get_state failed: %r" % reply)
    if reply.get("ok") is not True:
        return {}
    return reply.get("result") or {}


def check_test_tone(client, status, problems):
    """(c) the tone plays on the running device, and the dummy is never audible."""
    tone = client.call(20, "audio.test_tone", {"seconds": TONE_SECONDS})
    problems.require(tone.get("ok") is True,
                     "audio.test_tone failed on the healthy dummy: %r" % tone)
    if tone.get("ok") is not True:
        return
    result = tone.get("result") or {}
    problems.require(result.get("playing") is True,
                     "audio.test_tone playing is %r, expected true" % result.get("playing"))
    problems.require(result.get("output") == status.get("current"),
                     "audio.test_tone output %r must equal the running device %r"
                     % (result.get("output"), status.get("current")))
    problems.require(result.get("audible") is False,
                     "audio.test_tone audible is %r on the dummy, expected false"
                     % result.get("audible"))
    problems.require(isinstance(result.get("configured"), str),
                     "audio.test_tone configured is %r, expected a string" % result.get("configured"))
    print("audio.test_tone: %r" % result)


def check_transport_unchanged(client, before, problems):
    """(d) the tone left the transport alone: playing and position unchanged."""
    after = get_state(client, 21, problems)
    if not before or not after:
        return
    problems.require(stable_view(after) == stable_view(before),
                     "transport.get_state changed across the test tone: before %r, after %r"
                     % (stable_view(before), stable_view(after)))
    print("transport.get_state: playing=%r unchanged across the tone (%d fields compared)"
          % (after.get("playing"), len(stable_view(after))))


def check_test_tone_stop(client, problems):
    """(e) the tone is still playing when stopped; a second stop finds nothing."""
    first = client.call(22, "audio.test_tone_stop")
    problems.require(first.get("ok") is True, "audio.test_tone_stop failed: %r" % first)
    if first.get("ok") is True:
        result = first.get("result") or {}
        problems.require(result.get("stopped") is True,
                         "audio.test_tone_stop stopped is %r, expected true" % result.get("stopped"))
        problems.require(result.get("was_playing") is True,
                         "audio.test_tone_stop was_playing is %r, expected true (the stop came "
                         "well within %.1fs)" % (result.get("was_playing"), TONE_SECONDS))
    second = client.call(23, "audio.test_tone_stop")
    problems.require(second.get("ok") is True, "second audio.test_tone_stop failed: %r" % second)
    if second.get("ok") is True:
        problems.require((second.get("result") or {}).get("was_playing") is False,
                         "second audio.test_tone_stop was_playing is %r, expected false"
                         % (second.get("result") or {}).get("was_playing"))


def check_refusals(client, problems):
    """(f) malformed tone arguments are refused with a typed invalid_args error."""
    for index, args in enumerate(REFUSALS):
        reply = client.call(30 + index, "audio.test_tone", args)
        error = reply.get("error") or {}
        problems.require(reply.get("ok") is False and error.get("kind") == "invalid_args"
                         and bool(error.get("message")),
                         "audio.test_tone %r must be refused with invalid_args: %r" % (args, reply))


def check_tone_ends_itself(client, problems):
    """(h) a tone that has ended by itself is not reported as playing when stopped."""
    tone = client.call(50, "audio.test_tone", {"seconds": SHORT_TONE_SECONDS})
    problems.require(tone.get("ok") is True, "audio.test_tone failed: %r" % tone)
    time.sleep(SHORT_TONE_WAIT)
    stop = client.call(51, "audio.test_tone_stop")
    problems.require(stop.get("ok") is True, "audio.test_tone_stop failed: %r" % stop)
    was_playing = (stop.get("result") or {}).get("was_playing")
    problems.require(was_playing is False,
                     "audio.test_tone_stop was_playing is %r after a tone that ended, expected false"
                     % was_playing)
    print("h: a %.1fs tone ended by itself; audio.test_tone_stop was_playing=%r"
          % (SHORT_TONE_SECONDS, was_playing))


def check_repeated_tones(client, problems):
    """(i) finished tones are reclaimed: 12 short tones in turn are all accepted."""
    for index in range(REPEAT_TONE_COUNT):
        reply = client.call(60 + index, "audio.test_tone", {"seconds": SHORT_TONE_SECONDS})
        problems.require(reply.get("ok") is True,
                         "audio.test_tone %d of %d refused; finished tones were not reclaimed: %r"
                         % (index + 1, REPEAT_TONE_COUNT, reply))
        time.sleep(REPEAT_TONE_GAP)
    stop = client.call(72, "audio.test_tone_stop")
    problems.require(stop.get("ok") is True, "audio.test_tone_stop failed: %r" % stop)
    print("i: %d repeated %.1fs tones all accepted; audio.test_tone_stop ok=%r"
          % (REPEAT_TONE_COUNT, SHORT_TONE_SECONDS, stop.get("ok")))


def drive(client, steps):
    """The client side, one Problems per step. Returns the quit reply."""
    device = add_step(steps, "a: audio.device_list reports the configured backend running")
    status = check_device_list(client, device)
    recorded = add_step(steps, "b: transport.get_state recorded before the tone")
    before = get_state(client, 11, recorded)
    tone = add_step(steps, "c: audio.test_tone plays on the running device, never audible")
    check_test_tone(client, status, tone)
    unchanged = add_step(steps, "d: transport.get_state unchanged by the tone")
    check_transport_unchanged(client, before, unchanged)
    stop = add_step(steps, "e: audio.test_tone_stop stops the tone; a second stop finds nothing")
    check_test_tone_stop(client, stop)
    refusals = add_step(steps, "f: audio.test_tone refuses bad arguments with invalid_args")
    check_refusals(client, refusals)
    ended = add_step(steps, "h: a tone that ends by itself is not reported playing")
    check_tone_ends_itself(client, ended)
    reclaimed = add_step(steps, "i: repeated tones are reclaimed")
    check_repeated_tones(client, reclaimed)
    return client.call(40, "control.quit")


def check_shutdown(inst, quit_reply, steps):
    """(g) control.quit is acknowledged and the instance exits 0."""
    quit_step = add_step(steps, "g: control.quit answers quitting; the instance exits 0")
    quit_step.require((quit_reply.get("result") or {}).get("quitting") is True,
                      "control.quit answered %r" % quit_reply)
    exited, code, _ = inst.wait_for_exit(QUIT_TIMEOUT)
    quit_step.require(exited, "the instance did not exit after control.quit")
    if exited and code != 0:
        quit_step.add("the instance exited with %s after control.quit" % code)


def run_instance(binary, steps):
    """Start one normal (dummy) instance and run every step against it."""
    run = add_step(steps, "run: every wait stayed inside its bound")
    with Instance(binary, audiodev=DUMMY_DEVICE) as inst:
        try:
            inst.spawn()
            inst.wait_for_socket(CONNECT_TIMEOUT)
            client = Client(inst.socket_path)
            try:
                ready = wait_ready(inst, client, None)
                print("ready with audiodev=%s: %r" % (DUMMY_DEVICE, (ready.get("result") or {}).get("engine_ready")))
                quit_reply = drive(client, steps)
            finally:
                client.close()
            check_shutdown(inst, quit_reply, steps)
        except Timeout as exc:
            run.add(str(exc))
            inst.kill()
        if any(problems.items for _, problems in steps):
            dump("audio-test-tone stderr", inst.stderr_text()[-STDERR_DUMP_LIMIT:])


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print("FAIL: no lmms binary at %s" % binary)
        return 1

    steps = []
    run_instance(binary, steps)
    return finish([(name, not problems, problems.items) for name, problems in steps])


if __name__ == "__main__":
    sys.exit(main())
