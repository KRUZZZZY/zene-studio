#!/usr/bin/env python3
"""The export-settings transcript never leaves its instance running.

tests/control-export-settings.py launches a standalone instance. When the
transcript timed out or raised, that instance lived on, orphaned: on
2026-11-01 one reached 12.8 GB RSS inside the merge train's service. This
test drives the transcript with a stand-in "instance" (a process that records
its pid and never answers) down each exit path and requires that the stand-in
is dead once the transcript is gone:

  * an uncaught exception inside the transcript (the socket is not a socket),
  * SIGTERM (how a supervisor ends a test),
  * SIGKILL of the transcript itself (no Python cleanup can run; a watchdog
    process the transcript started kills the instance - portable POSIX, not
    Linux's PR_SET_PDEATHSIG),

and it checks that the liveness probe used here never reads a missing /proc as
death (macOS has none).

No sleeps decide anything: every wait is a bounded poll on an observable fact
(the pid file, the transcript's exit, the stand-in's death).

usage: control-export-settings-cleanup.py <path to control-export-settings.py>
"""
import os
import signal
import subprocess
import sys
import tempfile
import time

TRANSCRIPT = sys.argv[1]
BOUND = 60.0

STAND_IN = """#!/bin/sh
echo $$ > "$STAND_IN_PIDFILE"
{make_socket}
exec sleep 600
"""


def _state_from_proc(pid):
    with open("/proc/%d/stat" % pid) as handle:
        return handle.read().rsplit(")", 1)[1].split()[0]


def _state_from_ps(pid):
    """`ps -o stat= -p PID` is POSIX and exists where /proc does not (macOS, BSD)."""
    out = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True)
    return out.stdout.strip()[:1] or None


def alive(pid, use_proc=True):
    """True unless the process is gone or a zombie.

    A missing /proc is NOT evidence of death: without it (macOS) the state comes
    from ps, and a state that cannot be read at all counts as ALIVE, so a probe
    that cannot see the process can never let the test pass over a live child.
    """
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        pass  # exists, owned by someone else
    state = None
    if use_proc:
        try:
            state = _state_from_proc(pid)
        except OSError:
            state = None
    if state is None:
        try:
            state = _state_from_ps(pid)
        except OSError:
            state = None
    return state != "Z"


def check_liveness_probe_without_proc():
    """The probe must call a live process alive without /proc, and a dead one dead."""
    child = subprocess.Popen(["sleep", "600"])
    try:
        assert alive(child.pid, use_proc=False), "a live process was reported dead without /proc"
    finally:
        child.kill()
        child.wait()
    assert not alive(child.pid, use_proc=False), "a reaped process was reported alive without /proc"
    print("PASS: liveness probe without /proc")


def poll(predicate, what):
    deadline = time.time() + BOUND
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError("timed out waiting for %s" % what)


def run_case(name, make_socket, ending):
    work = tempfile.mkdtemp(prefix="zctl-export-cleanup-")
    binary = os.path.join(work, "fake-zene")
    pidfile = os.path.join(work, "pid")
    with open(binary, "w") as handle:
        handle.write(STAND_IN.format(make_socket=make_socket))
    os.chmod(binary, 0o755)
    env = dict(os.environ, STAND_IN_PIDFILE=pidfile, TMPDIR=work)
    runner = subprocess.Popen([sys.executable, TRANSCRIPT, binary, "unused.mmp"], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    stand_in = None
    try:
        def read_pid():
            try:
                return int(open(pidfile).read().strip())
            except (OSError, ValueError):
                return None
        stand_in = poll(read_pid, "the stand-in to start")
        ending(runner)
        poll(lambda: runner.poll() is not None, "the transcript to end")
        poll(lambda: not alive(stand_in, use_proc=False), "the stand-in to die with the transcript")
    finally:
        if runner.poll() is None:
            runner.kill()
            runner.wait()
        if stand_in is not None and alive(stand_in, use_proc=False):
            os.kill(stand_in, signal.SIGKILL)  # never leak from the test itself
    print("PASS: %s" % name)


def wait_for_exception(runner):
    pass  # the transcript ends by itself


def terminate(runner):
    runner.send_signal(signal.SIGTERM)


def kill(runner):
    runner.send_signal(signal.SIGKILL)


check_liveness_probe_without_proc()

# $5 is the --control-socket path argument; a plain file there makes the
# transcript's connect() raise an uncaught exception.
run_case("uncaught exception", ': > "$5"', wait_for_exception)
run_case("SIGTERM", "", terminate)
run_case("SIGKILL of the transcript", "", kill)
sys.exit(0)
