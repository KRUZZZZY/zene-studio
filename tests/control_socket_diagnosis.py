#!/usr/bin/env python3
"""Why an instance stopped answering: the DIAGNOSTIC, split out of the harness.

Split out of `control_socket_harness.py` on 2026-09-13, the same mechanical move
that created `control_socket_flows.py` on 2026-09-12: the harness core had grown
past the 500-line per-file ratchet (Gate 7) a second time, this time with the
diagnostic the release line added ("a frozen instance now reports its liveness,
wait channel and a backtrace") to a file whose core is the launch recipe and the
socket client.

A frozen instance is a HANG, and a hang is a failure - but until 2026-09-13 a CI
run reported only "no response line inside 30.0s", which named the symptom and not
the cause. The runners (no audio hardware, no /dev/snd) are the one place this
happens and the one place it cannot be reproduced off-runner, so the harness
collects the evidence itself: liveness, the kernel's wait channel, and a debugger
backtrace when one is present.

The split is also what makes the per-method complexity ratchet (Gate 4) pass. The
three steps were one function in the harness and measured CCN 14 against a target
of 10; they are two gatherers and one verdict here, and the printed text is
unchanged, because other tests and humans read it.

`Instance.spawn()` calls `remember()` and `Client._read_line` prints
`instance_diagnosis()`; nothing here starts a process or speaks the protocol.
"""

import shutil
import subprocess

_LAST_INSTANCE = None


def remember(instance):
    """Record the instance the harness just spawned, for a later diagnosis."""
    global _LAST_INSTANCE
    _LAST_INSTANCE = instance


def _proc_state_lines(pid: int) -> list:
    """The kernel's own answer for a live pid: wait channel, then task state."""
    lines = []
    for path, label in (("/proc/%d/wchan" % pid, "kernel wait channel"),
                        ("/proc/%d/status" % pid, "state")):
        try:
            with open(path) as fh:
                text = fh.read()
        except OSError:
            continue
        if label == "kernel wait channel":
            lines.append("%s: %s" % (label, text.strip()))
        else:
            lines.extend(l.strip() for l in text.splitlines()
                         if l.startswith(("State:", "Threads:", "voluntary")))
    return lines


def _backtrace_lines(pid: int) -> list:
    """The first debugger that is present, and only that one; none at all is normal."""
    lines = []
    for tool, args in (("gdb", ["-p", str(pid), "-batch", "-ex", "thread apply all bt"]),
                       ("lldb", ["-p", str(pid), "-b", "-o", "thread backtrace all", "-o", "quit"])):
        if not shutil.which(tool):
            continue
        try:
            done = subprocess.run([tool] + args, capture_output=True, text=True, timeout=180)
            body = (done.stdout or done.stderr or "").splitlines()
            lines.append("--- %s, first 100 lines ---" % tool)
            lines.extend(body[:100])
            lines.append("--- end %s ---" % tool)
        except Exception as exc:  # a debugger that cannot attach is not the test's failure
            lines.append("%s could not attach: %s" % (tool, exc))
        break
    return lines


def instance_diagnosis():
    """One paragraph on why the last instance stopped answering.

    Three shapes, in the order they are decided: no instance was launched, the
    instance EXITED (a crash or a refusal, not a hang), or it is STILL RUNNING -
    the hang the runners produce, and the only one that carries the observations.
    """
    instance = _LAST_INSTANCE
    if instance is None or instance.process is None:
        return "diagnosis: no instance was launched by this process"
    proc = instance.process
    if proc.poll() is not None:
        return ("diagnosis: the instance EXITED with %s - a crash or a refusal, not a hang "
                "(its stdout/stderr are the transcript above)" % proc.returncode)
    pid = proc.pid
    lines = ["diagnosis: the instance is STILL RUNNING (pid %d) - a HANG, not a crash" % pid]
    lines.extend(_proc_state_lines(pid))
    lines.extend(_backtrace_lines(pid))
    return "\n".join(lines)
