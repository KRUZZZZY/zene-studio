#!/usr/bin/env python3
"""agent-surface-gate.py - the `agent_surface` ctest gate (SPEC-zene-studio.md A15).

WHAT THIS GATE IS FOR
---------------------
A feature that only exists in a menu is, to an agent, a feature that does not
exist (AGENT-TOOLING.md section 1). 35 lanes add user-facing features to this
tree at any moment, so the obligation to tool them has to be mechanical:

  REFLECTION      every action in every menu and every toolbar of the REAL UI
                  must resolve to a registered command id. The list is not
                  maintained by hand: `control.surface_report` walks the live
                  MainWindow over the control socket and reports one entry per
                  user-visible action (DECLARATION CONTRACT below).

  RATCHET         the actions that do not resolve today are grandfathered in
                  tests/agent-surface-baseline.txt. The baseline may only
                  shrink: a NEW unregistered action fails the gate, and so does
                  a baseline entry that has become registered (or whose action
                  no longer exists) until that line is deleted or the baseline
                  is re-anchored deliberately. Same valve as
                  tests/{coverage,file-length,complexity}-gate.sh:
                      --reanchor "reason"      (a blank reason is refused)

  REVERSE         every command the registry declares must be accounted for:
                  swept by the headless sweep, or listed in
                  tests/agent-surface-allowlist.txt with a one-line reason. An
                  allowlist entry only counts when the command declares a
                  `requires` value (display|device|human); a command with no
                  declared excuse has to be run.

  HEADLESS SWEEP  the app is started with QT_QPA_PLATFORM=offscreen, a dummy
                  audio device and a throwaway HOME, and every non-allowlisted
                  command is invoked (arguments synthesised from its own schema)
                  until it produces a TYPED result. A crash, a hang (bounded
                  timeout) or the process exiting early fails the gate. The
                  whole run must stay under BUDGET_SECONDS.

  NEGATIVE CONTROL nothing here is taken on trust: tests/agent-surface-negative-
                  control.md records a deliberate menu action with no command
                  failing this gate, and the same tree passing once it is gone.

DECLARATION CONTRACT - how an action says which command it implements
--------------------------------------------------------------------
A menu action (QAction) or toolbar button (QToolButton) sets ONE of these to a
"group.verb" string that exists in the registry: objectName(); the dynamic
property "controlCommand"; or, for a QAction only, data() - the weakest channel,
because data() already carries template paths and config keys, so it counts only
when it resolves to a live command. A value that looks like a command id but is
NOT in the registry is a hard failure that cannot be grandfathered: a declaration
that names nothing is worse than no declaration at all.

HONEST LIMITS - what this gate does NOT prove
---------------------------------------------
  * Registration is DECLARATIVE. The public Qt API cannot report which slots an
    action is connected to, so the gate proves the declaration exists and names
    a live command; it cannot prove the slot routes through the registry. A11's
    "one action, one implementation" is what makes the declaration true.
  * Menus generated at run time from user or machine state are out of scope and
    listed in tests/agent-surface-exempt.txt with a reason each; their entries
    are counted and reported, never silently dropped.
  * The toolbar scope is the "mainToolbar" widget (LMMS's toolbar is a plain
    QWidget of ToolButtons, not a QToolBar) plus any QToolBar descendant.
  * The sweep reaches each command's TYPED result, not each command's happy
    path; ControlSocketIntegration holds the happy paths of this slice.

USAGE (ctest passes the binary and the fixture)
-----------------------------------------------
    python3 tests/agent-surface-gate.py <lmms-binary> <fixture-project.mmp>
                     [--check] [--reanchor "reason"] [--self-test]
                     [--baseline PATH] [--allowlist PATH] [--exempt PATH]
                     [--budget SECONDS] [--report PATH]

Exit codes: 0 = pass, 1 = gate violation (or the app died), 2 = usage/setup
error, where a 2 means no verdict was reached.
"""

import argparse
import json
import os
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from agent_surface_lib import (  # noqa: E402  (path set above)
    action_key, derive_args, emit_report, evaluate, file_sha256, is_exempt,
    load_baseline, load_reasons, reanchor, self_test, typed, write_baseline,
)

HERE = os.path.dirname(os.path.abspath(__file__))

DEFAULT_BASELINE = os.path.join(HERE, "agent-surface-baseline.txt")
DEFAULT_ALLOWLIST = os.path.join(HERE, "agent-surface-allowlist.txt")
DEFAULT_EXEMPT = os.path.join(HERE, "agent-surface-exempt.txt")

#: The whole gate - app start, reflection, sweep, shutdown - must stay inside
#: this so it can live in the default ctest suite (SPEC A15 / task #620).
BUDGET_SECONDS = 120.0
#: One command may not block longer than this; a later reply is a hang.
COMMAND_TIMEOUT = 45.0
#: The socket must appear this fast after launch.
CONNECT_TIMEOUT = 30.0
#: The engine must report ready this fast (cold start, offscreen, dummy audio).
ENGINE_TIMEOUT = 60.0
#: Shutdown after control.quit.
QUIT_TIMEOUT = 20.0

TRANSCRIPT = []


def record(direction, payload):
    TRANSCRIPT.append("%s %s" % (direction, payload))


def log(message):
    print(message, flush=True)


def fail(message):
    log("FAIL: %s" % message)


def live_overrides(client, fixture, tmp):
    """Real arguments where a placeholder would only ever fail an id lookup.

    The sweep's job is to reach a TYPED result, and `{}`-style placeholders get
    there; these overrides make it reach a real one, so a command that regressed
    into a refusal is visible in the report rather than hidden behind an
    invalid-args answer that was the gate's own fault (an empty channel id is
    malformed, not merely unknown).
    """
    overrides = {"render.render": {"out": os.path.join(tmp, "sweep-render.wav"),
                                   "format": "wav"},
                 "project.open": {"path": fixture},
                 # An explicit path, always: `project.save` with no path writes back
                 # over whatever file is open - which is the fixture. (Measured: a
                 # sweep without this overwrote tests/data/agent-control-fixture.mmp.)
                 "project.save": {"path": os.path.join(tmp, "sweep-save.mmp")},
                 "transport.set_tempo": {"bpm": 120},
                 "transport.seek": {"ticks": 0}}
    tracks = fetch(client, 910, "track.list").get("tracks", [])
    if tracks:
        overrides["track.get_state"] = {"track": tracks[0].get("id")}
    master = fetch(client, 911, "mixer.get_state").get("channels", [{}])[0].get("id")
    if master:
        overrides["mixer.set_volume"] = {"channel": master, "volume": 1.0}
        overrides["mixer.set_pan"] = {"channel": master, "pan": 0.0}
    added = fetch(client, 912, "mixer.add_channel").get("channel")
    if added:
        overrides["mixer.remove_channel"] = {"channel": added}
    return overrides


# --------------------------------------------------------------------------
# the control socket client
# --------------------------------------------------------------------------

class Client:
    """Line-delimited JSON-RPC, one request and one response per line."""

    def __init__(self, path, timeout):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(path)
        self.buffer = b""

    def call(self, request_id, command, args=None, proto=1):
        request = {"id": request_id, "cmd": command, "args": args or {}, "proto": proto}
        raw = json.dumps(request, separators=(",", ":"))
        record("->", raw)
        self.sock.sendall(raw.encode("utf-8") + b"\n")
        reply = self._read_line()
        record("<-", reply.decode("utf-8", "replace"))
        return json.loads(reply)

    def _read_line(self):
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("the server closed the connection")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def wait_for_socket(path, process):
    deadline = time.time() + CONNECT_TIMEOUT
    while time.time() < deadline:
        if process.poll() is not None:
            raise RuntimeError("the app exited before the socket was usable (exit %s)"
                               % process.returncode)
        if os.path.exists(path):
            try:
                probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                probe.settimeout(1.0)
                probe.connect(path)
                probe.close()
                return
            except OSError:
                pass
        time.sleep(0.1)
    raise RuntimeError("the control socket %s never became connectable" % path)


def wait_for_engine(client, process, request_id):
    deadline = time.time() + ENGINE_TIMEOUT
    last = None
    while time.time() < deadline:
        last = client.call(request_id, "control.ping")
        if last.get("ok") and (last.get("result") or {}).get("engine_ready"):
            return request_id
        if process.poll() is not None:
            raise RuntimeError("the app exited while waiting for the engine")
        time.sleep(0.2)
    raise RuntimeError("the engine never became ready (last ping: %r)" % last)


# --------------------------------------------------------------------------
# the documented headless launch recipe (AGENT-TOOLING.md section 4)
# --------------------------------------------------------------------------

def write_config(tmp):
    """`audiodev` must be exactly AudioDummy::name().

    With no sound card the app otherwise falls back to the dummy device and
    MainWindow puts up a modal "Audio device setup failed" dialog before the
    event loop starts; after that engine_ready never becomes true and every
    command answers busy (measured for 92 s with no recovery).
    """
    workspace = os.path.join(tmp, "workspace")
    os.makedirs(workspace, exist_ok=True)
    config_path = os.path.join(tmp, "lmmsrc.xml")
    with open(config_path, "w", encoding="utf-8") as handle:
        handle.write(
            '<?xml version="1.0"?>\n'
            '<!DOCTYPE lmms-config-file>\n'
            '<lmmsconfig version="0.2.0-alpha" configversion="3">\n'
            '  <app configured="1"/>\n'
            '  <audioengine audiodev="Dummy (no sound output)"/>\n'
            '  <paths workingdir="%s"/>\n'
            '</lmmsconfig>\n' % workspace)
    return config_path


def launch(binary, tmp):
    """Start the real binary, offscreen, with HOME and every XDG directory temp."""
    socket_path = os.path.join(tmp, "surface.sock")
    config_path = write_config(tmp)

    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["HOME"] = tmp
    env["XDG_CONFIG_HOME"] = os.path.join(tmp, "config")
    env["XDG_DATA_HOME"] = os.path.join(tmp, "data")
    os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)
    os.makedirs(env["XDG_DATA_HOME"], exist_ok=True)

    log_path = os.path.join(tmp, "app.log")
    log_file = open(log_path, "wb")
    process = subprocess.Popen(
        [binary, "--config", config_path, "--control-socket", socket_path],
        stdout=log_file, stderr=subprocess.STDOUT, env=env, cwd=tmp)
    return process, log_file, socket_path, log_path


# --------------------------------------------------------------------------
# the live run
# --------------------------------------------------------------------------

def fetch(client, request_id, command, args=None):
    """Call a command and insist on a typed, successful reply."""
    reply = client.call(request_id, command, args)
    problem = typed(reply, request_id)
    if problem is not None:
        raise RuntimeError("%s: %s" % (command, problem))
    if not reply.get("ok"):
        raise RuntimeError("%s failed: %r" % (command, reply.get("error")))
    return reply.get("result") or {}


def probe(client, request_id, command, args):
    """Invoke one command and classify the outcome. Never raises on a bad reply."""
    began = time.time()
    try:
        reply = client.call(request_id, command, args)
    except socket.timeout:
        return {"status": "timeout", "elapsed": time.time() - began}
    except (ConnectionError, OSError) as error:
        return {"status": "dead", "detail": str(error), "elapsed": time.time() - began}
    elapsed = time.time() - began
    problem = typed(reply, request_id)
    if problem is not None:
        return {"status": "malformed", "detail": problem, "elapsed": elapsed}
    if reply.get("ok"):
        return {"status": "ok", "elapsed": elapsed, "args": args}
    error = reply.get("error") or {}
    return {"status": "typed_error", "elapsed": elapsed, "args": args,
            "kind": error.get("kind"), "message": error.get("message", "")[:160]}


def sweep_commands(client, commands, allowlist, overrides, budget, started):
    """Exercise every non-allowlisted command (control.quit is done last)."""
    sweep = {}
    first_id = 1000
    for offset, command in enumerate(sorted(commands)):
        if command in allowlist or command == "control.quit":
            continue
        if time.time() > started + budget:
            raise RuntimeError("the gate exceeded its %.0f s budget during the sweep" % budget)
        args = derive_args(commands[command].get("args_schema", {}) or {}, command, overrides)
        request_id = first_id + offset
        outcome = probe(client, request_id, command, args)
        sweep[command] = outcome
        log("   %-28s %-11s %5.1fs  %s" % (
            command, outcome["status"], outcome["elapsed"],
            outcome.get("kind") or ("args=%s" % json.dumps(outcome.get("args", {})))))
    return sweep


def quit_and_wait(client, process, request_id):
    """The last command: its typed reply plus a clean, prompt process exit."""
    reply = client.call(request_id, "control.quit")
    problem = typed(reply, request_id)
    client.close()
    if problem is not None:
        return {"status": "malformed", "detail": problem, "elapsed": 0.0}
    outcome = {"status": "typed_error" if not reply.get("ok") else "ok", "elapsed": 0.0}
    deadline = time.time() + QUIT_TIMEOUT
    while time.time() < deadline and process.poll() is None:
        time.sleep(0.1)
    if process.poll() is None:
        outcome = {"status": "timeout", "detail": "the app did not exit after control.quit"}
    elif process.returncode != 0:
        outcome = {"status": "dead", "detail": "exit %s" % process.returncode}
    return outcome


def run_live(options):
    started = time.time()
    tmp = tempfile.mkdtemp(prefix="zsurface-", dir="/tmp")
    process = None
    log_file = None
    log_path = None
    try:
        baseline = load_baseline(options.baseline)
        allowlist = load_reasons(options.allowlist)
        exempt = load_reasons(options.exempt)

        process, log_file, socket_path, log_path, client = connect_session(options, tmp)
        fixture_before = file_sha256(options.fixture)

        # Reflection first, so the run is read before project.open can touch the
        # recent-projects list (which is exempt anyway, but determinism is free).
        surface = fetch(client, 901, "control.surface_report").get("actions", [])
        log("   control.surface_report: %d reflected actions" % len(surface))
        reply = fetch(client, 902, "control.commands_list")
        commands = {entry["id"]: entry for entry in reply.get("commands", [])}
        log("   control.commands_list: %d registered commands" % len(commands))
        # A real model behind the mixer/track commands. 903 is unused.
        fetch(client, 903, "project.open", {"path": options.fixture})
        overrides = live_overrides(client, options.fixture, tmp)

        sweep = sweep_commands(client, commands, allowlist, overrides, options.budget, started)
        sweep["control.quit"] = quit_and_wait(client, process, 904)
        # The gate may exercise write commands, but never in the repository.
        changed = file_sha256(options.fixture) != fixture_before
        return verdict(options, started, surface, commands, sweep, baseline, allowlist, exempt,
                       changed)
    finally:
        cleanup(log_file, process, log_path, tmp)


def connect_session(options, tmp):
    """Launch, wait for the socket, insist on mode 0600, wait for the engine."""
    process, log_file, socket_path, log_path = launch(options.binary, tmp)
    wait_for_socket(socket_path, process)
    mode = stat.S_IMODE(os.stat(socket_path).st_mode)
    if mode != 0o600:
        raise RuntimeError("socket mode is 0o%o, expected 0o600" % mode)
    client = Client(socket_path, COMMAND_TIMEOUT)
    wait_for_engine(client, process, 1)
    return process, log_file, socket_path, log_path, client


def verdict(options, started, surface, commands, sweep, baseline, allowlist, exempt, changed):
    """Judge the collected evidence and say so."""
    problems, stats = evaluate(surface, commands, baseline, allowlist, exempt, sweep,
                              COMMAND_TIMEOUT, frozenset(options.compiled_out))
    if changed:
        problems.append("the gate wrote to its own fixture project (%s); every sweep argument "
                        "that names a file must point into the temp directory"
                        % options.fixture)
    elapsed = time.time() - started
    emit_report(stats, sweep, problems, elapsed, options.budget, options.report)
    if elapsed > options.budget:
        problems.append("the gate took %.1f s, over its %.0f s budget - it has to stay "
                        "cheap enough for the default suite" % (elapsed, options.budget))
    if options.reanchor is not None:
        return reanchor(options.baseline, options.reanchor, surface, exempt, problems, log)
    if problems:
        log("")
        log("---- %d problem(s) ----" % len(problems))
        for problem in problems:
            fail(problem)
        log("")
        log("FAIL: agent surface gate (%d problem(s))" % len(problems))
        return 1
    log("")
    log("PASS: agent surface gate (reflection + ratchet + reverse completeness + headless sweep)")
    return 0


def cleanup(log_file, process, log_path, tmp):
    if log_file is not None:
        log_file.close()
    if process is not None and process.poll() is None:
        fail("the app is still running; killing it (tail of its log below)")
        if log_path:
            try:
                with open(log_path, "r", errors="replace") as handle:
                    log(handle.read()[-4000:])
            except OSError:
                pass
        process.kill()
        process.wait()
    shutil.rmtree(tmp, ignore_errors=True)


def parse_args(argv):
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("binary", nargs="?")
    parser.add_argument("fixture", nargs="?")
    parser.add_argument("--check", action="store_true",
                        help="report only (the default: this gate never writes the baseline "
                             "unless --reanchor is given)")
    parser.add_argument("--reanchor", metavar="REASON",
                        help="deliberate baseline refresh; an empty reason is refused")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--baseline", default=DEFAULT_BASELINE)
    parser.add_argument("--allowlist", default=DEFAULT_ALLOWLIST)
    parser.add_argument("--exempt", default=DEFAULT_EXEMPT)
    parser.add_argument("--compiled-out", action="append", default=[], metavar="COMMAND_ID",
                        help="a command id this configuration compiled out; its allowlist entry "
                             "is accounted for by the switch, refused if the registry declares "
                             "it. tests/CMakeLists.txt passes it for -DZENE_TELEMETRY=OFF.")
    parser.add_argument("--budget", type=float, default=BUDGET_SECONDS)
    parser.add_argument("--report", default=None)
    return parser.parse_args(argv)


def validate(options):
    """Setup errors, where the gate has no verdict to give: 0 means carry on."""
    if not options.binary or not options.fixture:
        log("usage: %s <lmms-binary> <fixture.mmp> [--check|--reanchor \"reason\"] "
            "[--self-test]" % os.path.basename(sys.argv[0]))
        return 2
    if options.reanchor is not None and not options.reanchor.strip():
        log('usage: --reanchor "reason" - an unrecorded re-anchor is not allowed')
        return 2
    for label, path in (("lmms binary", options.binary), ("fixture project", options.fixture)):
        if not os.path.exists(path):
            log("FAIL: no %s at %s" % (label, path))
            return 2
    return 0


def main(argv=None):
    options = parse_args(argv if argv is not None else sys.argv[1:])
    log("agent_surface gate (SPEC A15) - baseline=%s" % os.path.relpath(options.baseline, HERE))
    if options.self_test:
        return self_test(log, COMMAND_TIMEOUT)
    setup = validate(options)
    if setup:
        return setup
    options.binary = os.path.abspath(options.binary)
    options.fixture = os.path.abspath(options.fixture)
    try:
        return run_live(options)
    except (RuntimeError, ValueError) as error:
        fail("%s" % error)
        log("\n---- last request/response lines ----")
        for line in TRANSCRIPT[-40:]:
            log(line)
        return 1


if __name__ == "__main__":
    sys.exit(main())
