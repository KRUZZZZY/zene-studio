#!/usr/bin/env python3
"""Q050-235: the workspace preference survives a restart, over the control socket.

Starts the REAL `zene` binary headless twice over ONE user config, and proves:

  1. a fresh instance reads the default workspace ("standard") through window.appearance;
  2. `window.appearance {"workspace": "touch"}` selects the touch workspace, reports it and says
     the change takes effect at the next start;
  3. one `control.undo` puts the prior choice back, and redoing chooses touch again;
  4. the process is stopped and started again with the SAME --config, and the fresh process
     reports "touch" - the choice is in user configuration, not project data;
  5. the record of the choice is in the config file itself (ui/workspace), not only in memory.

Every wait is bounded; a hang is a failure, never a wait.

Usage: QT_QPA_PLATFORM=offscreen python3 control-touch-workspace.py <zene>
Exit 0 only when every check held.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402
from freeze_bounce_evidence import Recorder, Session, report_results  # noqa: E402


def workspace_of(session):
    return (session.result("window.appearance") or {}).get("workspace")


def choose(session, name):
    return session.result("window.appearance", {"workspace": name})


class Restarted(H.Instance):
    """The same user config and temp world as @a previous, in a new process.

    The temp world belongs to @a previous: this instance never removes it, so the next restart
    can read the same config file. The last world is removed once, by the original instance.
    """

    def __init__(self, previous):
        self.binary = previous.binary
        self.extra_env = dict(previous.extra_env)
        self.tmp = previous.tmp
        self.socket_path = previous.socket_path
        self.workspace = previous.workspace
        self.recovery_file = previous.recovery_file
        self.config_path = previous.config_path
        self.stderr_path = previous.stderr_path
        self.stdout_path = previous.stdout_path
        self.log_path = previous.stderr_path
        self.process = None
        self._stderr = None
        self._stdout = None

    def close(self):
        """Stop the process and close its logs; the shared temp world is not this process's to delete."""
        self.kill()
        for handle in (self._stderr, self._stdout):
            if handle:
                try:
                    handle.close()
                except (OSError, ValueError):
                    pass
        self._stderr = self._stdout = None


def stop(instance):
    """A clean stop through control.quit: the bounded wait_for_exit below is the real check."""
    try:
        if instance.alive():
            H.Client(instance.socket_path).call(1, "control.quit")
    except (H.Timeout, OSError):
        pass  # the reply races the shutdown
    exited, code, _ = instance.wait_for_exit(30)
    if not exited:
        raise H.Blocked("the instance did not stop for the restart inside 30s")
    if code != 0:
        raise H.Blocked("the instance did not shut down cleanly for the restart (code %s)" % code)


def restart(instance):
    """A clean stop, then a new process over the same temp world and --config file."""
    stop(instance)
    try:
        os.unlink(instance.socket_path)  # a clean shutdown unlinks it; a stale file is not the new instance's
    except OSError:
        pass
    fresh = Restarted(instance)
    fresh.spawn()
    return fresh


def check_the_choice(session, recorder):
    """The live instance: default, select, read back, one undo, redo."""
    recorder.check("a fresh config reads the standard workspace", workspace_of(session) == "standard",
                   "workspace=%r" % workspace_of(session))
    chosen = choose(session, "touch")
    recorder.check("selecting the touch workspace answers it and says it applies at the next start",
                   chosen.get("workspace") == "touch" and chosen.get("applies") == "next_start"
                   and chosen.get("restart_required") is True and bool(chosen.get("applies_note")),
                   "reply=%r" % chosen)
    recorder.check("an agent reads the saved choice back at once", workspace_of(session) == "touch",
                   "workspace=%r" % workspace_of(session))
    undone = session.result("control.undo")
    recorder.check("one undo restores the prior choice", undone is not None and workspace_of(session) == "standard",
                   "workspace=%r" % workspace_of(session))
    redone = session.result("control.redo")
    recorder.check("redo chooses the touch workspace again", redone is not None and workspace_of(session) == "touch",
                   "workspace=%r" % workspace_of(session))


def config_text(instance):
    try:
        with open(instance.config_path, encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError as error:
        print("note: could not read the config file (%s)" % error)
        return ""


def check_restart_keeps_it(previous, transcript, recorder):
    """A new process over the same config reads the saved choice back, from the file itself."""
    fresh = restart(previous)
    H.wait_for_socket(fresh)
    client = H.connect(fresh)
    H.wait_ready(fresh, client, transcript)
    restarted = Session(client, transcript)
    recorder.check("a restarted app keeps the selected workspace in user configuration",
                   workspace_of(restarted) == "touch", "workspace=%r" % workspace_of(restarted))
    text = config_text(fresh)
    recorder.check("the choice is in the config file itself (ui/workspace)",
                   'workspace="touch"' in text, "config has ui/workspace=%r" % ('workspace="touch"' in text))
    return fresh, restarted


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    instance = H.start_instance(argv[1])
    second = third = None
    try:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        check_the_choice(session, recorder)
        second, restarted = check_restart_keeps_it(instance, transcript, recorder)

        # The other direction too: choosing standard and restarting again reads standard back.
        choose(restarted, "standard")
        third = restart(second)
        third_client = H.connect(third)
        H.wait_ready(third, third_client, transcript)
        recorder.check("choosing standard and restarting reads standard back",
                       workspace_of(Session(third_client, transcript)) == "standard",
                       "workspace=%r" % workspace_of(Session(third_client, transcript)))
    finally:
        for running in (third, second, instance):
            if running is not None:
                running.close()
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("touch-workspace restart driver")
        return 1
    H.ok("touch-workspace restart driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
