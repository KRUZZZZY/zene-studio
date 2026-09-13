#!/usr/bin/env python3
"""Why did an instance stop answering? Evidence collection, split out of the harness.

A frozen instance is a HANG and a hang is a failure, but for six CI matrices the suite
reported only "no response line inside 30.0s" - the symptom. The runners (no audio hardware,
no /dev/snd) are the one place it happens and the one place it cannot be reproduced off-runner,
so the harness collects the evidence itself.

WHAT A FIRST VERSION GOT WRONG. It reported liveness, `Threads:`/`State:` and a debugger
backtrace, and the seven-platform matrix answered `Threads: 1` / `State: R (running)` /
`kernel wait channel: 0` / `no gdb or lldb on this runner`. Those four lines do not separate
the two states a `Threads: 1` process can be in, and they are the whole diagnosis:

  * BLOCKED - parked in a syscall, waiting for a device, a lock or a peer. wchan names what,
    and the process burns no CPU.
  * COMPUTING - running one thread flat out, which makes "took longer than the bound" the
    actual finding (the engine was still initialising, not stuck), and makes the fix a
    startup-cost problem rather than a device problem.

So this module now measures CPU, enumerates EVERY thread rather than counting them, prints
the tail of both captured streams (the app's own "which device failed" sentence goes to
stdout), gives the debugger two attempts, and says how to get a debugger onto the runner.
Everything here stays bounded (a 1 s CPU sample), read-only, and on the failure path only.

Split out of control_socket_harness.py for two ratchet reasons, both measured by run-all-gates:
the harness had grown past the 500-line cap (Gate 7) and this code's entry point had CCN 17
(Gate 4). Nothing here is logic the harness needs in order to run - only in order to explain a
failure - so a separate module is the honest place for it. Each function below keeps CCN <= 10
for the same gate.
"""

import os
import shutil
import subprocess
import time

# The most recently spawned instance, so a Blocked raise anywhere in a test can describe it.
_LAST_INSTANCE = None

# Bounds. A failure path may spend this long explaining itself, and no longer.
_CPU_SAMPLE_SECONDS = 1.0
_DEBUGGER_TIMEOUT = 120
_DEBUGGER_LINES = 120
_MAX_THREADS_LISTED = 16
_LOG_TAIL_LINES = 12


def register_instance(instance):
    """Called by Instance.spawn() so a later failure can describe this instance."""
    global _LAST_INSTANCE
    _LAST_INSTANCE = instance


def _read(path):
    """File contents, or '' when the kernel says no. Never raises."""
    try:
        with open(path) as handle:
            return handle.read()
    except OSError:
        return ""


def _proc_facts(pid):
    """Kernel-side facts about a live pid: what it is waiting on, and its thread count."""
    facts = []
    wchan = _read("/proc/%d/wchan" % pid).strip()
    if wchan:
        facts.append("kernel wait channel: %s" % wchan)
    for line in _read("/proc/%d/status" % pid).splitlines():
        if line.startswith(("State:", "Threads:")):
            facts.append(line.strip())
    return facts


def _cpu_ticks(pid):
    """(utime + stime) in clock ticks, or None. /proc/<pid>/stat fields 14 and 15."""
    halves = _read("/proc/%d/stat" % pid).rsplit(") ", 1)
    if len(halves) != 2:
        return None
    # fields[0] here is the process state (field 3 of the real stat line).
    fields = halves[1].split()
    try:
        return int(fields[11]) + int(fields[12])
    except (IndexError, ValueError):
        return None


def _cpu_evidence(pid, seconds=_CPU_SAMPLE_SECONDS):
    """COMPUTING or BLOCKED? One bounded sample of the process's own CPU accounting.

    A process that burns a core while it answers nothing is not trapped - it is busy - and
    the same stack-later question ("which function?") has a different answer in each case.
    Reported as a percentage of ONE core, so a 4-core runner cannot flatter it.
    """
    before = _cpu_ticks(pid)
    time.sleep(seconds)
    after = _cpu_ticks(pid)
    if before is None or after is None:
        return ["cpu: /proc/%d/stat is not readable here, no CPU sample" % pid]
    burned = 100.0 * (after - before) / 100.0 / seconds
    return ["cpu: burned %.0f%% of one core over %.1fs (%d -> %d ticks)"
            % (burned, seconds, before, after)]


def _thread_states(pid):
    """EVERY thread's state and wait channel, or an explicit single-thread report.

    `Threads: 1` in the first version's output was read as "the engine is still starting";
    printing the threads themselves makes that a fact (one thread, and which one) instead of
    an inference from a counter.
    """
    try:
        tids = sorted(os.listdir("/proc/%d/task" % pid))
    except OSError:
        return []
    lines = ["threads: %d live" % len(tids)]
    for tid in tids[:_MAX_THREADS_LISTED]:
        halves = _read("/proc/%d/task/%s/stat" % (pid, tid)).rsplit(") ", 1)
        if len(halves) != 2:
            continue
        fields = halves[1].split()
        wchan = _read("/proc/%d/task/%s/wchan" % (pid, tid)).strip() or "-"
        lines.append("  thread %s: state=%s wchan=%s"
                     % (tid, fields[0] if fields else "?", wchan))
    if len(tids) > _MAX_THREADS_LISTED:
        lines.append("  ... %d more" % (len(tids) - _MAX_THREADS_LISTED))
    return lines


def _log_tail(instance, attribute, count=_LOG_TAIL_LINES):
    """The tail of one captured stream. The app's device/progress lines go to stdout."""
    path = getattr(instance, attribute, None)
    if not path or not os.path.exists(str(path)):
        return []
    text = _read(str(path))
    if not text:
        return ["---- %s: empty ----" % attribute]
    return (["---- %s tail ----" % attribute]
            + text.splitlines()[-count:]
            + ["---- end %s ----" % attribute])


def _debugger_run(tool, extra_args, pid):
    """One debugger, up to two attempts: a live process can refuse the first attach."""
    lines = ["--- %s ---" % tool]
    for attempt in (1, 2):
        try:
            done = subprocess.run([tool, "-p", str(pid)] + extra_args, capture_output=True,
                                  text=True, timeout=_DEBUGGER_TIMEOUT)
        except Exception as exc:  # a debugger that cannot attach is not the test's failure
            lines.append("%s attach attempt %d raised: %s" % (tool, attempt, exc))
            return lines
        body = (done.stdout or done.stderr or "").splitlines()
        if body:
            lines.append("attach attempt %d:" % attempt)
            return lines + body[:_DEBUGGER_LINES] + ["--- end %s ---" % tool]
        lines.append("attach attempt %d produced no output" % attempt)
        time.sleep(0.5)
    return lines + ["--- end %s ---" % tool]


def _backtrace(pid):
    """A debugger's view of EVERY thread, when the runner has one. Bounded and best-effort."""
    for tool, extra_args in (("gdb", ["-batch", "-ex", "thread apply all bt"]),
                             ("lldb", ["-b", "-o", "thread backtrace all", "-o", "quit"])):
        if shutil.which(tool):
            return _debugger_run(tool, extra_args, pid)
    return ["no gdb or lldb on this runner: no backtrace available. The diagnostic that needs "
            "it is only as good as the debugger: 'gdb' belongs in "
            ".github/workflows/deps-ubuntu-24.04-gcc.txt (lldb ships with the Xcode CLT)."]


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
    lines += _proc_facts(proc.pid)
    lines += _thread_states(proc.pid)
    lines += _cpu_evidence(proc.pid)
    lines += _backtrace(proc.pid)
    lines += _log_tail(instance, "stdout_path")
    lines += _log_tail(instance, "stderr_path")
    return "\n".join(lines + _crash_reports(getattr(instance, "tmp", os.getcwd())))
