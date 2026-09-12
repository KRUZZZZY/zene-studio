#!/usr/bin/env python3
"""Shared harness for the control-surface shutdown/readiness tests (task #626).

Every test in this directory starts the REAL `lmms` binary with
`--control-socket <path>` under `QT_QPA_PLATFORM=offscreen` and drives it from an
external AF_UNIX client. This module owns the parts they share:

  * `Instance` - temp dir, `--config` file, HOME/XDG_* isolation, spawn, bounded
    wait for the socket, bounded stop. A hang is always a FAILURE, never a wait:
    every loop carries a deadline and `require()` turns an expired deadline into a
    failed assertion with the evidence attached.
  * `Client`   - one request / one response per line, bounded reads.
  * the CHECKERS (`check_clean_shutdown`, `check_ping_shape`,
    `check_readiness_reason`, `check_audio_fallback`) - pure functions from
    observed evidence to a list of problems. `control-negative-control.py` feeds
    them deliberately defective evidence to prove they can fail; that is what
    keeps them from being tautologies.

The checker boundary matters: the tests never assert "the run looked fine", they
assert on the exit code, the socket file, the exact stderr text and the JSON
fields, and the negative control shows each of those assertions failing.
"""

import json
import os
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

# The pre-fix workaround line (reproduction (a) and (b) both printed it). Kept
# verbatim so the negative control can prove the shutdown checker rejects it.
LEGACY_WATCHDOG_LINE = "control.quit: the event loop did not stop"
# The last-resort guard this lane added. It must NEVER fire on a normal
# shutdown: if it does, the shutdown path is broken again.
FATAL_GUARD_MARKER = "FATAL: the normal shutdown did not finish"

DEFAULT_DEVICE = "Dummy (no sound output)"
# A backend that cannot open, deterministically. "SDL (Simple DirectMedia Layer)"
# is AudioSdl::name() exactly (the app compares the configured value against that
# string, so "SDL" is NOT a valid device name - an invalid name makes the engine
# try every backend instead, which is how the first attempt at this test fooled
# itself), and SDL_AUDIODRIVER names a driver that cannot exist, so SDL_Init
# fails on any machine. That reproduces the SDL/ALSA "Playback open error: Host is
# down" situation without depending on the box having no sound card.
BROKEN_DEVICE = "SDL (Simple DirectMedia Layer)"
BROKEN_DEVICE_ENV = {"SDL_AUDIODRIVER": "zene-no-such-audio-driver"}

SOCKET_MODE = 0o600


class Timeout(Exception):
    """A bounded wait expired. Always a test failure, never a skip."""


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


# ---------------------------------------------------------------------------
# config / launching
# ---------------------------------------------------------------------------


class Instance:
    """One real `lmms` process with a control socket, in its own temp world."""

    def __init__(self, binary, audiodev=DEFAULT_DEVICE, autosave=True, extra_xml="", extra_env=None):
        self.binary = os.path.abspath(binary)
        if not os.path.exists(self.binary):
            raise Timeout("no lmms binary at %s" % self.binary)
        self.extra_env = dict(extra_env or {})
        self.tmp = tempfile.mkdtemp(prefix="zctl-run-", dir="/tmp")
        self.socket_path = os.path.join(self.tmp, "zene.sock")
        self.workspace = os.path.join(self.tmp, "workspace")
        os.makedirs(self.workspace)
        self.recovery_file = os.path.join(self.workspace, "recover.mmp")
        self.config_path = os.path.join(self.tmp, "lmmsrc.xml")
        self.stderr_path = os.path.join(self.tmp, "stderr.log")
        self.stdout_path = os.path.join(self.tmp, "stdout.log")
        self.process = None
        self._stderr = None
        self._stdout = None

        settings = [
            '  <app configured="1"/>',
            '  <audioengine audiodev="%s"/>' % audiodev,
            '  <paths workingdir="%s"/>' % self.workspace,
        ]
        if autosave:
            # Needed for the normal shutdown to run its autosave cleanup
            # (MainWindow::closeEvent -> sessionCleanup).
            settings.append('  <ui enableautosave="1" saveinterval="60"/>')
        if extra_xml:
            settings.append(extra_xml)
        with open(self.config_path, "w") as handle:
            handle.write('<?xml version="1.0"?>\n'
                         '<!DOCTYPE lmms-config-file>\n'
                         '<lmmsconfig version="0.2.0-alpha" configversion="3">\n'
                         + "\n".join(settings) + "\n"
                         '</lmmsconfig>\n')

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

    def socket_exists(self):
        return os.path.exists(self.socket_path)

    def stderr_text(self):
        try:
            with open(self.stderr_path, errors="replace") as handle:
                return handle.read()
        except OSError:
            return ""

    def wait_for_socket(self, timeout_s):
        """Poll for the socket file AND a connectable listener, connect ASAP."""
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if self.process.poll() is not None:
                raise Timeout("the app exited before the control socket was usable "
                              "(exit %s)" % self.process.returncode)
            if self.socket_exists():
                try:
                    probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    probe.settimeout(0.5)
                    probe.connect(self.socket_path)
                    probe.close()
                    return time.time()
                except OSError:
                    pass
            time.sleep(0.005)
        raise Timeout("the control socket %s never became connectable within %.1fs"
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
        if self._stderr:
            self._stderr.close()
        if self._stdout:
            self._stdout.close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
        return False


class Client:
    """Line-delimited JSON-RPC client with a bounded read per request."""

    def __init__(self, path, timeout_s=30.0):
        self.timeout = timeout_s
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout_s)
        self.sock.connect(path)
        self.buffer = b""
        self.transcript = []

    def call(self, request_id, cmd, args=None, proto=1):
        request = {"id": request_id, "cmd": cmd, "args": args or {}, "proto": proto}
        raw = json.dumps(request, separators=(",", ":"))
        self.transcript.append("-> %s" % raw)
        try:
            self.sock.sendall(raw.encode("utf-8") + b"\n")
        except OSError as exc:
            raise Timeout("could not send %s: %s" % (cmd, exc)) from exc
        reply = self._read_line()
        self.transcript.append("<- %s" % reply.decode("utf-8", "replace"))
        return json.loads(reply)

    def _read_line(self):
        # socket timeouts arrive as the builtin TimeoutError (socket.timeout is an
        # alias of it since 3.10) and a frozen or killed server can also give EOF
        # or ECONNRESET; all of them are "no answer", which is a bounded failure,
        # never a crash inside a test.
        deadline = time.time() + self.timeout
        while b"\n" not in self.buffer:
            if time.time() > deadline:
                raise Timeout("no response line within %.1fs (last transcript: %r)"
                              % (self.timeout, self.transcript[-2:]))
            try:
                chunk = self.sock.recv(65536)
            except (TimeoutError, OSError) as exc:
                raise Timeout("no response line within %.1fs (%s) (last transcript: %r)"
                              % (self.timeout, exc, self.transcript[-2:])) from exc
            if not chunk:
                raise Timeout("the server closed the connection without answering")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# checkers - pure evidence -> problems
# ---------------------------------------------------------------------------


def check_clean_shutdown(exited, exit_code, socket_exists, stderr_text, elapsed_s,
                         expect_socket_unlinked=True):
    """A clean shutdown: gone in time, exit 0, no watchdog, socket unlinked."""
    problems = []
    if not exited:
        problems.append("the process did not exit within the bounded wait (%.1fs); "
                        "the shutdown is blocked" % elapsed_s)
    elif exit_code != 0:
        problems.append("the process exited with %s, expected 0" % exit_code)
    if LEGACY_WATCHDOG_LINE in stderr_text:
        problems.append("stderr carries the pre-fix watchdog line %r: the event loop did not stop"
                        % LEGACY_WATCHDOG_LINE)
    if FATAL_GUARD_MARKER in stderr_text:
        problems.append("stderr carries the last-resort guard line: the normal shutdown did not "
                        "complete (it must never fire)")
    if expect_socket_unlinked and socket_exists:
        problems.append("the control socket file was not unlinked on exit")
    return problems


def check_ping_shape(reply, request_id=1):
    """control.ping carries liveness, readiness and the audio report, always."""
    problems = []
    if reply.get("id") != request_id:
        problems.append("ping reply id %r != %r" % (reply.get("id"), request_id))
    if reply.get("ok") is not True:
        problems.append("ping answered %r, expected ok=true (ping must work before the engine "
                        "is ready - that is its whole purpose)" % reply)
        return problems
    result = reply.get("result") or {}
    if result.get("pong") is not True:
        problems.append("ping result has no pong=true: %r" % result)
    if not isinstance(result.get("engine_ready"), bool):
        problems.append("ping result has no boolean engine_ready: %r" % result)
    audio = result.get("audio")
    if not isinstance(audio, dict):
        problems.append("ping result has no audio report: %r" % result)
    else:
        for field in ("state", "device", "requested", "start_failed", "sound_output"):
            if field not in audio:
                problems.append("ping audio report is missing '%s': %r" % (field, audio))
    return problems


def check_readiness_reason(reply):
    """While engine_ready is false, ping must say WHY - a code and a message."""
    problems = []
    result = reply.get("result") or {}
    if result.get("engine_ready") is not False:
        problems.append("expected engine_ready=false to check the reason, got %r" % result)
        return problems
    reason = result.get("reason")
    if not isinstance(reason, dict):
        problems.append("engine_ready=false with no 'reason' object: a bare false is exactly "
                        "the defect this test targets: %r" % result)
        return problems
    if not reason.get("code"):
        problems.append("reason carries no code: %r" % reason)
    if not reason.get("message"):
        problems.append("reason carries no message: %r" % reason)
    return problems


def check_busy_carries_reason(reply):
    """An engine command issued while not ready is a typed busy error, explained."""
    problems = []
    if reply.get("ok") is not False:
        problems.append("an engine command before readiness answered %r, expected a typed "
                        "failure" % reply)
        return problems
    error = reply.get("error") or {}
    if error.get("kind") != "busy":
        problems.append("expected error.kind=busy before readiness, got %r" % error)
    message = error.get("message") or ""
    if not message:
        problems.append("the busy error carries no message: %r" % error)
    return problems


def check_audio_fallback(reply, requested):
    """The no-sound-card report: names the backend that failed, and the fallback."""
    problems = []
    result = reply.get("result") or {}
    audio = result.get("audio") or {}
    if audio.get("start_failed") is not True:
        problems.append("the configured device '%s' cannot open on this box, but the audio "
                        "report says start_failed=%r" % (requested, audio.get("start_failed")))
    if audio.get("state") != "dummy_fallback":
        problems.append("audio.state is %r, expected 'dummy_fallback' (the product fell back "
                        "to the dummy device and must say so)" % audio.get("state"))
    if audio.get("sound_output") is not False:
        problems.append("audio.sound_output is %r, expected false (a dummy device makes no "
                        "sound)" % audio.get("sound_output"))
    if requested not in (audio.get("requested") or ""):
        problems.append("the report does not name the requested backend %r: %r"
                        % (requested, audio.get("requested")))
    if not (audio.get("message") or ""):
        problems.append("the fallback is silent: no audio.message explaining the failure: %r"
                        % audio)
    return problems


def check_requires_device_refusal(reply):
    """A device-dependent command refuses, typed, naming the backend."""
    problems = []
    if reply.get("ok") is not False:
        problems.append("transport.play answered %r on an instance whose audio device failed "
                        "to open; it must refuse with a typed error" % reply)
        return problems
    error = reply.get("error") or {}
    if error.get("kind") != "requires":
        problems.append("expected error.kind=requires, got %r" % error)
    message = error.get("message") or ""
    if not message:
        problems.append("the refusal carries no message: %r" % error)
    return problems


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
