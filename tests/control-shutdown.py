#!/usr/bin/env python3
"""Clean-shutdown test for BOTH #626 reproductions (control surface, SPEC A12).

Reproduction (a): after control.undo / control.redo the project is modified, so
`QCoreApplication::quit()` -> QApplication's termination -> `MainWindow::closeEvent`
-> `mayChangeProject()` used to open the modal "Project not saved" QMessageBox and
block there forever. Reproduction (b): with an audio device that cannot open, the
modal "Audio device setup failed" box in `MainWindow::finalize()` used to block
*before* `app->exec()`, so the instance never became ready and quit never worked.

Both scenarios must now end the way a normal application exit does:
exit code 0, the control socket unlinked, the autosave recovery file cleaned up,
and NO "the event loop did not stop" line on stderr.

Bounded everywhere: a hang is a failure, never a wait. `control-negative-control.py`
shows this checker failing on defective evidence.

Usage: control-shutdown.py <lmms-binary> <project.mmp>
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_harness import (  # noqa: E402
    BROKEN_DEVICE, BROKEN_DEVICE_ENV, DEFAULT_DEVICE, Client, Instance, Problems,
    Timeout, check_clean_shutdown, check_ping_shape, dump, finish,
)

CONNECT_TIMEOUT = 60.0
READY_TIMEOUT = 120.0
QUIT_TIMEOUT = 30.0
STDERR_DUMP_LIMIT = 4000


def wait_for_ready(client, timeout_s, problems):
    """Poll control.ping until engine_ready, bounded. Returns the last reply."""
    deadline = time.time() + timeout_s
    reply = None
    while time.time() < deadline:
        reply = client.call(1, "control.ping")
        problems.extend(check_ping_shape(reply, 1))
        if (reply.get("result") or {}).get("engine_ready") is True:
            return reply
        time.sleep(0.2)
    problems.add("the engine never became ready within %.1fs (last ping: %r)" % (timeout_s, reply))
    return reply


def run_steps(client, steps, problems):
    """Drive the session the way the reproduction does."""
    for index, (cmd, args) in enumerate(steps, start=2):
        reply = client.call(index, cmd, args)
        if reply.get("ok") is not True:
            problems.add("step %s answered %r" % (cmd, reply))


def plant_recovery_file(inst):
    """Plant an autosave recover.mmp: a clean quit must clean it up.

    MainWindow::closeEvent calls sessionCleanup() on an accepted close when
    autosave is on, and that removes the file. This is the observable half of
    "the shutdown cleaned the autosave up".
    """
    with open(inst.recovery_file, "w") as handle:
        handle.write("planted by control-shutdown.py\n")


def quit_and_observe(inst, problems):
    """Ask for the normal shutdown and collect the evidence about it."""
    plant_recovery_file(inst)
    client = Client(inst.socket_path)
    try:
        quit_reply = client.call(90, "control.quit")
        if (quit_reply.get("result") or {}).get("quitting") is not True:
            problems.add("control.quit answered %r" % quit_reply)
    finally:
        client.close()

    exited, exit_code, elapsed = inst.wait_for_exit(QUIT_TIMEOUT)
    socket_exists = inst.socket_exists()
    stderr_text = inst.stderr_text()
    if exited and os.path.exists(inst.recovery_file):
        problems.add("the autosave recovery file survived a clean quit "
                     "(MainWindow::closeEvent -> sessionCleanup did not run)")
    return exited, exit_code, socket_exists, stderr_text, elapsed


def drive_and_quit(inst, steps, problems):
    """Wait for readiness, run the steps, then quit and observe."""
    client = Client(inst.socket_path)
    try:
        wait_for_ready(client, READY_TIMEOUT, problems)
        run_steps(client, steps, problems)
    finally:
        client.close()
    return quit_and_observe(inst, problems)


def shutdown_scenario(binary, project, name, device, steps, extra_env=None):
    """Start one instance, drive `steps`, quit it, and check the shutdown.

    Returns (name, ok, problems). A hang or an early death is a failure with the
    evidence attached, never an exception that hides the other scenario.
    """
    problems = Problems()
    exited, exit_code, socket_exists, stderr_text, elapsed = False, None, True, "", 0.0
    with Instance(binary, audiodev=device, extra_env=extra_env) as inst:
        try:
            inst.spawn()
            inst.wait_for_socket(CONNECT_TIMEOUT)
            exited, exit_code, socket_exists, stderr_text, elapsed = drive_and_quit(
                inst, steps, problems)
        except Timeout as exc:
            problems.add(str(exc))
            inst.kill()
            exited, exit_code, socket_exists, stderr_text, elapsed = (
                False, None, inst.socket_exists(), inst.stderr_text(), 0.0)
        finally:
            inst.close()

    problems.extend(check_clean_shutdown(exited, exit_code, socket_exists, stderr_text, elapsed))
    print("[%s] exit=%s after %.2fs, socket exists=%s" % (name, exit_code, elapsed, socket_exists))
    if problems:
        dump("%s stderr" % name, stderr_text[-STDERR_DUMP_LIMIT:])
    return name, not problems, problems.items


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    project = os.path.abspath(sys.argv[2])
    if not os.path.exists(project):
        print("FAIL: no fixture project at %s" % project)
        return 1

    results = []
    # (a) undo/redo leaves the project modified -> the quit prompt used to block.
    # The audio device works here on purpose: reproduction (a) is caused by the
    # dirty project alone, and a scenario that breaks both things at once would
    # not isolate it.
    results.append(shutdown_scenario(
        binary, project, "shutdown: undo/redo (dirty project)", DEFAULT_DEVICE,
        [("mixer.add_channel", {}),
         ("mixer.set_volume", {"channel": "ch-1", "volume": 0.5}),
         ("control.undo", {}),
         ("control.redo", {})]))
    # (b) an audio device that cannot open -> the startup audio dialog used to
    # block before app->exec(). No undo/redo anywhere in this one.
    results.append(shutdown_scenario(
        binary, project, "shutdown: no usable audio device", BROKEN_DEVICE,
        [("project.open", {"path": project}),
         ("mixer.get_state", {})], extra_env=BROKEN_DEVICE_ENV))
    return finish(results)


if __name__ == "__main__":
    sys.exit(main())
