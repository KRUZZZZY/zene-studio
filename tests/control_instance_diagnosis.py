#!/usr/bin/env python3
"""Why did an instance stop answering? Evidence collection, split out of the harness.

A frozen instance is a HANG and a hang is a failure, but for six CI matrices the suite
reported only "no response line inside 30.0s" - the symptom. The runners (no audio hardware,
no /dev/snd) are the one place it happens and the one place it cannot be reproduced off-runner,
so the harness collects the evidence itself.

Split out of control_socket_harness.py for two ratchet reasons, both measured by run-all-gates:
the harness had grown past the 500-line cap (Gate 7) and this code's entry point had CCN 17
(Gate 4). Nothing here is logic the harness needs in order to run - only in order to explain a
failure - so a separate module is the honest place for it.
"""

import os
import shutil
import subprocess

# The most recently spawned instance, so a Blocked raise anywhere in a test can describe it.
_LAST_INSTANCE = None


def register_instance(instance):
    """Called by Instance.spawn() so a later failure can describe this instance."""
    global _LAST_INSTANCE
    _LAST_INSTANCE = instance


def _proc_facts(pid):
    """Kernel-side facts about a live pid: what it is waiting on, and its thread count."""
    facts = []
    try:
        with open("/proc/%d/wchan" % pid) as handle:
            facts.append("kernel wait channel: %s" % handle.read().strip())
    except OSError:
        pass
    try:
        with open("/proc/%d/status" % pid) as handle:
            for line in handle.read().splitlines():
                if line.startswith(("State:", "Threads:")):
                    facts.append(line.strip())
    except OSError:
        pass
    return facts


def _backtrace(pid):
    """A debugger's view of every thread, when the runner has one. Bounded and best-effort."""
    for tool, args in (("gdb", ["-p", str(pid), "-batch", "-ex", "thread apply all bt"]),
                       ("lldb", ["-p", str(pid), "-b", "-o", "thread backtrace all", "-o", "quit"])):
        if not shutil.which(tool):
            continue
        try:
            done = subprocess.run([tool] + args, capture_output=True, text=True, timeout=180)
            body = (done.stdout or done.stderr or "").splitlines()
        except Exception as exc:  # a debugger that cannot attach is not the test's failure
            return ["%s could not attach: %s" % (tool, exc)]
        return ["--- %s, first 100 lines ---" % tool] + body[:100] + ["--- end %s ---" % tool]
    return ["no gdb or lldb on this runner: no backtrace available"]


def _crash_reports(root):
    """The product's own crash reporter writes these on a signal; print whatever is there."""
    lines = []
    for found in sorted(__import__("pathlib").Path(root).rglob("*crash*report*")):
        try:
            lines.append("--- crash report %s ---" % found.name)
            lines.extend(found.read_text(errors="replace").splitlines()[:80])
        except OSError:
            pass
    return lines


def instance_diagnosis():
    """One string explaining the last-launched instance's state. Never raises."""
    instance = _LAST_INSTANCE
    if instance is None or instance.process is None:
        return "diagnosis: no instance was launched by this process"
    proc = instance.process
    if proc.poll() is not None:
        lines = ["diagnosis: the instance EXITED with %s - a crash or a refusal, not a hang "
                 "(its stdout/stderr are the transcript above)" % proc.returncode]
        return "\n".join(lines + _crash_reports(getattr(instance, "tmp", os.getcwd())))
    lines = ["diagnosis: the instance is STILL RUNNING (pid %d) - a HANG, not a crash" % proc.pid]
    return "\n".join(lines + _proc_facts(proc.pid) + _backtrace(proc.pid))
