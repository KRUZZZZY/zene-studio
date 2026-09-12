#!/usr/bin/env python3
"""THE shared socket harness for every control-surface test in this directory.

ONE harness, not two. Before 2026-09-12 there were two - `control_harness.py`
(task #626: shutdown/readiness) and `headless_load_harness.py` (task #625:
headless load) - and they had drifted: two `Instance` classes with different
signatures, two `Client` classes with different `call()` signatures, and two
predicates for "did this run block". Both are merged here into the single module
every test imports, so a fix to the launch recipe or a bound lands in one place.

Every test here starts the REAL `lmms` binary with `--control-socket <path>`
under `QT_QPA_PLATFORM=offscreen` and drives it from an EXTERNAL client over the
AF_UNIX socket. The one rule that matters:

    a HANG IS A FAILURE.

So every wait in this module is BOUNDED. A bound that expires raises `Blocked`
(the task #625 signature: the instance is parked inside a modal dialog's nested
event loop, so Qt keeps dispatching socket events and `control.ping` answers
`pong:true engine_ready:false` forever) or `Timeout` (the task #626 signature for
a bounded wait inside a test). Both are always a test failure, never a skip.

The headless start recipe (AGENT-TOOLING.md section 4) is reproduced exactly:
`QT_QPA_PLATFORM=offscreen`, a `--config` whose
`<audioengine audiodev="Dummy (no sound output)"/>` matches AudioDummy::name()
by character, `HOME`/`XDG_*` in a temp directory, cwd in the temp directory.

This module is the CORE: constants and bounds, `Instance`, `Client`, the readiness
poll, the assertion helpers (`ok_result`, `typed_error`, `Problems`, `fail`, `ok`)
and the reporting helpers (`dump`, `finish`). The per-case payloads - the pure
`check_*` "evidence -> problems" checkers and the multi-instance flow helpers
(`healthy_control`, `diagnose_block`, `parse_args`, `report_pre_fix`) - live in
`control_socket_flows.py`, which imports their plumbing from here. The split is
mechanical (Gate 7, the 500-line per-file ratchet); no behaviour moved with it.

Usage (each test script owns its own argv):
    QT_QPA_PLATFORM=offscreen python3 <test>.py <lmms> [...]
Exit code 0 only when every assertion passed.
"""

import json
import os
import shutil
import socket
import stat  # noqa: F401  (kept: tests import it through this module historically)
import subprocess
import sys
import tempfile
import time
from typing import NoReturn

# ---------------------------------------------------------------------------
# bounds and constants
# ---------------------------------------------------------------------------

# A healthy instance answers a ping in milliseconds; these numbers are generous
# enough for a loaded build box and small enough that a hang costs seconds.
SOCKET_TIMEOUT = 30.0
PING_TIMEOUT = 10.0
READY_TIMEOUT = 120.0
OPEN_TIMEOUT = 20.0
QUIT_TIMEOUT = 30.0

# The device AudioDummy::name() matches by character: the app compares the
# configured value against that string, so it must be exact.
DUMMY_DEVICE = "Dummy (no sound output)"
DEFAULT_DEVICE = DUMMY_DEVICE

# A backend that cannot open, deterministically. "SDL (Simple DirectMedia Layer)"
# is AudioSdl::name() exactly (so "SDL" is NOT a valid device name - an invalid
# name makes the engine try every backend instead, which is how the first attempt
# at this test fooled itself), and SDL_AUDIODRIVER names a driver that cannot
# exist, so SDL_Init fails on any machine. That reproduces the SDL/ALSA
# "Playback open error: Host is down" situation without depending on the box
# having no sound card.
BROKEN_DEVICE = "SDL (Simple DirectMedia Layer)"
BROKEN_DEVICE_ENV = {"SDL_AUDIODRIVER": "zene-no-such-audio-driver"}

SOCKET_MODE = 0o600

# The pre-fix workaround line (task #626 reproductions (a) and (b) both printed
# it). Kept verbatim so the negative control can prove the shutdown checker
# rejects it.
LEGACY_WATCHDOG_LINE = "control.quit: the event loop did not stop"
# The last-resort guard this lane added. It must NEVER fire on a normal
# shutdown: if it does, the shutdown path is broken again.
FATAL_GUARD_MARKER = "FATAL: the normal shutdown did not finish"


# ---------------------------------------------------------------------------
# failures
# ---------------------------------------------------------------------------


class Timeout(Exception):
    """A bounded wait expired. Always a test failure, never a skip."""


class Blocked(Timeout):
    """The instance did not become usable inside the bound (task #625).

    A SUBCLASS of Timeout on purpose: the two harnesses this module replaces used
    two names for the same fact - "bounded wait expired" (#626, `Timeout`) and
    "the run is parked in a modal dialog" (#625, `Blocked`). A caller that only
    cares that it did not answer catches Timeout; a caller that wants to name the
    task #625 signature catches Blocked. Neither had to be rewritten.

    `QMessageBox::exec()` and `QDialog::exec()` run a NESTED event loop: Qt keeps
    dispatching events, so the control socket is still serviced while the box is
    up and `control.ping` keeps answering `pong:true, engine_ready:false` for as
    long as nobody clicks. A block is "never usable inside the bound", not "no
    bytes on the socket".
    """


def fail(message, instance=None, transcript=None) -> NoReturn:
    print("\nFAIL: %s" % message)
    if transcript is not None:
        transcript.dump()
    if instance is not None:
        instance.dump_log()
        instance.close()
    sys.exit(1)


def ok(message):
    print("PASS: %s" % message)


class Problems:
    """Accumulates failed checks with their evidence."""

    def __init__(self):
        self.items = []

    def add(self, text):
        self.items.append(text)

    def extend(self, texts):
        self.items.extend(texts)

    def require(self, condition, text):
        if not condition:
            self.add(text)
        return bool(condition)

    def __bool__(self):
        # True when there ARE problems: the tests phrase it as `if problems:`.
        return bool(self.items)

    def report(self, headline):
        if self.items:
            print("FAIL: %s" % headline)
            for item in self.items:
                print("  - %s" % item)
        return not self.items


class Transcript:
    """Every request and response, in order, for the acceptance evidence."""

    def __init__(self):
        self.lines = []

    def add(self, direction, payload):
        self.lines.append("%s %s" % (direction, payload))

    def dump(self):
        print("\n---- raw request/response transcript ----")
        for line in self.lines:
            print(line)


# ---------------------------------------------------------------------------
# config / launching
# ---------------------------------------------------------------------------


def config_xml(workingdir, audiodev=DUMMY_DEVICE, configured=1, autosave=False, extra_xml=""):
    """The minimal headless config.

    `workingdir` need not exist (task #625: a missing working directory used to
    park the run in the setup dialog). `autosave` adds the `ui enableautosave`
    element the normal-shutdown autosave cleanup checks.
    """
    settings = [
        '  <app configured="%d"/>' % configured,
        '  <audioengine audiodev="%s"/>' % audiodev,
        '  <paths workingdir="%s"/>' % workingdir,
    ]
    if autosave:
        # Needed for the normal shutdown to run its autosave cleanup
        # (MainWindow::closeEvent -> sessionCleanup).
        settings.append('  <ui enableautosave="1" saveinterval="60"/>')
    if extra_xml:
        settings.append(extra_xml)
    return ('<?xml version="1.0"?>\n'
            '<!DOCTYPE lmms-config-file>\n'
            '<lmmsconfig version="0.2.0-alpha" configversion="3">\n'
            + "\n".join(settings) + "\n"
            '</lmmsconfig>\n')


class Instance:
    """One real `lmms` process with a control socket, in its own temp world."""

    def __init__(self, binary, audiodev=DEFAULT_DEVICE, autosave=True, extra_xml="",
                 extra_env=None, workingdir=None, configured=1):
        self.binary = os.path.abspath(binary)
        if not os.path.exists(self.binary):
            raise Timeout("no lmms binary at %s" % self.binary)
        self.extra_env = dict(extra_env or {})
        self.tmp = tempfile.mkdtemp(prefix="zctl-run-", dir="/tmp")
        self.socket_path = os.path.join(self.tmp, "zene.sock")
        # workingdir=None means "a working directory that exists": a fresh one is
        # created inside the instance's temp dir.  Pass an explicit path to drive
        # the missing-working-directory case (task #625 BLOCKER 2).
        self.workspace = workingdir if workingdir is not None else os.path.join(self.tmp, "workspace")
        if workingdir is None:
            os.makedirs(self.workspace)
        self.recovery_file = os.path.join(self.tmp, "workspace", "recover.mmp")
        self.config_path = os.path.join(self.tmp, "lmmsrc.xml")
        self.stderr_path = os.path.join(self.tmp, "stderr.log")
        self.stdout_path = os.path.join(self.tmp, "stdout.log")
        # The app's diagnostics (qWarning, the typed refusals repeated on stderr,
        # the audio-failure sentence) go to stderr, so that is "the app log" the
        # tests look in.  read_log() returns stderr + stdout, a superset.
        self.log_path = self.stderr_path
        self.process = None
        self._stderr = None
        self._stdout = None
        with open(self.config_path, "w") as handle:
            handle.write(config_xml(self.workspace, audiodev, configured, autosave, extra_xml))

    def env(self):
        env = dict(os.environ)
        env["QT_QPA_PLATFORM"] = "offscreen"
        env["HOME"] = self.tmp
        env["XDG_CONFIG_HOME"] = os.path.join(self.tmp, "config")
        env["XDG_DATA_HOME"] = os.path.join(self.tmp, "data")
        env.update(self.extra_env)
        os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)
        os.makedirs(env["XDG_DATA_HOME"], exist_ok=True)
        return env

    def spawn(self):
        self._stderr = open(self.stderr_path, "wb")
        self._stdout = open(self.stdout_path, "wb")
        self.process = subprocess.Popen(
            [self.binary, "--config", self.config_path, "--control-socket", self.socket_path],
            stdout=self._stdout, stderr=self._stderr, env=self.env(), cwd=self.tmp)
        return self.process

    def alive(self):
        return self.process is not None and self.process.poll() is None

    def socket_exists(self):
        return os.path.exists(self.socket_path)

    def _read(self, path):
        try:
            with open(path, errors="replace") as handle:
                return handle.read()
        except OSError:
            return ""

    def read_log(self):
        """stderr then stdout: everything the app wrote, in one string."""
        return self._read(self.stderr_path) + self._read(self.stdout_path)

    def stderr_text(self):
        return self._read(self.stderr_path)

    def dump_log(self):
        print("\n---- app log (%s) ----" % self.stderr_path)
        print(self.read_log()[-8000:])

    def wait_for_socket(self, timeout_s):
        """Poll for the socket file AND a connectable listener.

        The listener is created before the GUI and the engine (main.cpp), so this
        succeeds even on an instance that is about to hang in a modal dialog: the
        accept() backlog takes the connection with nobody reading it.
        """
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if self.process.poll() is not None:
                raise Blocked("the instance exited (code %s) before its control socket was usable"
                              % self.process.returncode)
            if self.socket_exists():
                try:
                    probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    probe.settimeout(1.0)
                    probe.connect(self.socket_path)
                    probe.close()
                    return time.time()
                except OSError:
                    pass
            time.sleep(0.02)
        raise Blocked("the control socket %s was never connectable within %.1fs"
                      % (self.socket_path, timeout_s))

    def wait_for_exit(self, timeout_s):
        """Returns (exited, exit_code, elapsed_s). Bounded: a hang is returned."""
        start = time.time()
        deadline = start + timeout_s
        while time.time() < deadline:
            if self.process.poll() is not None:
                return True, self.process.returncode, time.time() - start
            time.sleep(0.02)
        return False, None, time.time() - start

    def kill(self):
        if self.process is not None and self.process.poll() is None:
            self.process.kill()
            self.process.wait()

    def close(self):
        self.kill()
        for handle in (self._stderr, self._stdout):
            if handle:
                try:
                    handle.close()
                except (OSError, ValueError):
                    pass
        shutil.rmtree(self.tmp, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
        return False


def start_instance(binary, workingdir=None, audiodev=DUMMY_DEVICE, configured=1):
    """Construct and spawn an Instance. The socket may not be up yet."""
    instance = Instance(binary, audiodev=audiodev, configured=configured, workingdir=workingdir)
    instance.spawn()
    return instance


class Client:
    """Line-delimited JSON-RPC client with a bounded read per request."""

    def __init__(self, path, timeout_s=30.0):
        self.timeout = timeout_s
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout_s)
        self.sock.connect(path)
        self.buffer = b""
        self.transcript = []

    def call(self, request_id, cmd, args=None, proto=1, timeout=None, transcript=None):
        request = {"id": request_id, "cmd": cmd, "args": args or {}, "proto": proto}
        raw = json.dumps(request, separators=(",", ":"))
        self.transcript.append("-> %s" % raw)
        if transcript is not None:
            transcript.add("->", raw)
        try:
            self.sock.sendall(raw.encode("utf-8") + b"\n")
        except OSError as exc:
            raise Timeout("could not send %s: %s" % (cmd, exc)) from exc
        try:
            reply = self._read_line(self.timeout if timeout is None else timeout)
        except (Blocked, OSError) as error:
            if transcript is not None:
                transcript.add("<-", "NO REPLY inside %.0fs (%s)" % (self.timeout, error))
            raise
        text = reply.decode("utf-8", "replace")
        self.transcript.append("<- %s" % text)
        if transcript is not None:
            transcript.add("<-", text)
        return json.loads(text)

    def _read_line(self, timeout):
        # socket timeouts arrive as the builtin TimeoutError (socket.timeout is an
        # alias of it since 3.10) and a frozen or killed server can also give EOF
        # or ECONNRESET; all of them are "no answer", which is a bounded failure,
        # never a crash inside a test.
        deadline = time.time() + timeout
        self.sock.settimeout(timeout)
        while b"\n" not in self.buffer:
            if time.time() > deadline:
                raise Blocked("no response line inside %.1fs (last transcript: %r)"
                              % (timeout, self.transcript[-2:]))
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                raise Blocked("no response line inside %.1fs (socket timed out)" % timeout)
            except OSError as exc:
                raise Blocked("no response line inside %.1fs (%s) (last transcript: %r)"
                              % (timeout, exc, self.transcript[-2:])) from exc
            if not chunk:
                raise Blocked("the server closed the connection without answering")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def wait_for_socket(instance, seconds=SOCKET_TIMEOUT):
    """Module-level form of Instance.wait_for_socket (raises Blocked)."""
    return instance.wait_for_socket(seconds)


def connect(instance, seconds=SOCKET_TIMEOUT):
    wait_for_socket(instance, seconds)
    return Client(instance.socket_path)


def wait_ready(instance, client, transcript, seconds=READY_TIMEOUT, ping_timeout=PING_TIMEOUT):
    """Poll control.ping until engine_ready is true. Blocked when it never answers."""
    deadline = time.time() + seconds
    last = None
    while time.time() < deadline:
        if not instance.alive():
            raise Blocked("the instance exited (code %s) while waiting for the engine"
                          % instance.process.returncode)
        last = client.call(0, "control.ping", timeout=ping_timeout, transcript=transcript)
        result = last.get("result") or {}
        if last.get("ok") and result.get("engine_ready"):
            return last
        time.sleep(0.2)
    raise Blocked("the engine never became ready within %.0fs (last ping: %r)" % (seconds, last))


def ok_result(reply, expected_id):
    if reply.get("id") != expected_id:
        raise AssertionError("reply id %r != %r" % (reply.get("id"), expected_id))
    if reply.get("ok") is not True:
        raise AssertionError("expected ok=true, got %r" % (reply,))
    return reply.get("result") or {}


def typed_error(reply, expected_id, expected_kind):
    if reply.get("id") != expected_id:
        raise AssertionError("reply id %r != %r" % (reply.get("id"), expected_id))
    if reply.get("ok") is not False:
        raise AssertionError("expected ok=false, got %r" % (reply,))
    error = reply.get("error") or {}
    if error.get("kind") != expected_kind:
        raise AssertionError("expected error.kind=%r, got %r" % (expected_kind, error))
    if not error.get("message"):
        raise AssertionError("the typed error carries no message: %r" % (reply,))
    return error


# ---------------------------------------------------------------------------
# reporting helpers
# ---------------------------------------------------------------------------


def dump(label, text):
    print("---- %s ----" % label)
    print(text)
    print("---- end %s ----" % label)


def finish(results):
    """results: list of (name, ok, problems). Exits 0 only when all passed."""
    failed = [name for name, ok, _ in results if not ok]
    print("")
    print("=== %s ===" % ("PASS" if not failed else "FAIL"))
    for name, ok, problems in results:
        print("  %-46s %s" % (name, "ok" if ok else "FAILED (%d)" % len(problems)))
        for item in problems:
            print("      %s" % item)
    return 0 if not failed else 1
