#!/usr/bin/env python3
"""The documented readiness order, tested by a client that connects at once.

AGENT-TOOLING.md §4 (task #626): the control socket is up BEFORE the engine is
initialised, by design, so a client can see *why* an instance is not usable. The
order a client must follow is:

    connect -> poll control.ping -> wait for engine_ready -> issue engine commands

This test connects the moment the socket appears (i.e. before readiness) and
asserts the contract from that side:

  1. connecting that early WORKS (the socket is a readiness probe, not a
     post-startup service);
  2. while engine_ready is false, control.ping says WHY - a `reason` object with
     a code and a message, never a bare false;
  3. an engine command issued in that window answers the typed `busy` error and
     that error carries a message (it used to be "the engine is not initialised
     yet" with nothing to act on);
  4. control.ping eventually reports engine_ready true, and from then on the
     engine commands succeed.

Bounded everywhere. If a future change makes startup faster than the client can
connect, this test FAILS LOUDLY rather than passing vacuously: the not-ready
observation is the assertion, not a nicety.

Usage: control-readiness.py <lmms-binary> <project.mmp>
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_harness import (  # noqa: E402
    Client, Instance, Problems, Timeout, check_busy_carries_reason,
    check_ping_shape, check_readiness_reason, dump, finish,
)

CONNECT_TIMEOUT = 60.0
NOT_READY_WINDOW = 30.0
READY_TIMEOUT = 120.0
STDERR_DUMP_LIMIT = 4000


def observe_not_ready(client, connected_at, problems):
    """Poll ping until it is not ready; assert the reason and the typed busy error.

    Returns (observed, elapsed_s). The loop also stops if the instance turns out
    to be ready already, which the caller reports as a failure of the scenario
    rather than a pass.
    """
    deadline = time.time() + NOT_READY_WINDOW
    while time.time() < deadline:
        reply = client.call(1, "control.ping")
        problems.extend(check_ping_shape(reply, 1))
        ready = (reply.get("result") or {}).get("engine_ready")
        if ready is False:
            elapsed = time.time() - connected_at
            problems.extend(check_readiness_reason(reply))
            print("not ready %.3fs after connect: %r" % (elapsed, reply.get("result")))
            busy = client.call(2, "mixer.get_state")
            problems.extend(check_busy_carries_reason(busy))
            print("engine command before ready: %r" % busy)
            return True, elapsed
        if ready is True:
            return False, time.time() - connected_at
        time.sleep(0.01)
    return False, None


def wait_until_ready(client, problems):
    """Poll to readiness and require it. Returns True when ready."""
    deadline = time.time() + READY_TIMEOUT
    while time.time() < deadline:
        reply = client.call(3, "control.ping")
        problems.extend(check_ping_shape(reply, 3))
        result = reply.get("result") or {}
        if result.get("engine_ready") is True:
            if "reason" in result:
                problems.add("ping still carries a 'reason' after engine_ready became true: %r"
                             % result)
            return True
        time.sleep(0.2)
    return False


def exercise_engine_commands(client, problems):
    """From readiness on, the engine commands must work."""
    after = client.call(4, "mixer.get_state")
    shape = client.call(5, "transport.get_state")
    if after.get("ok") is not True:
        problems.add("mixer.get_state failed after readiness: %r" % after)
    if shape.get("ok") is not True:
        problems.add("transport.get_state failed after readiness: %r" % shape)


def drive_readiness(inst, problems):
    """Do the whole client side of the contract. Returns the quit reply or None."""
    started = time.time()
    client = Client(inst.socket_path)
    try:
        observed, _ = observe_not_ready(client, started, problems)
        if not observed and not problems.items:
            problems.add(
                "never observed engine_ready=false after connecting at the socket's creation: "
                "this test's whole point is the pre-readiness window. Either the instance was "
                "already ready when the client connected (make this scenario deterministic "
                "before trusting a green run), or ping stopped reporting readiness honestly.")
        if wait_until_ready(client, problems):
            print("ready after %.2fs" % (time.time() - started))
            exercise_engine_commands(client, problems)
            return client.call(6, "control.quit")
        problems.add("the engine never became ready within %.1fs" % READY_TIMEOUT)
        return None
    finally:
        client.close()


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print("FAIL: no lmms binary at %s" % binary)
        return 1

    problems = Problems()
    stderr_text = ""
    with Instance(binary) as inst:
        try:
            inst.spawn()
            inst.wait_for_socket(CONNECT_TIMEOUT)
            quit_reply = drive_readiness(inst, problems)
            if quit_reply is None:
                inst.kill()
            else:
                exited, code, _ = inst.wait_for_exit(30.0)
                problems.require(exited, "the instance did not exit after control.quit")
                if exited and code != 0:
                    problems.add("the instance exited with %s after control.quit" % code)
        except Timeout as exc:
            problems.add(str(exc))
            inst.kill()
        finally:
            stderr_text = inst.stderr_text()
            if problems.items:
                dump("readiness stderr", stderr_text[-STDERR_DUMP_LIMIT:])
            inst.close()

    return finish([("readiness: connect early, poll, then command", not problems,
                    problems.items)])


if __name__ == "__main__":
    sys.exit(main())
