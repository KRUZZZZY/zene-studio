#!/usr/bin/env python3
"""The case/flow payloads for the control-surface tests: the DIALOGUE, not the plumbing.

Split out of `control_socket_harness.py` on 2026-09-12: the harness core -
`Instance`, `Client`, the readiness poll and the assertion helpers - had grown
past the 500-line per-file ratchet (Gate 7) together with the case payloads.
This module holds the parts a TEST calls, not the parts that start a process or
speak the protocol:

  - the multi-instance flow helpers the headless tests drive
    (`healthy_control`, `diagnose_block`, `parse_args`, `report_pre_fix`);
  - the pure `evidence -> problems` checkers (`check_*`), which take the raw
    evidence a test has already gathered and return the list of failed claims.
    There is no I/O in them: a checker can be pointed at a hand-written reply
    (that is what tests/control-negative-control.py does).

Everything these functions call - the bounds, the launch recipe, the socket
client, the readiness poll - is imported from `control_socket_harness`; there is
no second copy of any of it.

Usage is unchanged (each test script owns its own argv):

    QT_QPA_PLATFORM=offscreen python3 <test>.py <lmms> [...]

Exit code 0 only when every assertion passed. A hang is still a failure, and
every bound still lives in `control_socket_harness`.
"""

import sys
import time

from control_socket_harness import (  # noqa: E402
    FATAL_GUARD_MARKER, LEGACY_WATCHDOG_LINE, PING_TIMEOUT, Blocked, Transcript,
    connect, ok, start_instance, wait_ready,
)

# ---------------------------------------------------------------------------
# flow helpers - a whole case, in one call
# ---------------------------------------------------------------------------


def diagnose_block(instance, client, transcript, request_id=99):
    """After a Blocked, say WHICH block signature was measured.

    Returns one sentence for the report. Raises AssertionError when the instance
    is demonstrably NOT blocked (a ping that answers `engine_ready:true`).
    """
    try:
        reply = client.call(request_id, "control.ping", timeout=PING_TIMEOUT, transcript=transcript)
    except Blocked:
        return "the socket stopped answering altogether (the UI thread is not dispatching events)"
    result = reply.get("result") or {}
    if not result.get("pong"):
        raise AssertionError("control.ping answered without pong: %r" % (reply,))
    if result.get("engine_ready"):
        raise AssertionError("the instance reports engine_ready while presumably blocked: %r"
                             % (reply,))
    return ("the nested modal loop is still dispatching: control.ping answers "
            "pong=true engine_ready=false, i.e. the startup path is parked inside the box")


def healthy_control(binary, seconds=90.0):
    """Prove the binary CAN serve a control socket on this machine, right now.

    A negative control must not be satisfiable by a broken environment: a binary
    started outside its data directory (or on a machine with no Qt platform
    plugin) never becomes ready either, which would make "never became ready"
    mean nothing.  This starts the SAME binary with a working directory that
    exists and the dummy device, and returns how long it took to become ready.
    """
    instance = start_instance(binary)
    transcript = Transcript()
    try:
        client = connect(instance)
        started = time.time()
        wait_ready(instance, client, transcript, seconds=seconds, ping_timeout=PING_TIMEOUT)
        return time.time() - started
    finally:
        instance.close()


def parse_args(usage, minimum_argv=1):
    """argv[1..] with a `--expect-blocked` switch; returns (mode, rest)."""
    argv = [a for a in sys.argv[1:] if a != "--expect-blocked"]
    expect_blocked = "--expect-blocked" in sys.argv[1:]
    if len(argv) < minimum_argv:
        print(usage)
        sys.exit(2)
    return expect_blocked, argv


def report_pre_fix(what):
    ok("%s [pre-fix mode: the block was reproduced, as task #625 measured]" % what)


# ---------------------------------------------------------------------------
# checkers - pure evidence -> problems
# ---------------------------------------------------------------------------


def check_clean_shutdown(exited, exit_code, socket_exists, stderr_text, elapsed_s,
                         expect_socket_unlinked=True):
    """A clean shutdown: gone in time, exit 0, no watchdog, socket unlinked."""
    problems = []
    if not exited:
        problems.append("the process did not exit within the bounded wait (%.1fs); "
                        "the shutdown is blocked" % elapsed_s)
    elif exit_code != 0:
        problems.append("the process exited with %s, expected 0" % exit_code)
    if LEGACY_WATCHDOG_LINE in stderr_text:
        problems.append("stderr carries the pre-fix watchdog line %r: the event loop did not stop"
                        % LEGACY_WATCHDOG_LINE)
    if FATAL_GUARD_MARKER in stderr_text:
        problems.append("stderr carries the last-resort guard line: the normal shutdown did not "
                        "complete (it must never fire)")
    if expect_socket_unlinked and socket_exists:
        problems.append("the control socket file was not unlinked on exit")
    return problems


def check_ping_shape(reply, request_id=1):
    """control.ping carries liveness, readiness and the audio report, always."""
    problems = []
    if reply.get("id") != request_id:
        problems.append("ping reply id %r != %r" % (reply.get("id"), request_id))
    if reply.get("ok") is not True:
        problems.append("ping answered %r, expected ok=true (ping must work before the engine "
                        "is ready - that is its whole purpose)" % reply)
        return problems
    result = reply.get("result") or {}
    if result.get("pong") is not True:
        problems.append("ping result has no pong=true: %r" % result)
    if not isinstance(result.get("engine_ready"), bool):
        problems.append("ping result has no boolean engine_ready: %r" % result)
    audio = result.get("audio")
    if not isinstance(audio, dict):
        problems.append("ping result has no audio report: %r" % result)
    else:
        for field in ("state", "device", "requested", "start_failed", "sound_output"):
            if field not in audio:
                problems.append("ping audio report is missing '%s': %r" % (field, audio))
    return problems


def check_readiness_reason(reply):
    """While engine_ready is false, ping must say WHY - a code and a message."""
    problems = []
    result = reply.get("result") or {}
    if result.get("engine_ready") is not False:
        problems.append("expected engine_ready=false to check the reason, got %r" % result)
        return problems
    reason = result.get("reason")
    if not isinstance(reason, dict):
        problems.append("engine_ready=false with no 'reason' object: a bare false is exactly "
                        "the defect this test targets: %r" % result)
        return problems
    if not reason.get("code"):
        problems.append("reason carries no code: %r" % reason)
    if not reason.get("message"):
        problems.append("reason carries no message: %r" % reason)
    return problems


def check_busy_carries_reason(reply):
    """An engine command issued while not ready is a typed busy error, explained."""
    problems = []
    if reply.get("ok") is not False:
        problems.append("an engine command before readiness answered %r, expected a typed "
                        "failure" % reply)
        return problems
    error = reply.get("error") or {}
    if error.get("kind") != "busy":
        problems.append("expected error.kind=busy before readiness, got %r" % error)
    message = error.get("message") or ""
    if not message:
        problems.append("the busy error carries no message: %r" % error)
    return problems


def check_audio_fallback(reply, requested):
    """The no-sound-card report: names the backend that failed, and the fallback."""
    problems = []
    result = reply.get("result") or {}
    audio = result.get("audio") or {}
    if audio.get("start_failed") is not True:
        problems.append("the configured device '%s' cannot open on this box, but the audio "
                        "report says start_failed=%r" % (requested, audio.get("start_failed")))
    if audio.get("state") != "dummy_fallback":
        problems.append("audio.state is %r, expected 'dummy_fallback' (the product fell back "
                        "to the dummy device and must say so)" % audio.get("state"))
    if audio.get("sound_output") is not False:
        problems.append("audio.sound_output is %r, expected false (a dummy device makes no "
                        "sound)" % audio.get("sound_output"))
    if requested not in (audio.get("requested") or ""):
        problems.append("the report does not name the requested backend %r: %r"
                        % (requested, audio.get("requested")))
    if not (audio.get("message") or ""):
        problems.append("the fallback is silent: no audio.message explaining the failure: %r"
                        % audio)
    return problems


def check_requires_device_refusal(reply):
    """A device-dependent command refuses, typed, naming the backend."""
    problems = []
    if reply.get("ok") is not False:
        problems.append("transport.play answered %r on an instance whose audio device failed "
                        "to open; it must refuse with a typed error" % reply)
        return problems
    error = reply.get("error") or {}
    if error.get("kind") != "requires":
        problems.append("expected error.kind=requires, got %r" % error)
    message = error.get("message") or ""
    if not message:
        problems.append("the refusal carries no message: %r" % error)
    return problems
