#!/usr/bin/env python3
"""Serve mode (`zene --engine`, Q050-517): the engine with the control socket and no interface.

Drives the REAL binary. What is proven, and how each proof could fail:

 1. no-gui-application  The engine comes up ready with NO display at all: QT_QPA_PLATFORM names a
                        platform that needs one (xcb) and DISPLAY/WAYLAND_DISPLAY are removed. A
                        QGuiApplication cannot be constructed there, so a ready engine means none was.
                        CONTROL: the same environment WITHOUT --engine must not become ready - if it
                        did, the environment would prove nothing.
 2. interface-commands  Every command that declares `requires: display` and every `window.*` command
                        answers a typed error and the process survives; control.ping says
                        interface:"none".
 3. engine-commands     An edit, an undo, a save, and a reopen work as they do in the application.
 4. quit                control.quit ends the process with exit 0, the socket unlinked and no
                        last-resort guard; control.quit {save:true} writes the project first (the
                        application's window does that save; here nothing else would).
 5. sigterm             SIGTERM ends it the same way: a supervisor needs no socket client.
 6. start-refusals      `--engine` without --control-socket exits 1 and says why.
 7. project-argument    `zene --engine <project>` serves that project.

Usage: control-engine-serve.py <zene-binary>
Exit codes: 0 every check held; 1 a check failed (the app log is printed).
"""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

EXIT_BOUND = 30.0
NO_DISPLAY_ENV = {"QT_QPA_PLATFORM": "xcb"}
DISPLAY_VARIABLES = ("DISPLAY", "WAYLAND_DISPLAY")


class ServeInstance(H.Instance):
    """An instance started with `--engine`, optionally with a project and with no display variables."""

    def __init__(self, binary, project=None, engine=True, no_display=False):
        super().__init__(binary, extra_env=NO_DISPLAY_ENV if no_display else None)
        self.project = project
        self.engine = engine
        self.no_display = no_display

    def argv(self):
        line = [self.binary, "--config", self.config_path, "--control-socket", self.socket_path]
        if self.engine:
            line.append("--engine")
        if self.project:
            line.append(self.project)
        return line

    def env(self):
        env = super().env()
        if self.no_display:
            for name in DISPLAY_VARIABLES:
                env.pop(name, None)
        return env


class Session:
    """A socket client with a running request id."""

    def __init__(self, instance):
        self.instance = instance
        self.client = H.connect(instance)
        self.transcript = H.Transcript()
        self.last_id = 0

    def ready(self):
        return H.wait_ready(self.instance, self.client, self.transcript).get("result") or {}

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is not True:
            raise AssertionError("%s failed: %r" % (command, reply.get("error")))
        return reply.get("result") or {}


def serve(binary, **options):
    instance = ServeInstance(binary, **options)
    instance.spawn()
    return instance


def clean_exit_problems(instance, label):
    """The shutdown contract: exit 0 inside the bound, socket gone, the last-resort guard silent."""
    problems = []
    exited, code, elapsed = instance.wait_for_exit(EXIT_BOUND)
    if not exited:
        return ["%s: the process was still running after %.0fs" % (label, EXIT_BOUND)]
    if code != 0:
        problems.append("%s: exit code %r, expected 0" % (label, code))
    if instance.socket_exists():
        problems.append("%s: the control socket was left behind" % label)
    if H.FATAL_GUARD_MARKER in instance.stderr_text():
        problems.append("%s: the last-resort shutdown guard fired" % label)
    print("  %s: exited %r in %.2fs" % (label, code, elapsed))
    return problems


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_no_gui_application(binary):
    problems = []
    with serve(binary, no_display=True) as instance:
        ping = Session(instance).ready()
        if ping.get("interface") != "none":
            problems.append("control.ping interface is %r, expected 'none'" % ping.get("interface"))
        if "with no interface" not in instance.stderr_text():
            problems.append("the serve line is missing from stderr")
    # The control: the application in the same environment must NOT come up.
    with serve(binary, engine=False, no_display=True) as control:
        exited, code, _ = control.wait_for_exit(EXIT_BOUND)
        if not exited or code == 0:
            problems.append("CONTROL: the application started with no display (exited=%r code=%r); "
                            "this environment cannot tell a GUI application from none" % (exited, code))
    return problems


def interface_commands(session):
    listing = session.result("control.commands_list").get("commands") or []
    declared = [c["id"] for c in listing if "display" in (c.get("requires") or [])]
    windows = [c["id"] for c in listing if c["id"].startswith("window.")]
    return declared, windows


def display_command_problems(session, declared):
    problems = []
    for command in declared:
        error = (session.call(command).get("error") or {})
        if error.get("kind") != "requires":
            problems.append("%s answered %r, expected the typed 'requires' error" % (command, error))
    return problems


def window_command_problems(session, windows):
    """A window command either reads state that needs no window (window.layout_list) or is refused
    typed; what it may never do is answer untyped or take the process down."""
    problems = []
    refused = 0
    for command in windows:
        reply = session.call(command)
        error = reply.get("error") or {}
        if reply.get("ok") is not True and not (error.get("kind") and error.get("message")):
            problems.append("%s answered neither a result nor a typed error: %r" % (command, reply))
        refused += error.get("kind") == "requires"
    if refused < len(windows) // 2:
        problems.append("only %d of %d window commands were refused 'requires'" % (refused, len(windows)))
    return problems


def check_interface_commands(binary):
    problems = []
    with serve(binary) as instance:
        session = Session(instance)
        session.ready()
        declared, windows = interface_commands(session)
        if len(declared) < 5 or len(windows) < 20:
            problems.append("the registry lists %d display commands and %d window commands; "
                            "the check would be vacuous" % (len(declared), len(windows)))
        problems += display_command_problems(session, declared)
        problems += window_command_problems(session, windows)
        if not instance.alive():
            problems.append("the engine died while answering interface commands")
        elif session.result("control.ping").get("engine_ready") is not True:
            problems.append("the engine is not ready after the interface commands")
    return problems


def check_engine_commands(binary):
    problems = []
    with serve(binary) as instance:
        session = Session(instance)
        session.ready()
        before = len(session.result("track.list").get("tracks") or [])
        session.result("track.add", {"type": "instrument"})
        if len(session.result("track.list")["tracks"]) != before + 1:
            problems.append("track.add did not add a track")
        session.result("control.undo")
        if len(session.result("track.list")["tracks"]) != before:
            problems.append("control.undo did not remove the added track")
        session.result("control.redo")
        tracks = session.result("track.list")["tracks"]
        if len(tracks) != before + 1:
            problems.append("control.redo did not bring the track back")
        session.result("track.rename", {"track": tracks[-1]["id"], "name": "serve-proof"})
        path = os.path.join(instance.tmp, "serve.mmp")
        session.result("project.save", {"path": path})
        if not os.path.exists(path) or "serve-proof" not in open(path, errors="replace").read():
            problems.append("project.save did not write the renamed track to %s" % path)
        session.result("project.new")
        session.result("project.open", {"path": path})
        names = [t.get("name") for t in session.result("track.list")["tracks"]]
        if "serve-proof" not in names:
            problems.append("project.open did not bring the track back: %r" % names)
    return problems


def check_quit(binary):
    problems = []
    with serve(binary) as instance:
        session = Session(instance)
        session.ready()
        session.result("track.add", {"type": "instrument"})  # modified: the application would prompt
        session.result("control.quit")
        problems += clean_exit_problems(instance, "control.quit")

    with serve(binary) as instance:
        session = Session(instance)
        session.ready()
        path = os.path.join(instance.tmp, "quit-save.mmp")
        session.result("project.save", {"path": path})
        track = session.result("track.add", {"type": "instrument"})
        session.result("track.rename", {"track": track["id"], "name": "saved-by-quit"})
        if "saved-by-quit" in open(path, errors="replace").read():
            problems.append("quit-save: the edit was on disk before the quit; the check proves nothing")
        session.result("control.quit", {"save": True})
        problems += clean_exit_problems(instance, "control.quit save:true")
        if "saved-by-quit" not in open(path, errors="replace").read():
            problems.append("control.quit {save:true} did not save the project")
    return problems


def check_sigterm(binary):
    with serve(binary) as instance:
        Session(instance).ready()
        instance.process.send_signal(signal.SIGTERM)
        problems = clean_exit_problems(instance, "SIGTERM")
        if "termination signal" not in instance.stderr_text():
            problems.append("SIGTERM: the engine did not say why it quit")
    return problems


def check_start_refusals(binary):
    problems = []
    env = H.with_vendor_library_path(binary, dict(os.environ))
    env["QT_QPA_PLATFORM"] = "offscreen"
    with H.Instance(binary) as scratch:  # a throwaway HOME and config, never the user's
        env.update({"HOME": scratch.tmp, "XDG_CONFIG_HOME": scratch.tmp, "XDG_DATA_HOME": scratch.tmp})
        run = subprocess.run([binary, "--config", scratch.config_path, "--engine"], env=env, cwd=scratch.tmp,
                             capture_output=True, timeout=EXIT_BOUND, check=False)
    if run.returncode != 1:
        problems.append("`--engine` without --control-socket exited %r, expected 1" % run.returncode)
    if b"--engine needs --control-socket" not in run.stderr:
        problems.append("the refusal does not name the missing option: %r" % run.stderr[-300:])
    return problems


def check_project_argument(binary):
    problems = []
    with serve(binary) as first:
        session = Session(first)
        session.ready()
        track = session.result("track.add", {"type": "instrument"})
        session.result("track.rename", {"track": track["id"], "name": "from-the-command-line"})
        path = os.path.join(first.tmp, "argument.mmp")
        session.result("project.save", {"path": path})
        with serve(binary, project=path) as second:
            reader = Session(second)
            reader.ready()
            names = [t.get("name") for t in reader.result("track.list")["tracks"]]
            if "from-the-command-line" not in names:
                problems.append("the project named on the command line was not loaded: %r" % names)
            state = reader.result("project.get_state")
            if os.path.basename(path) not in str(state):
                problems.append("project.get_state does not name the loaded file: %r" % state)
    return problems


CHECKS = (
    ("no-gui-application", check_no_gui_application),
    ("interface-commands", check_interface_commands),
    ("engine-commands", check_engine_commands),
    ("quit", check_quit),
    ("sigterm", check_sigterm),
    ("start-refusals", check_start_refusals),
    ("project-argument", check_project_argument),
)


def run_check(name, check, binary):
    started = time.time()
    try:
        problems = check(binary)
    except (H.Timeout, AssertionError, OSError, subprocess.TimeoutExpired) as error:
        problems = ["%s: %s" % (type(error).__name__, error)]
    print("%s: %s (%.1fs)" % (name, "ok" if not problems else "FAILED", time.time() - started))
    return name, not problems, problems


def main(argv):
    if len(argv) != 2 or not os.path.exists(argv[1]):
        print("usage: control-engine-serve.py <zene-binary>", file=sys.stderr)
        return 2
    binary = os.path.abspath(argv[1])
    return H.finish([run_check(name, check, binary) for name, check in CHECKS])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
