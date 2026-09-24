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

Scenario (d) is the same class of defect one layer earlier: a crash report left by
an earlier session is a MODAL offer, and main() asks it BEFORE app->exec(). Where
nobody can click it (an unattended run - --control-socket, or no display) the box
used to run a nested event loop, so the engine never became ready and control.quit
died on the last-resort shutdown guard (exit 1). The offer must go to stderr there.

Bounded everywhere: a hang is a failure, never a wait. `control-negative-control.py`
shows this checker failing on defective evidence.

Usage: control-shutdown.py <lmms-binary> <project.mmp>
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_flows import (  # noqa: E402
    check_clean_shutdown, check_ping_shape, check_recovery_cleaned,
)
from control_socket_harness import (  # noqa: E402
    BROKEN_DEVICE, BROKEN_DEVICE_ENV, DEFAULT_DEVICE, PING_TIMEOUT, Client, Instance,
    Problems, Timeout, dump, finish,
)

CONNECT_TIMEOUT = 60.0
READY_TIMEOUT = 120.0
QUIT_TIMEOUT = 30.0
STDERR_DUMP_LIMIT = 4000


def wait_for_ready(inst, client, timeout_s, problems):
    """Poll control.ping until engine_ready, spending the whole declared budget.

    A ping that does not answer inside PING_TIMEOUT is not the end of the window:
    the engine initialises on the thread that serves this socket, so while it
    starts the client's ping is answered late or not at all. This poll used to end
    on ONE unanswered ping (client.call's 30s socket timeout), so the 120s budget
    READY_TIMEOUT declares was really 30s - the defect 9d15bd7e9 fixed in
    control-readiness.py and in the harness's wait_ready, left behind in this copy.
    Measured on CI: the linux-arm64 runner's engine start is ~34s, past one 30s
    ping, so a starting instance was reported as a HANG ("no response line inside
    30.0s") before the scenario could reach any shutdown assertion. The budget that
    ends the poll is therefore `timeout_s`, and its expiry carries the last error
    beside the instance's own diagnosis, so a real hang still fails, bounded, and a
    healthy slow start is not called one. The shape of every reply that arrives is
    asserted, as before.
    """
    deadline = time.time() + timeout_s
    reply = None
    last_error = None
    while time.time() < deadline:
        if not inst.alive():
            problems.add("the instance exited (code %s) while polling for readiness"
                         % inst.process.returncode)
            return reply
        try:
            reply = client.call(1, "control.ping", timeout=PING_TIMEOUT)
        except Timeout as error:
            last_error = str(error).splitlines()[0]
            time.sleep(0.2)
            continue
        problems.extend(check_ping_shape(reply, 1))
        if (reply.get("result") or {}).get("engine_ready") is True:
            return reply
        time.sleep(0.2)
    problems.add("the engine never became ready within %.1fs (last ping: %r, last error: %s)"
                 % (timeout_s, reply, last_error))
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
    problems.extend(check_recovery_cleaned(exited, os.path.exists(inst.recovery_file)))
    return exited, exit_code, socket_exists, stderr_text, elapsed


def drive_and_quit(inst, steps, problems):
    """Wait for readiness, run the steps, then quit and observe."""
    client = Client(inst.socket_path)
    try:
        wait_for_ready(inst, client, READY_TIMEOUT, problems)
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


#: The crash reporter's own names (include/CrashReporter.h), relative to the
#: configured working directory.
CRASH_REPORT_DIR = "crash-reports"
CRASH_REPORT_FILE = "zene-crash-report.txt"
OFFERED_MARKER_FILE = "zene-crash-report.offered"
#: The line main() writes where nobody can click the crash-report box
#: (offerPendingCrashReport, src/core/main.cpp).
CRASH_OFFER_LINE = "A crash report from an earlier session is pending:"


def plant_crash_report(inst):
    """Put a PENDING crash report in the instance's working directory.

    `pending` is the module's own predicate - the report file exists and the
    offered sentinel does not (CrashReporter.cpp, hasPendingReport) - and main()
    reads it at STARTUP. So this runs BEFORE spawn(), which is the one thing a
    fixture planted into a RUNNING instance can never do: by then the offer has
    already been decided.
    """
    directory = os.path.join(inst.workspace, CRASH_REPORT_DIR)
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, CRASH_REPORT_FILE), "w") as handle:
        handle.write("Zene Studio crash report v1\nsignal=SIGSEGV(11)\n")


def check_crash_offer(stderr_text, inst, problems):
    """The offer was MADE where it can be read, and recorded as made."""
    offer_at = stderr_text.find(CRASH_OFFER_LINE)
    problems.require(offer_at >= 0,
                     "the unattended offer must be on stderr, where the log and the operator "
                     "can see it; stderr tail: %r" % (stderr_text[-600:],))
    if offer_at >= 0:
        # By NAME, not by the planted path: reportFileInDir() joins "<working dir>/"
        # with "/crash-reports", so the module's own path doubles the separator.
        problems.require(CRASH_REPORT_FILE in stderr_text[offer_at:offer_at + 512],
                         "the offer must NAME the report file so it can be attached by hand: "
                         "%r" % (stderr_text[offer_at:offer_at + 200],))
    directory = os.path.join(inst.workspace, CRASH_REPORT_DIR)
    problems.require(os.path.exists(os.path.join(directory, OFFERED_MARKER_FILE)),
                     "the offer must be recorded as made (the module's own sentinel): %s"
                     % os.path.join(directory, OFFERED_MARKER_FILE))
    problems.require(os.path.exists(os.path.join(directory, CRASH_REPORT_FILE)),
                     "the report itself must be KEPT for a hand attach")


def crash_report_scenario(binary):
    """(d) A report pending AT STARTUP must not park the startup path.

    The offer is a MODAL box and main() asks it BEFORE app->exec(). Where nobody
    can click it - the harness's offscreen platform is one, which is what
    lmms::isUnattendedRun() names - the box runs a nested event loop: the engine
    never reaches setReady() (control.ping answers engine_ready=false forever and
    every engine command is the typed 'busy' refusal), and control.quit is answered
    and then dies on the shutdown guard ten seconds later, exit 1, with "this is a
    bug in the shutdown path, not in the client" on stderr. That is the boundary
    sweep's seed-1005 defect, and this is its only input.

    The shape of the three scenarios above, plus the offer's own evidence. The
    readiness poll inside drive_and_quit is the load-bearing assertion: a parked
    startup never answers engine_ready=true.
    """
    name = "shutdown: a crash report pending at startup"
    problems = Problems()
    exited, exit_code, socket_exists, stderr_text, elapsed = False, None, True, "", 0.0
    with Instance(binary) as inst:
        try:
            plant_crash_report(inst)         # BEFORE spawn: main() reads it at startup
            inst.spawn()
            inst.wait_for_socket(CONNECT_TIMEOUT)
            # The save keeps the recorded sequence's shape (a save, then the quit).
            exited, exit_code, socket_exists, stderr_text, elapsed = drive_and_quit(
                inst, [("project.save", {"path": os.path.join(inst.tmp, "boundary.mmp")})],
                problems)
            check_crash_offer(stderr_text, inst, problems)
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
    # (c) A PROJECT OPEN THAT SHRINKS THE MIXER. Mixer::loadSettings() clears the
    # mixer before it restores the file's channels (Mixer::clear() ->
    # deleteChannel()), and MixerView is not told, so its channel list is left one
    # entry LONGER than the mixer. Every view-side lookup then indexes the MIXER
    # with a VIEW index and Mixer::mixerChannel() does not bounds-check, so this
    # used to be a wild MixerChannel* - measured: SIGSEGV inside
    # Fader::calculateKnobPosYFromModel (a surplus view repainting a deleted
    # channel's model) and inside QObject::disconnectImpl on shutdown, exit code
    # -11. The fixture has ONE mixer channel, so adding one and opening it is
    # exactly that shrink, deterministically.
    results.append(shutdown_scenario(
        binary, project, "shutdown: a project open that shrinks the mixer", DEFAULT_DEVICE,
        [("mixer.add_channel", {}),
         ("project.open", {"path": project}),
         ("mixer.get_state", {})]))
    # (d) A crash report pending at startup: the modal offer runs BEFORE
    # app->exec(), so it parked the startup path where nobody could click it. Its
    # own function because the report has to be on disk BEFORE spawn().
    results.append(crash_report_scenario(binary))
    return finish(results)


if __name__ == "__main__":
    sys.exit(main())
