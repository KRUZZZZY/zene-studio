#!/usr/bin/env python3
"""The CODE-9 smoke test: `--control-socket` on a Windows NAMED PIPE.

WHAT THIS PROVES, AND WHAT IT CANNOT
------------------------------------
The Windows half of the control transport (src/core/ControlServerWin32.cpp) is a
named pipe behind the SAME contract as the POSIX AF_UNIX socket: the same
line-delimited JSON-RPC framing, the same command ids, the same typed refusal
shapes, the same `--control-socket <path>` start line.  This script drives a REAL
built instance and checks exactly that over the wire:

  1. the instance starts with `--control-socket \\\\.\\pipe\\<name>` and prints the
     same `control socket listening on <path>` line the POSIX transports print;
  2. a client can connect to that pipe (CreateFileW) and exchange whole lines;
  3. `control.ping` answers with the protocol's own result (pong, proto 1); two
     requests written in ONE write are answered with TWO lines, in order (the
     framing rule: one request line in, one response line out, never a spliced
     line);
  4. `control.commands_list` carries the command ids the surface has on POSIX -
     checked as a set membership of long-standing ids, plus the shape (strings);
  5. a malformed request line and an unknown command id get the typed refusal
     shape: {"id":..,"ok":false,"error":{"kind":..,"message":..}} with a kind from
     the protocol's closed set;
  6. a request line past the 1 MiB cap is refused with the SAME invalid_args
     sentence the POSIX path sends, and only that connection is dropped: a fresh
     connection still works;
  7. a path that is not a pipe name is refused at START-UP with the typed line on
     stderr and a non-zero exit - the launcher-visible refusal shape, which on
     POSIX comes from listen() and here from listenWin32() through the same
     reporter;
  8. `control.quit` answers, the instance exits inside a bound, and the pipe name
     is gone afterwards.

A hang is a failure: every wait below is bounded, and the bound expiring is a
failure (exit 1), never a skip.

Registered as the ctest `ControlNamedPipeSmoke` under `if(WIN32 ...)` in
tests/CMakeLists.txt, which is the only place it can run for real: the lane that
wrote the transport had no Windows toolchain, so this script is the CI-only half
of the evidence (docs/CONTROL-NAMED-PIPE.md says so in the same words).

ON POSIX IT STILL RUNS - over the socket, by hand, as a check of ITS OWN logic:
the transport is chosen by the SHAPE OF THE PATH (`\\\\.\\pipe\\...` -> named pipe,
anything else -> AF_UNIX), so a developer without Windows can run

    QT_QPA_PLATFORM=offscreen python3 tests/control-named-pipe-smoke.py <zene>

against a local build and see the SAME assertions pass over the POSIX transport.
That does not make the Windows half verified; it makes this file's framing,
timeouts and expectations executable where the pipe is not.  Without a binary it
exits 77 (ctest: Skipped), the same convention the other control transcripts use.

Usage:
    QT_QPA_PLATFORM=offscreen python3 tests/control-named-pipe-smoke.py <zene-binary>
Exit code 0 only when every check passed; 77 when there is nothing to drive.
"""

import json
import os
import random
import shutil
import socket
import subprocess
import sys
import tempfile
import time

# ---------------------------------------------------------------------------
# bounds.  A healthy instance answers a ping in milliseconds; the FIRST reply can
# wait for Engine::init on a loaded runner, so it gets the declared readiness
# budget the POSIX harness uses, and every later wait is its own bound.
# ---------------------------------------------------------------------------
START_BOUND = 120.0   # the first reply (the instance is starting its engine)
REPLY_BOUND = 30.0    # one request/response exchange
CONNECT_BOUND = 60.0  # the listener appearing
EXIT_BOUND = 60.0     # control.quit -> process gone

MAX_REQUEST_LINE_BYTES = 1024 * 1024

# AudioDummy::name() exactly: the app compares the configured value character by
# character (the same string the POSIX harness uses).
DUMMY_DEVICE = "Dummy (no sound output)"

# The ids a client may rely on always being there.  Deliberately a small,
# long-standing set: this asserts the Windows transport serves the SAME SURFACE,
# not that the surface is frozen.
STABLE_IDS = (
    "control.ping",
    "control.version",
    "control.commands_list",
    "control.quit",
    "project.get_state",
    "transport.get_state",
    "mixer.get_state",
)

KINDS = ("not_found", "requires", "invalid_args", "busy", "refused", "irreversible")


class Failure(Exception):
    """One check did not hold.  The message is the evidence."""


class Timeout(Failure):
    """A bounded wait expired.  Always a failure, never a skip."""


def ok(message):
    print("PASS: %s" % message)


# ---------------------------------------------------------------------------
# the headless recipe (AGENT-TOOLING.md section 4, identical to the POSIX
# harness's: offscreen QPA, a config whose audio device matches AudioDummy::name(),
# HOME/XDG_* in a temp directory, cwd in the temp directory)
# ---------------------------------------------------------------------------


def config_xml(workingdir, audiodev=DUMMY_DEVICE):
    return ('<?xml version="1.0"?>\n'
            '<!DOCTYPE lmms-config-file>\n'
            '<lmmsconfig version="0.2.0-alpha" configversion="3">\n'
            '  <app configured="1"/>\n'
            '  <audioengine audiodev="%s"/>\n'
            '  <paths workingdir="%s"/>\n'
            '</lmmsconfig>\n' % (audiodev, workingdir))


class Instance:
    """One real zene process listening with --control-socket, in its own temp world."""

    def __init__(self, binary, socket_path, offscreen=True):
        self.binary = os.path.abspath(binary)
        if not os.path.exists(self.binary):
            raise Failure("no binary at %s" % self.binary)
        self.tmp = tempfile.mkdtemp(prefix="zene-pipe-smoke-")
        self.socket_path = socket_path
        self.workspace = os.path.join(self.tmp, "workspace")
        os.makedirs(self.workspace)
        self.config_path = os.path.join(self.tmp, "zene.xml")
        self.stdout_path = os.path.join(self.tmp, "stdout.log")
        self.stderr_path = os.path.join(self.tmp, "stderr.log")
        self.offscreen = offscreen
        self.process = None
        self._handles = []
        with open(self.config_path, "w") as handle:
            handle.write(config_xml(self.workspace))

    def env(self):
        env = dict(os.environ)
        if self.offscreen:
            env["QT_QPA_PLATFORM"] = "offscreen"
        # The app's own settings must not be touched: point every per-user
        # location the platform gives it at the temp world.
        env["HOME"] = self.tmp
        env["XDG_CONFIG_HOME"] = os.path.join(self.tmp, "config")
        env["XDG_DATA_HOME"] = os.path.join(self.tmp, "data")
        env["USERPROFILE"] = self.tmp
        env["APPDATA"] = os.path.join(self.tmp, "config")
        env["LOCALAPPDATA"] = os.path.join(self.tmp, "config")
        for key in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "APPDATA", "LOCALAPPDATA"):
            os.makedirs(env[key], exist_ok=True)
        return env

    def spawn(self, socket_path=None):
        self._handles = [open(self.stdout_path, "wb"), open(self.stderr_path, "wb")]
        self.process = subprocess.Popen(
            [self.binary, "--config", self.config_path,
             "--control-socket", socket_path or self.socket_path],
            stdout=self._handles[0], stderr=self._handles[1], env=self.env(), cwd=self.tmp)
        return self.process

    def log(self):
        text = ""
        for path in (self.stdout_path, self.stderr_path):
            try:
                with open(path, errors="replace") as handle:
                    text += handle.read()
            except OSError:
                pass
        return text

    def alive(self):
        process = self.process
        return process is not None and process.poll() is None

    def wait_for_exit(self, seconds):
        process = self.process
        if process is None:
            return False, None
        deadline = time.time() + seconds
        while time.time() < deadline:
            if process.poll() is not None:
                return True, process.returncode
            time.sleep(0.05)
        return False, None

    def kill(self):
        if self.alive():
            self.process.kill()
            self.process.wait()

    def close(self):
        self.kill()
        for handle in self._handles:
            try:
                handle.close()
            except (OSError, ValueError):
                pass
        shutil.rmtree(self.tmp, ignore_errors=True)


# ---------------------------------------------------------------------------
# transports.  One interface, two kernels: AF_UNIX on POSIX, a named pipe on
# Windows.  The tests below cannot tell which one they are driving.
# ---------------------------------------------------------------------------


class UnixSocketClient:
    def __init__(self, path, timeout_s):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout_s)
        self.sock.connect(path)
        self.buffer = b""

    def send(self, payload):
        self.sock.sendall(payload)

    def read_line(self, timeout_s):
        self.sock.settimeout(timeout_s)
        while b"\n" not in self.buffer:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                raise Timeout("no reply line within %.1fs" % timeout_s)
            if not chunk:
                raise Failure("the connection closed with %d buffered bytes and no newline"
                              % len(self.buffer))
            self.buffer += chunk
        line, _, self.buffer = self.buffer.partition(b"\n")
        return line

    def connection_closed(self, timeout_s):
        self.sock.settimeout(timeout_s)
        try:
            return self.sock.recv(65536) == b""
        except socket.timeout:
            return False

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class NamedPipeClient:
    """A byte-mode named pipe client, with CreateFileW/PeekNamedPipe/ReadFile.

    PeekNamedPipe is what makes the read bounded: a blocking ReadFile on a pipe
    with nothing to give would hang the test forever, which this file's contract
    forbids (a hang is a failure, and a failure has to be reportable).
    """

    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3
    ERROR_FILE_NOT_FOUND = 2
    ERROR_PIPE_BUSY = 231

    def __init__(self, path, timeout_s):
        if os.name != "nt":
            raise Failure("a named pipe needs Windows; this host is %s" % sys.platform)
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.wintypes = wintypes
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k = self.kernel32
        k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.c_void_p]
        k.CreateFileW.restype = ctypes.c_void_p
        k.WriteFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD,
                                ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        k.WriteFile.restype = wintypes.BOOL
        k.ReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD,
                               ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        k.ReadFile.restype = wintypes.BOOL
        k.PeekNamedPipe.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD,
                                    ctypes.POINTER(wintypes.DWORD),
                                    ctypes.POINTER(wintypes.DWORD),
                                    ctypes.POINTER(wintypes.DWORD)]
        k.PeekNamedPipe.restype = wintypes.BOOL
        k.CloseHandle.argtypes = [ctypes.c_void_p]
        k.CloseHandle.restype = wintypes.BOOL
        k.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
        k.WaitNamedPipeW.restype = wintypes.BOOL
        self.invalid = ctypes.c_void_p(-1).value
        self.path = path
        self.buffer = b""
        self.handle = self._connect(timeout_s)

    def _connect(self, timeout_s):
        deadline = time.time() + timeout_s
        while True:
            handle = self.kernel32.CreateFileW(
                self.path, self.GENERIC_READ | self.GENERIC_WRITE, 0, None,
                self.OPEN_EXISTING, 0, None)
            if handle and handle != self.invalid:
                return handle
            error = self.ctypes.get_last_error()
            if time.time() >= deadline:
                raise Timeout("the pipe %s was not connectable within %.1fs (CreateFileW "
                              "error %d)" % (self.path, timeout_s, error))
            if error == self.ERROR_PIPE_BUSY:
                # Every instance is serving a client: the documented wait.
                self.kernel32.WaitNamedPipeW(self.path, 1000)
            elif error != self.ERROR_FILE_NOT_FOUND:
                raise Failure("CreateFileW on %s failed with error %d" % (self.path, error))
            time.sleep(0.05)

    def send(self, payload):
        sent = 0
        while sent < len(payload):
            written = self.wintypes.DWORD(0)
            chunk = payload[sent:]
            ok_write = self.kernel32.WriteFile(self.handle, chunk, len(chunk),
                                               self.ctypes.byref(written), None)
            if not ok_write:
                raise Failure("WriteFile on %s failed with error %d"
                              % (self.path, self.ctypes.get_last_error()))
            sent += written.value

    def read_line(self, timeout_s):
        deadline = time.time() + timeout_s
        while b"\n" not in self.buffer:
            if time.time() >= deadline:
                raise Timeout("no reply line within %.1fs" % timeout_s)
            available = self.wintypes.DWORD(0)
            if not self.kernel32.PeekNamedPipe(self.handle, None, 0, None,
                                               self.ctypes.byref(available), None):
                raise Failure("PeekNamedPipe on %s failed with error %d"
                              % (self.path, self.ctypes.get_last_error()))
            if available.value == 0:
                time.sleep(0.01)
                continue
            chunk = self.ctypes.create_string_buffer(min(available.value, 65536))
            got = self.wintypes.DWORD(0)
            if not self.kernel32.ReadFile(self.handle, chunk, len(chunk),
                                          self.ctypes.byref(got), None):
                raise Failure("ReadFile on %s failed with error %d"
                              % (self.path, self.ctypes.get_last_error()))
            self.buffer += chunk.raw[:got.value]
        line, _, self.buffer = self.buffer.partition(b"\n")
        return line

    def connection_closed(self, timeout_s):
        """True when the server's end is gone: the next peek/read sees EOF."""
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if self.buffer:
                return False
            available = self.wintypes.DWORD(0)
            if not self.kernel32.PeekNamedPipe(self.handle, None, 0, None,
                                               self.ctypes.byref(available), None):
                return True
            if available.value:
                return False
            time.sleep(0.01)
        return False

    def close(self):
        if self.handle:
            self.kernel32.CloseHandle(self.handle)
            self.handle = None


def is_pipe_path(path):
    return path.lower().startswith("\\\\.\\pipe\\")


def make_client(path, timeout_s):
    return NamedPipeClient(path, timeout_s) if is_pipe_path(path) else UnixSocketClient(path, timeout_s)


def default_socket_path(tmpdir):
    if os.name == "nt":
        return "\\\\.\\pipe\\zene-code9-smoke-%d-%d" % (os.getpid(), random.randrange(100000))
    return os.path.join(tmpdir, "zene.sock")


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


class Client:
    """A transcript-recording JSON-RPC client over whichever transport is in use."""

    def __init__(self, transport):
        self.transport = transport
        self.next_id = 1

    def raw(self, payload, timeout_s=REPLY_BOUND):
        self.transport.send(payload)
        line = self.transport.read_line(timeout_s)
        print("-> %s" % payload.decode("utf-8", "replace")[:200])
        print("<- %s" % line.decode("utf-8", "replace")[:400])
        return line

    def call(self, cmd, args=None, request_id=None, timeout_s=REPLY_BOUND, proto=1):
        request_id = self.next_id if request_id is None else request_id
        self.next_id = max(self.next_id, request_id) + 1
        request = {"id": request_id, "cmd": cmd, "args": args or {}, "proto": proto}
        line = self.raw((json.dumps(request, separators=(",", ":")) + "\n").encode("utf-8"),
                        timeout_s)
        try:
            reply = json.loads(line)
        except ValueError:
            raise Failure("the reply to %s is not JSON: %r" % (cmd, line[:200]))
        if reply.get("id") != request_id:
            raise Failure("the reply to %s carries id %r, expected %r"
                          % (cmd, reply.get("id"), request_id))
        return reply

    def ok_call(self, cmd, args=None, timeout_s=REPLY_BOUND):
        reply = self.call(cmd, args, timeout_s=timeout_s)
        if reply.get("ok") is not True:
            raise Failure("%s was refused: %s" % (cmd, json.dumps(reply)[:300]))
        return reply.get("result") or {}

    def typed_refusal(self, cmd, what, timeout_s=REPLY_BOUND):
        reply = self.call(cmd, timeout_s=timeout_s)
        if reply.get("ok") is not False:
            raise Failure("%s answered ok - expected the typed refusal (%s)" % (what, what))
        error = reply.get("error")
        if not isinstance(error, dict) or "kind" not in error or "message" not in error:
            raise Failure("%s answered without the {kind,message} error object: %s"
                          % (what, json.dumps(reply)[:300]))
        if error["kind"] not in KINDS:
            raise Failure("%s answered kind %r, outside the protocol's closed set %s"
                          % (what, error["kind"], list(KINDS)))
        return reply


def run(binary, socket_path, problems):
    instance = Instance(binary, socket_path)
    client = None
    try:
        instance.spawn()
        # The listener can appear before the GUI: wait for the NAME, then connect.
        deadline = time.time() + CONNECT_BOUND
        last = None
        while time.time() < deadline:
            if not instance.alive():
                raise Failure("the instance exited (code %s) before its control socket was "
                              "usable; log:\n%s" % (instance.process.returncode,
                                                    instance.log()[-4000:]))
            try:
                client = make_client(socket_path, 5.0)
                break
            except (Failure, Timeout, OSError) as exc:
                last = exc
                time.sleep(0.1)
        if client is None:
            # A runner without the offscreen plugin is a platform fact, not a defect
            # in the transport: retry once with the platform's own default QPA.
            if "platform plugin" in instance.log():
                print("note: the offscreen QPA plugin is unavailable here; retrying with the "
                      "platform default")
                instance.kill()
                instance.offscreen = False
                instance.spawn()
                deadline = time.time() + CONNECT_BOUND
                while time.time() < deadline and client is None:
                    try:
                        client = make_client(socket_path, 5.0)
                    except (Failure, Timeout, OSError) as exc:
                        last = exc
                        time.sleep(0.1)
        if client is None:
            raise Timeout("the control socket %s was never connectable within %.1fs (%s); "
                          "log:\n%s" % (socket_path, CONNECT_BOUND, last,
                                        instance.log()[-4000:]))
        ok("the instance listens on %s and a client can connect to it" % socket_path)

        if "control socket listening on %s" % socket_path not in instance.log():
            raise Failure("the instance's start line is not the one every transport prints: "
                          "'control socket listening on %s' is missing from its output"
                          % socket_path)
        ok("the start line is the shared one ('control socket listening on <path>')")

        transcript = Client(client)

        # 1. liveness, before anything that needs the engine.
        result = transcript.ok_call("control.ping", timeout_s=START_BOUND)
        if result.get("pong") is not True:
            raise Failure("control.ping answered without pong: %s" % json.dumps(result)[:300])
        if result.get("proto") != 1:
            raise Failure("control.ping reports proto %r, this surface speaks 1"
                          % result.get("proto"))
        ok("control.ping answers over the pipe (pong, proto 1)")

        # 2. framing: TWO requests in ONE write, TWO reply lines, in order.
        first = json.dumps({"id": 101, "cmd": "control.version", "args": {}, "proto": 1},
                           separators=(",", ":"))
        second = json.dumps({"id": 102, "cmd": "control.ping", "args": {}, "proto": 1},
                            separators=(",", ":"))
        client.send((first + "\n" + second + "\n").encode("utf-8"))
        line_a = client.read_line(REPLY_BOUND)
        line_b = client.read_line(REPLY_BOUND)
        reply_a, reply_b = json.loads(line_a), json.loads(line_b)
        if reply_a.get("id") != 101 or reply_b.get("id") != 102:
            raise Failure("two requests in one write were not answered one line each, in "
                          "order: got ids %r then %r" % (reply_a.get("id"), reply_b.get("id")))
        ok("two requests in one write -> two whole reply lines, in order (no spliced line)")

        # 3. the same command ids, read off the wire.
        surface = transcript.ok_call("control.commands_list")
        ids = surface.get("commands") or surface.get("ids") or []
        if not isinstance(ids, list) or not ids:
            raise Failure("control.commands_list answered without an id list: %s"
                          % json.dumps(surface)[:300])
        names = set()
        for entry in ids:
            if isinstance(entry, str):
                names.add(entry)
            elif isinstance(entry, dict) and isinstance(entry.get("id"), str):
                names.add(entry["id"])
            else:
                raise Failure("an entry of control.commands_list is neither an id string nor "
                              "an object with one: %r" % (entry,))
        declared = surface.get("count")
        if declared is not None and declared != len(ids):
            raise Failure("control.commands_list declares count=%r for %d entries: the "
                          "transport must not lose or duplicate an entry"
                          % (declared, len(ids)))
        missing = [name for name in STABLE_IDS if name not in names]
        if missing:
            raise Failure("the pipe serves a surface missing %s (of %d ids): the Windows "
                          "transport must serve the SAME command ids" % (missing, len(names)))
        ok("control.commands_list carries %d ids over the pipe, including all %d checked"
           % (len(names), len(STABLE_IDS)))

        # 4. a malformed request line, and an unknown command id: typed refusals.
        #    A malformed line cannot name a request, so the surface answers id -1
        #    (the id it gives a reply that belongs to no request).
        line = transcript.raw(b'{"id":201,"cmd":\n')
        reply = json.loads(line)
        if reply.get("ok") is not False or reply.get("id") != -1:
            raise Failure("a malformed request line was not refused in the protocol's shape: "
                          "%s" % line[:300])
        error = reply.get("error") or {}
        if error.get("kind") != "invalid_args":
            raise Failure("a malformed request line was refused with kind %r, expected "
                          "invalid_args" % error.get("kind"))
        ok("a malformed request line is refused with the typed error shape (id -1, "
           "invalid_args)")

        reply = transcript.call("no.such_command", request_id=202)
        if reply.get("ok") is not False or reply.get("id") != 202:
            raise Failure("an unknown command id was not refused typed: %s"
                          % json.dumps(reply)[:300])
        ok("an unknown command id is refused typed (%s)"
           % (reply.get("error") or {}).get("kind"))

        # 5. the 1 MiB request-line cap, then the SAME connection's fate and a fresh one.
        over = b"x" * (MAX_REQUEST_LINE_BYTES + 4096)
        chunk = 64 * 1024
        for offset in range(0, len(over), chunk):
            client.send(over[offset:offset + chunk])
        line = client.read_line(REPLY_BOUND)
        reply = json.loads(line)
        if reply.get("ok") is not False or reply.get("id") != -1:
            raise Failure("an over-cap request line was not refused typed: %s" % line[:300])
        if (reply.get("error") or {}).get("kind") != "invalid_args":
            raise Failure("an over-cap request line answered kind %r, expected invalid_args"
                          % (reply.get("error") or {}).get("kind"))
        ok("a request line past the %d-byte cap is refused with the same invalid_args line "
           "the POSIX path sends" % MAX_REQUEST_LINE_BYTES)
        client.close()
        client = None

        fresh = make_client(socket_path, CONNECT_BOUND)
        survivor = Client(fresh)
        result = survivor.ok_call("control.ping", timeout_s=START_BOUND)
        if result.get("pong") is not True:
            raise Failure("a connection after an over-cap refusal could not ping")
        ok("the listener survives an over-cap connection: a fresh one pings")

        # 6. the refusal a LAUNCHER sees, for a path that is not a pipe name.
        bad = Instance(binary, "not-a-pipe-name")
        try:
            bad.spawn()
            exited, code = bad.wait_for_exit(EXIT_BOUND)
            if not exited:
                raise Failure("a non-pipe --control-socket path did not make the instance "
                              "exit; log:\n%s" % bad.log()[-2000:])
            if code == 0:
                raise Failure("a non-pipe --control-socket path exited 0")
            stderr = bad.log()
            if '"kind":"invalid_args"' not in stderr.replace(" ", ""):
                raise Failure("the start-up refusal is not the protocol's typed line "
                              "(\\\"kind\\\":\\\"invalid_args\\\"); stderr:\n%s"
                              % stderr[-2000:])
            ok("a path that is not \\\\.\\pipe\\<name> is refused at start-up with the typed "
               "line (exit %s)" % code)
        finally:
            bad.close()

        # 7. normal shutdown, and the name is gone.
        reply = survivor.call("control.quit", request_id=900)
        if reply.get("ok") is not True:
            raise Failure("control.quit was refused: %s" % json.dumps(reply)[:300])
        ok("control.quit answers over the pipe")
        fresh.close()
        client = None
        exited, code = instance.wait_for_exit(EXIT_BOUND)
        if not exited:
            raise Failure("the instance did not exit within %.1fs of control.quit; log:\n%s"
                          % (EXIT_BOUND, instance.log()[-4000:]))
        ok("the instance exits after control.quit (code %s)" % code)
        if "FATAL: the normal shutdown did not finish" in instance.log():
            raise Failure("the shutdown guard fired: the normal shutdown did not finish")
        probe = None
        try:
            probe = make_client(socket_path, 3.0)
        except (Failure, Timeout, OSError):
            ok("the control socket is gone after the instance exits")
        else:
            probe.close()
            raise Failure("the control socket %s is still connectable after the instance "
                          "exited: it was not cleaned up" % socket_path)
    except Failure as exc:
        problems.append(str(exc))
        print("FAIL: %s" % exc)
        print("---- the instance's own output ----")
        print(instance.log()[-6000:])
    finally:
        if client is not None:
            client.close()
        instance.close()
    return problems


def main(argv):
    if os.name != "nt":
        if len(argv) < 2:
            print("usage: %s <zene-binary> [socket-path]" % argv[0])
            print("(on this host the path selects the AF_UNIX transport: the same assertions)")
            return 77
    if len(argv) < 2:
        print("SKIP: no binary given (ctest passes $<TARGET_FILE:zene>)")
        return 77
    binary = argv[1]
    if not os.path.exists(binary):
        print("SKIP: nothing to drive: %s does not exist" % binary)
        return 77
    tmp = tempfile.mkdtemp(prefix="zene-pipe-smoke-root-")
    try:
        socket_path = argv[2] if len(argv) > 2 else default_socket_path(tmp)
        print("control socket under test: %s" % socket_path)
        problems = run(binary, socket_path, [])
        print("\n================ SUMMARY ================")
        print("ControlNamedPipeSmoke: %s" % ("FAIL" if problems else "PASS"))
        for item in problems:
            print("  - %s" % item)
        return 1 if problems else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
