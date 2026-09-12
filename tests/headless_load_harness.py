#!/usr/bin/env python3
"""Shared harness for the task #625 headless-load tests (SPEC A13).

Every test here starts the real `lmms` binary with `--control-socket <path>`
under QT_QPA_PLATFORM=offscreen and drives it from an EXTERNAL client over the
AF_UNIX socket.  The one rule that matters:

    a HANG IS A FAILURE.

So every wait in this module is BOUNDED, and a bound that expires raises
`Blocked` instead of blocking the test forever.  `Blocked` is the diagnosis of
the pre-fix defect (task #625): startup or a load is parked inside a modal
dialog's nested event loop, so the instance never becomes usable.  Measured on
the base commit 6b01b98eb, such an instance keeps answering `control.ping`
(`engine_ready:false`) forever because Qt dispatches socket events inside the
nested loop - the block is read as "never ready", not as "no reply".  The
pre-fix reproductions are this module's `--expect-blocked` mode, which asserts
that a bound DOES expire, that the process is still alive, and which of the two
signatures (nested modal loop still answering / silence) was measured.

The headless start recipe (AGENT-TOOLING.md section 4) is reproduced here
exactly: `QT_QPA_PLATFORM=offscreen`, a `--config` whose
`<audioengine audiodev="Dummy (no sound output)"/>` matches AudioDummy::name()
by character, `HOME`/`XDG_*` in a temp directory, cwd in /tmp.

Usage (each test script owns its own argv):
    QT_QPA_PLATFORM=offscreen python3 <test>.py <lmms> [...]
Exit code 0 only when every assertion passed.
"""

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from typing import NoReturn

# Bounds. A healthy instance answers a ping in milliseconds; these numbers are
# generous enough for a loaded build box and small enough that a hang costs
# seconds, not minutes.
SOCKET_TIMEOUT = 30.0
PING_TIMEOUT = 10.0
READY_TIMEOUT = 120.0
OPEN_TIMEOUT = 20.0
QUIT_TIMEOUT = 30.0

DUMMY_DEVICE = "Dummy (no sound output)"


class Blocked(Exception):
    """The instance did not become usable inside the bound.

    This is the task #625 signature.  `QMessageBox::exec()` and
    `QDialog::exec()` run a NESTED event loop: Qt keeps dispatching events, so
    (measured on the base commit 6b01b98eb) the control socket is still
    serviced while the box is up and `control.ping` keeps answering
    `pong:true, engine_ready:false` for as long as nobody clicks.  The startup
    path can therefore never finish, and the instance answers every engine
    command with the typed `busy` refusal forever.  A block is "never usable
    inside the bound", not "no bytes on the socket".
    """


class Transcript(object):
    """Every request and response, in order, for the acceptance evidence."""

    def __init__(self):
        self.lines = []

    def add(self, direction, payload):
        self.lines.append("%s %s" % (direction, payload))

    def dump(self):
        print("\n---- raw request/response transcript ----")
        for line in self.lines:
            print(line)


def fail(message, instance=None, transcript=None) -> NoReturn:
    print("\nFAIL: %s" % message)
    if transcript is not None:
        transcript.dump()
    if instance is not None:
        instance.dump_log()
        instance.kill()
    sys.exit(1)


def ok(message):
    print("PASS: %s" % message)


class Instance(object):
    """A running `lmms --control-socket` instance plus its log."""

    def __init__(self, binary, tmp, config_path, socket_path, log_path, process, log_handle):
        self.binary = binary
        self.tmp = tmp
        self.config_path = config_path
        self.socket_path = socket_path
        self.log_path = log_path
        self.process = process
        self.log_handle = log_handle

    def alive(self):
        return self.process.poll() is None

    def read_log(self):
        try:
            with open(self.log_path, "r", errors="replace") as handle:
                return handle.read()
        except OSError:
            return ""

    def dump_log(self):
        print("\n---- app log (%s) ----" % self.log_path)
        print(self.read_log()[-8000:])

    def kill(self):
        try:
            self.log_handle.close()
        except (OSError, ValueError):
            pass
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        shutil.rmtree(self.tmp, ignore_errors=True)


def config_xml(workingdir, audiodev=DUMMY_DEVICE, configured=1):
    """The minimal headless config. `workingdir` need not exist (task #625)."""
    return (
        '<?xml version="1.0"?>\n'
        '<!DOCTYPE lmms-config-file>\n'
        '<lmmsconfig version="0.2.0-alpha" configversion="3">\n'
        '  <app configured="%d"/>\n'
        '  <audioengine audiodev="%s"/>\n'
        '  <paths workingdir="%s"/>\n'
        '</lmmsconfig>\n' % (configured, audiodev, workingdir)
    )


def start_instance(binary, workingdir=None, audiodev=DUMMY_DEVICE, configured=1):
    """Start the instance. Returns an Instance (the socket may not be up yet).

    `workingdir=None` means "a working directory that exists": a fresh one is
    created inside the instance's temp dir.  Pass an explicit path to drive the
    missing-working-directory case (task #625 BLOCKER 2).
    """
    tmp = tempfile.mkdtemp(prefix="z625-", dir="/tmp")
    if workingdir is None:
        workingdir = os.path.join(tmp, "workspace")
        os.makedirs(workingdir)
    socket_path = os.path.join(tmp, "zene.sock")
    config_path = os.path.join(tmp, "lmmsrc.xml")
    log_path = os.path.join(tmp, "app.log")
    with open(config_path, "w") as handle:
        handle.write(config_xml(workingdir, audiodev, configured))

    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["HOME"] = tmp
    env["XDG_CONFIG_HOME"] = os.path.join(tmp, "config")
    env["XDG_DATA_HOME"] = os.path.join(tmp, "data")
    os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)
    os.makedirs(env["XDG_DATA_HOME"], exist_ok=True)

    log_handle = open(log_path, "wb")
    process = subprocess.Popen(
        [binary, "--config", config_path, "--control-socket", socket_path],
        stdout=log_handle, stderr=subprocess.STDOUT, env=env, cwd=tmp,
    )
    return Instance(binary, tmp, config_path, socket_path, log_path, process, log_handle)


def wait_for_socket(instance, seconds=SOCKET_TIMEOUT):
    """Wait until the socket file exists AND accepts a connection.

    The listener is created before the GUI and the engine (main.cpp), so this
    succeeds even on an instance that is about to hang in a modal dialog: the
    accept() backlog takes the connection with nobody reading it.
    """
    deadline = time.time() + seconds
    while time.time() < deadline:
        if not instance.alive():
            raise Blocked("the instance exited (code %s) before its socket was usable"
                          % instance.process.returncode)
        if os.path.exists(instance.socket_path):
            try:
                probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                probe.settimeout(1.0)
                probe.connect(instance.socket_path)
                probe.close()
                return
            except OSError:
                pass
        time.sleep(0.1)
    raise Blocked("the control socket %s was never connectable within %.0fs"
                  % (instance.socket_path, seconds))


class Client(object):
    """One request and one response per line, the wire contract of ControlServer."""

    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.buffer = b""

    def call(self, request_id, cmd, args=None, proto=1, timeout=OPEN_TIMEOUT, transcript=None):
        request = {"id": request_id, "cmd": cmd, "args": args or {}, "proto": proto}
        raw = json.dumps(request, separators=(",", ":"))
        if transcript is not None:
            transcript.add("->", raw)
        self.sock.sendall(raw.encode("utf-8") + b"\n")
        try:
            reply = self._read_line(timeout)
        except (Blocked, OSError) as error:
            if transcript is not None:
                transcript.add("<-", "NO REPLY inside %.0fs (%s)" % (timeout, error))
            raise
        text = reply.decode("utf-8", "replace")
        if transcript is not None:
            transcript.add("<-", text)
        return json.loads(text)

    def _read_line(self, timeout):
        deadline = time.time() + timeout
        self.sock.settimeout(timeout)
        while b"\n" not in self.buffer:
            if time.time() > deadline:
                raise Blocked("no response line inside %.0fs" % timeout)
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                raise Blocked("no response line inside %.0fs (socket timed out)" % timeout)
            if not chunk:
                raise Blocked("the server closed the connection")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


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


def diagnose_block(instance, client, transcript, request_id=99):
    """After a Blocked, say WHICH block signature was measured.

    Returns one sentence for the report. Raises AssertionError when the instance
    is demonstrably NOT blocked (a ping that answers `engine_ready:true`).
    """
    try:
        reply = client.call(request_id, "control.ping", timeout=PING_TIMEOUT, transcript=transcript)
    except Blocked:
        return "the socket stopped answering altogether (the UI thread is not dispatching events)"
    result = reply.get("result") or {}
    if not result.get("pong"):
        raise AssertionError("control.ping answered without pong: %r" % (reply,))
    if result.get("engine_ready"):
        raise AssertionError("the instance reports engine_ready while presumably blocked: %r"
                             % (reply,))
    return ("the nested modal loop is still dispatching: control.ping answers "
            "pong=true engine_ready=false, i.e. the startup path is parked inside the box")


def healthy_control(binary, seconds=90.0):
    """Prove the binary CAN serve a control socket on this machine, right now.

    A negative control must not be satisfiable by a broken environment: a binary
    started outside its data directory (or on a machine with no Qt platform
    plugin) never becomes ready either, which would make "never became ready"
    mean nothing.  This starts the SAME binary with a working directory that
    exists and the dummy device, and returns how long it took to become ready.
    """
    instance = start_instance(binary)
    transcript = Transcript()
    try:
        client = connect(instance)
        started = time.time()
        wait_ready(instance, client, transcript, seconds=seconds, ping_timeout=PING_TIMEOUT)
        return time.time() - started
    finally:
        instance.kill()


def parse_args(usage, minimum_argv=1):
    """argv[1..] with a `--expect-blocked` switch; returns (mode, rest)."""
    argv = [a for a in sys.argv[1:] if a != "--expect-blocked"]
    expect_blocked = "--expect-blocked" in sys.argv[1:]
    if len(argv) < minimum_argv:
        print(usage)
        sys.exit(2)
    return expect_blocked, argv


def report_pre_fix(what):
    ok("%s [pre-fix mode: the block was reproduced, as task #625 measured]" % what)
