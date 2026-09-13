#!/usr/bin/env python3
"""Negative controls: prove every #626 test can FAIL on the defect it targets.

A test that cannot fail is not evidence. This script drives the SAME checkers the
tests use with deliberately defective evidence and asserts they report problems.
Three kinds of control:

  A. assertion-level - the exact pre-fix artefacts (the verbatim
     "control.quit: the event loop did not stop" line, a non-zero exit, a socket
     file left behind, a shutdown that never finishes, a bare `engine_ready:false`
     with no reason) are fed to the checkers; each must be rejected. This is what
     keeps the checkers from being tautologies.
  B. end-to-end, self-contained - the real binary is made to exhibit each SYMPTOM
     (SIGKILL: non-zero exit + socket left behind; SIGSTOP: a shutdown that never
     finishes) and the shutdown checker must flag it. These run against the real
     product, not a simulation of it. A symptom a box cannot exhibit at all - the
     arm64 macOS runner resumes/exits the SIGSTOPped instance, measured - is a
     stated skip printed with its measurement, never a silent pass: control (A)
     still proves the checker rejects that artefact, and control (B) still
     asserts it wherever the symptom is exhibitable.
  C. end-to-end, exact defect - with --legacy-binary <pre-fix lmms>, the same
     shutdown scenario is run against the binary built before the fix and the
     checker must reject it. That binary is not part of this worktree, so the
     control is optional here; the run that used it is recorded in the lane
     report.

Usage: control-negative-control.py <lmms-binary> [--legacy-binary <lmms>]
"""

import os
import signal
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_flows import (  # noqa: E402
    check_audio_fallback, check_busy_carries_reason, check_clean_shutdown,
    check_ping_shape, check_readiness_reason, check_recovery_cleaned,
    check_requires_device_refusal,
)
from control_socket_harness import (  # noqa: E402
    BROKEN_DEVICE, FATAL_GUARD_MARKER, LEGACY_WATCHDOG_LINE, Client, Instance,
    Problems, Timeout, finish,
)

CONNECT_TIMEOUT = 60.0
READY_TIMEOUT = 120.0


def stop_observed(process, timeout_s):
    """Positive evidence that SIGSTOP took: waitpid(WUNTRACED) reports a stop.

    The SIGSTOP control's premise is "a stopped process cannot exit", and the
    only honest way to depend on that is to observe the stop rather than assume
    it. Consuming the stop report does not change the process state - it stays
    stopped, and Popen.poll() keeps returning None (no state change) until it is
    continued. Returns True when the child was reported stopped, False when it
    was not (it exited instead, or the box never stopped it).
    """
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            pid, status = os.waitpid(process.pid, os.WUNTRACED | os.WNOHANG)
        except (ChildProcessError, OSError):
            return False
        if pid == 0:
            time.sleep(0.02)
            continue
        return os.WIFSTOPPED(status)
    return False


def must_reject(problems, name, defective, check, reason):
    """`check(defective)` must report problems; if it reports none, this control failed."""
    found = check(defective)
    if not found:
        problems.add("control %r did NOT detect the defect it targets (%s): %r"
                     % (name, reason, defective))
    else:
        print("  control %-34s detected: %s" % (name, found[0]))
    return found


def assertion_level_controls():
    problems = Problems()

    # --- the shutdown checker (test: control-shutdown.py) -------------------
    base = (True, 0, False, "", 0.4)  # a clean run, for contrast
    if check_clean_shutdown(*base):
        problems.add("the shutdown checker rejects a CLEAN run: %r"
                     % (check_clean_shutdown(*base),))
    def shutdown_check(evidence):
        return check_clean_shutdown(*evidence)

    must_reject(problems, "legacy watchdog line on stderr",
                (True, 0, False, "control.quit: %s; unlinking the control socket and exiting\n"
                 % LEGACY_WATCHDOG_LINE, 2.1),
                shutdown_check,
                "reproduction (a)/(b) printed this line instead of shutting down")
    must_reject(problems, "last-resort guard fired",
                (True, 1, False, "control.quit: %s within 10000 ms; ...\n" % FATAL_GUARD_MARKER, 10.2),
                shutdown_check, "the guard must never fire in a healthy run")
    must_reject(problems, "non-zero exit code",
                (True, 3, False, "", 0.5), shutdown_check,
                "a forced or failed shutdown must not look clean")
    must_reject(problems, "socket file left behind",
                (True, 0, True, "", 0.5), shutdown_check,
                "the socket must be unlinked on exit")
    must_reject(problems, "shutdown never finished",
                (False, None, True, "", 30.0), shutdown_check,
                "a hang must count as a failure, not as a wait")

    # --- the autosave cleanup (test: control-shutdown.py) -------------------
    # The file that survives is the crash marker the NEXT launch offers to
    # recover, so a clean quit that keeps it is a false "the last session
    # crashed". The real product failed this on Qt 5.15 (control.quit stopped the
    # event loop without closing the window, so closeEvent never ran).
    if check_recovery_cleaned(True, False):
        problems.add("the recovery checker rejects a clean quit that DID clean up: %r"
                     % (check_recovery_cleaned(True, False),))
    must_reject(problems, "clean quit left recover.mmp behind",
                (True, True), lambda evidence: check_recovery_cleaned(*evidence),
                "control.quit must run the same cleanup a GUI quit does")

    # --- the readiness checker (test: control-readiness.py) -----------------
    prefix_ping = {"id": 1, "ok": True, "result": {
        "pong": True, "engine_ready": False, "version": "0.2.0-alpha", "proto": 1}}
    must_reject(problems, "bare engine_ready=false", prefix_ping, check_readiness_reason,
                "the pre-fix ping answered false with no reason at all")
    must_reject(problems, "reason without a message",
                {"id": 1, "ok": True, "result": {"pong": True, "engine_ready": False,
                 "reason": {"code": "engine_starting"}}},
                check_readiness_reason, "a code with no message is not actionable")
    must_reject(problems, "ping with no audio report",
                {"id": 1, "ok": True, "result": {"pong": True, "engine_ready": True}},
                check_ping_shape, "a bare ready is not a report")
    must_reject(problems, "busy error with no message",
                {"id": 2, "ok": False, "error": {"kind": "busy", "message": ""}},
                check_busy_carries_reason, "the old busy error said nothing actionable")
    must_reject(problems, "engine command answered ok before ready",
                {"id": 2, "ok": True, "result": {"channels": []}},
                check_busy_carries_reason, "the pre-readiness window must be typed")

    # --- the no-audio-device checkers (test: control-no-audio-device.py) ----
    good = {"id": 1, "ok": True, "result": {"pong": True, "engine_ready": True, "audio": {
        "state": "dummy_fallback", "requested": BROKEN_DEVICE, "device": "Dummy (no sound output)",
        "start_failed": True, "sound_output": False, "message": "device 'SDL' could not be opened"}}}
    if check_audio_fallback(good, BROKEN_DEVICE):
        problems.add("the audio checker rejects a correct fallback report: %r"
                     % (check_audio_fallback(good, BROKEN_DEVICE),))
    must_reject(problems, "fallback not announced",
                {"id": 1, "ok": True, "result": {"pong": True, "engine_ready": True, "audio": {
                    "state": "ok", "requested": BROKEN_DEVICE, "device": BROKEN_DEVICE,
                    "start_failed": False, "sound_output": True}}},
                lambda reply: check_audio_fallback(reply, BROKEN_DEVICE),
                "the dummy fallback must never be silent")
    must_reject(problems, "fallback reported without a reason",
                {"id": 1, "ok": True, "result": {"pong": True, "engine_ready": True, "audio": {
                    "state": "dummy_fallback", "requested": BROKEN_DEVICE,
                    "device": "Dummy (no sound output)", "start_failed": True,
                    "sound_output": False}}},
                lambda reply: check_audio_fallback(reply, BROKEN_DEVICE),
                "an unexplained fallback is still a silent failure")
    must_reject(problems, "transport.play silently 'succeeds' on a dummy device",
                {"id": 2, "ok": True, "result": {"playing": True}},
                check_requires_device_refusal,
                "playback that makes no sound must refuse, typed and named")
    return problems


def end_to_end_symptom_controls(binary):
    """Make the real binary show the shutdown symptoms and require detection.

    Returns (problems, skips). A symptom this box cannot exhibit at all is a
    stated skip: not a silent pass (the skip is printed with its measurement and
    repeated in the run summary) and not a failure about the platform.
    """
    problems = Problems()
    skips = []

    # (1) SIGKILL: the process dies without running any shutdown, so the socket
    # file is left behind and the exit status is the signal - both assertions must
    # fire. (No control.quit here: with the fix in place a quit would exit
    # cleanly, which would prove nothing.)
    with Instance(binary) as inst:
        try:
            inst.spawn()
            inst.wait_for_socket(CONNECT_TIMEOUT)
            inst.process.send_signal(signal.SIGKILL)
            exited, code, elapsed = inst.wait_for_exit(15.0)
            found = check_clean_shutdown(exited, code, inst.socket_exists(), inst.stderr_text(),
                                        elapsed)
            if not found:
                problems.add("a SIGKILLed instance looked like a clean shutdown: exit=%s socket=%s"
                             % (code, inst.socket_exists()))
            else:
                print("  control %-34s detected: %s" % ("SIGKILL (real process)", found[0]))
        except Timeout as exc:
            problems.add("SIGKILL control could not start the instance: %s" % exc)
        finally:
            inst.close()

    # (2) SIGSTOP after control.quit: the shutdown begins and never finishes.
    #
    # The premise of this control is that a STOPPED process cannot exit, so the
    # only evidence the shutdown checker can see is "did not exit in the bound".
    # That premise is checkable, and on macos-arm64 it does not hold: the
    # instance exited anyway - cleanly (exit 0, socket unlinked), so the checker
    # correctly found nothing to reject - and the old code reported that as a
    # failure of this control (job 103762607454; the same control failed the same
    # way on the same platform in run 34757610204, job 103724603529, before the
    # wave-2 merge, so nothing in the wave regressed it). When a box cannot
    # exhibit the symptom the control must skip LOUDLY with its measurement
    # rather than fail: the assertion-level control (A) above still proves the
    # checker rejects "shutdown never finished" evidence.
    with Instance(binary) as inst:
        try:
            inst.spawn()
            inst.wait_for_socket(CONNECT_TIMEOUT)
            client = Client(inst.socket_path, timeout_s=3.0)
            inst.process.send_signal(signal.SIGSTOP)
            stopped = stop_observed(inst.process, 2.0)
            answered = True
            try:
                client.call(1, "control.quit")
            except Timeout:
                answered = False  # stopped before it could answer; that is the point
            client.close()
            exit_observed, code, elapsed = inst.wait_for_exit(8.0)
            socket_left = inst.socket_exists()
            found = check_clean_shutdown(exit_observed, code, socket_left, inst.stderr_text(),
                                         elapsed)
            if found:
                print("  control %-34s detected: %s (stop observed=%s)"
                      % ("SIGSTOP (real process)", found[0], stopped))
            elif exit_observed:
                skips.append("SIGSTOP symptom not exhibitable: the instance exited inside the "
                             "bounded wait (exit=%s, quit_answered=%s, socket_left=%s) even "
                             "though it was sent SIGSTOP, and waitpid(WUNTRACED) reported the "
                             "stop=%s; a stopped process cannot exit, so this box never gave "
                             "the checker the 'shutdown never finished' evidence it "
                             "targets" % (code, answered, socket_left, stopped))
                print("  control %-34s SKIPPED (stated, not a pass): %s"
                      % ("SIGSTOP (real process)", skips[-1]))
            else:
                problems.add("a stopped instance looked like a clean shutdown (exited=%s, "
                             "stop observed=%s)" % (exit_observed, stopped))
            inst.process.send_signal(signal.SIGCONT)
        except Timeout as exc:
            problems.add("SIGSTOP control could not start the instance: %s" % exc)
        finally:
            inst.close()
    return problems, skips


def reach_ready(client):
    """Poll control.ping until the instance is ready (bounded)."""
    deadline = time.time() + READY_TIMEOUT
    while time.time() < deadline:
        ping = client.call(1, "control.ping")
        if (ping.get("result") or {}).get("engine_ready") is True:
            return True
        time.sleep(0.2)
    return False


def drive_legacy_defect(inst):
    """Reproduce the defect on the pre-fix binary and collect the shutdown evidence.

    undo/redo leaves the project modified, so the pre-fix quit path ends in the
    modal "Project not saved" box (or its audio-dialog sibling) and the 2 s
    watchdog fires instead of a normal exit.
    """
    client = Client(inst.socket_path)
    try:
        reach_ready(client)
        for index, (cmd, args) in enumerate(
                (("mixer.add_channel", {}),
                 ("mixer.set_volume", {"channel": "ch-1", "volume": 0.5}),
                 ("control.undo", {}),
                 ("control.redo", {})), start=2):
            client.call(index, cmd, args)
        client.call(90, "control.quit")
    finally:
        client.close()
    exited, code, elapsed = inst.wait_for_exit(30.0)
    return exited, code, inst.socket_exists(), inst.stderr_text(), elapsed


def legacy_binary_control(legacy):
    """Run the shutdown scenario against a pre-fix binary; its defect must be caught."""
    problems = Problems()
    with Instance(legacy) as inst:
        try:
            inst.spawn()
            inst.wait_for_socket(CONNECT_TIMEOUT)
            exited, code, socket_exists, stderr_text, elapsed = drive_legacy_defect(inst)
            found = check_clean_shutdown(exited, code, socket_exists, stderr_text, elapsed)
            if not found:
                problems.add("the pre-fix binary %s passed the clean-shutdown check; the control "
                             "proves nothing about this test" % legacy)
            else:
                print("  control %-34s detected: %s" % ("pre-fix binary (real defect)", found[0]))
            if not (LEGACY_WATCHDOG_LINE in stderr_text or not exited or (code or 0) != 0):
                problems.add("the pre-fix binary shut down cleanly; this control no longer "
                             "exercises the defect")
        except Timeout as exc:
            problems.add("legacy control could not drive the pre-fix binary: %s" % exc)
        finally:
            inst.close()
    return problems


def main():
    args = [a for a in sys.argv[1:]]
    legacy = None
    if "--legacy-binary" in args:
        index = args.index("--legacy-binary")
        legacy = os.path.abspath(args[index + 1])
        del args[index:index + 2]
    if not args:
        print(__doc__)
        return 2
    binary = os.path.abspath(args[0])
    if not os.path.exists(binary):
        print("FAIL: no lmms binary at %s" % binary)
        return 1

    results = []
    print("A. assertion-level controls (the checkers must reject the pre-fix artefacts)")
    problems = assertion_level_controls()
    results.append(("negative control: assertion level", not problems, problems.items))

    print("B. end-to-end symptom controls (real process, real symptoms)")
    problems, skips = end_to_end_symptom_controls(binary)
    results.append(("negative control: end-to-end symptoms", not problems, problems.items))
    for item in skips:
        print("SKIPPED (stated, not a pass): %s" % item)

    if legacy:
        print("C. end-to-end exact-defect control against %s" % legacy)
        problems = legacy_binary_control(legacy)
        results.append(("negative control: pre-fix binary", not problems, problems.items))
    else:
        print("C. skipped: pass --legacy-binary <pre-fix lmms> to run the exact-defect control")
    return finish(results)


if __name__ == "__main__":
    sys.exit(main())
