#!/usr/bin/env python3
"""The two transports one control-surface transcript can be driven over.

ONE interface, two kernels: `AF_UNIX` on POSIX, a NAMED PIPE on Windows.  A test
imports `make_client()` and cannot tell which one it is talking to; the transport
is chosen by the SHAPE OF THE PATH (`\\\\.\\pipe\\...` -> named pipe, anything else
-> AF_UNIX), which is also what lets a Windows-only transcript be run by hand on
a POSIX box over the socket.

Split out of `control-named-pipe-smoke.py` for the same reason
`control_socket_flows.py` and `control_instance_diagnosis.py` were split out of
`control_socket_harness.py`: a helper with its own subject gets its own file, and
the per-file length ratchet stays honest (both halves are under its limit).

The named-pipe half exists because there is no other way to speak the control
protocol on Windows: Python's `open()` on a pipe path and `socket` do not give
the byte-exact framing this surface needs, so it uses `CreateFileW`/`WriteFile`/
`ReadFile` through `ctypes`, and `PeekNamedPipe` for availability - a blocking
`ReadFile` on a pipe with nothing to give would hang a test forever, and a hang
is a failure in every control-surface test in this directory (the rule
`control_socket_harness.py` states in its header).

Both clients expose the same four calls: `send()`, `read_line(timeout_s)`,
`connection_closed(timeout_s)` and `close()`.
"""

import os
import random
import socket
import sys
import time

# ---------------------------------------------------------------------------
# failures.  Timeout is a subclass of Failure so a caller can catch one name or
# the other; both are ALWAYS a failure, never a skip.
# ---------------------------------------------------------------------------


class Failure(Exception):
    """One check did not hold.  The message is the evidence."""


class Timeout(Failure):
    """A bounded wait expired."""


# ---------------------------------------------------------------------------
# the AF_UNIX half
# ---------------------------------------------------------------------------


class UnixSocketClient:
    """The POSIX transport: a blocking socket with a bound on every read."""

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


# ---------------------------------------------------------------------------
# the named-pipe half
# ---------------------------------------------------------------------------


class NamedPipeClient:
    """The Windows transport: CreateFileW + WriteFile/ReadFile through ctypes."""

    GENERIC_READ = 0x80000000
    GENERIC_WRITE = 0x40000000
    OPEN_EXISTING = 3
    ERROR_FILE_NOT_FOUND = 2
    ERROR_PIPE_BUSY = 231

    def __init__(self, path, timeout_s):
        if os.name != "nt":
            raise Failure("a named pipe needs Windows; this host is %s" % sys.platform)
        # Declared before the platform import so the attributes exist even where a
        # type checker cannot see WinDLL (this class only ever runs on Windows).
        self.ctypes = None
        self.wintypes = None
        self.kernel32 = None
        self.invalid = None
        self.buffer = b""
        self.path = path
        self.handle = None
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


# ---------------------------------------------------------------------------
# the one call a test makes
# ---------------------------------------------------------------------------


def is_pipe_path(path):
    """True when \p path names something in the Windows named-pipe namespace."""
    return path.lower().startswith("\\\\.\\pipe\\")


def make_client(path, timeout_s):
    """The transport the SHAPE OF THE PATH selects, ready to talk to."""
    if is_pipe_path(path):
        return NamedPipeClient(path, timeout_s)
    return UnixSocketClient(path, timeout_s)


def default_socket_path(tmpdir):
    """A control path nothing else on this machine is using."""
    if os.name == "nt":
        return "\\\\.\\pipe\\zene-code9-smoke-%d-%d" % (os.getpid(), random.randrange(100000))
    return os.path.join(tmpdir, "zene.sock")
