#!/usr/bin/env python3
"""The socket PATH is destructive to bind, so binding must never eat what is there.

Reproduced on the release configuration (commit ba24a9578, 2026-09-12):

    $ printf 'IMPORTANT PROJECT DATA\\n' > /tmp/socktest/song.mmp      # 92 bytes
    $ QT_QPA_PLATFORM=offscreen ./build-rel/zene --control-socket /tmp/socktest/song.mmp
    $ ls -l /tmp/socktest/song.mmp
      -rw-rw-r-- 0 bytes  kind=socket      <-- the file's contents are GONE

`ControlServer::openBoundSocket()` called `unlink(address.sun_path)` before EVERY
`bind()`, so whatever the caller had at that path was destroyed and replaced by
the socket, silently, and the process reported nothing. `--control-socket
~/song.mmp` is the mistake the report found; reusing a path from a previous run is
the other. The data loss is the whole reason this surface exists to be readable.

The rule this test pins down (docs/CONTROL-SOCKET-PATH-SAFETY.md):
  * nothing at the path        -> bind normally;
  * a socket file at the path  -> a stale socket from a crashed run: unlinking it
                                  is the only way to bind, so do it - and SAY SO;
  * anything else (regular
    file, symlink, FIFO)       -> refuse, typed `refused`, naming the path: do not
                                  unlink, do not bind, do not start the server;
  * a directory                -> refuse, typed `invalid_args`: a directory can
                                  never be a socket path, and nothing is at risk of
                                  being deleted (see the doc for why the two kinds
                                  differ here).

The refusal is read where an agent can read it: an instance that refused to start
has no socket to answer on, so the process writes the surface's OWN typed error to
stderr, byte-for-byte the wire shape
(`{"id":-1,"ok":false,"error":{"kind":...,"message":...}}`), and exits non-zero.

Cases: (1) a regular file survives byte-identical, the refusal names it and
nothing listens; (2) a stale socket is still cleaned up and the instance listens;
(3) a free path still binds, with no unlink line and no refusal; (4) a directory
is `invalid_args` and survives with its contents; (5) a symlink is `refused` and
BOTH the link and its target survive; (6) a request line over
`ControlServer::MaxRequestLineBytes` is refused, typed, and the connection is
retired (drained, then closed) instead of buffering without bound.

Usage:
  QT_QPA_PLATFORM=offscreen python3 control-socket-path-safety.py <lmms>
Exit code 0 only when every case passed.
"""

import hashlib
import json
import os
import socket
import stat
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_harness import (  # noqa: E402
    PING_TIMEOUT, Blocked, Instance, Problems, Timeout, connect, finish, ok,
    start_instance,
)

# The cap in include/ControlServer.h (ControlServer::MaxRequestLineBytes).
REQUEST_LINE_CAP = 1024 * 1024
# Big enough that a destroyed file is unmistakable, small enough to print.
PAYLOAD = b"IMPORTANT PROJECT DATA\n" * 4
# The one line the test parses; the process writes the typed object after it.
REFUSAL_MARKER = "control socket: "


class PathInstance(Instance):
    """The harness Instance, with the socket path chosen by the case.

    The harness hard-codes <tmp>/zene.sock and every case here is ABOUT the path
    handed to --control-socket, so the name has to be settable.
    """

    def __init__(self, binary, socket_name="zene.sock", **kwargs):
        super().__init__(binary, **kwargs)
        self.socket_path = os.path.join(self.tmp, socket_name)


def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def typed_errors(text):
    """Every typed error the process wrote, decoded from the wire-shaped line."""
    found = []
    for line in text.splitlines():
        start = line.find('{"id"')
        if start < 0:
            continue
        try:
            found.append(json.loads(line[start:]))
        except ValueError:
            continue
    return found


def typed_error(text):
    """The first typed error in the log, or None."""
    found = typed_errors(text)
    return (found[0].get("error") or {}) if found else None


def refused_error(text, problems, expected_kind, path):
    """Assert the log carries a typed error of `expected_kind` naming `path`."""
    error = typed_error(text)
    if error is None:
        problems.add("no typed refusal on stderr (looked for a line carrying '{\"id\"'); the "
                     "process said: %r" % text[-600:])
        return None
    if error.get("kind") != expected_kind:
        problems.add("the refusal kind is %r, expected %r: %r"
                     % (error.get("kind"), expected_kind, error))
    if path not in (error.get("message") or ""):
        problems.add("the refusal does not name the offending path %s: %r" % (path, error))
    return error


def exit_code(instance, seconds, problems):
    """Bounded wait for the refusal's exit; None when it did not exit at all."""
    exited, code, elapsed = instance.wait_for_exit(seconds)
    if not exited:
        problems.add("the process was still running %.1fs later; a path refusal is immediate "
                     "(the deadlock here is what the test is for)" % elapsed)
        return None
    return code


def assert_no_listener(path, problems):
    """Nothing may accept a connection: the server must not have been started."""
    probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    probe.settimeout(2.0)
    try:
        probe.connect(path)
    except OSError as error:
        print("no server listening at %s (expected): %s" % (path, error))
    else:
        problems.add("something ACCEPTED a connection at %s: the server started on a path it "
                     "should have refused" % path)
    finally:
        probe.close()


def outcome(name, problems):
    if not problems.report(name):
        return name, False, problems
    ok(name)
    return name, True, problems


def case_regular_file(binary):
    """THE defect: a project file at --control-socket."""
    name = "a regular file at --control-socket is refused and left byte-identical"
    problems = Problems()
    instance = PathInstance(binary)
    try:
        path = instance.socket_path
        with open(path, "wb") as handle:
            handle.write(PAYLOAD)
        before, size_before = digest(path), os.path.getsize(path)
        instance.spawn()
        code = exit_code(instance, 60.0, problems)
        if code is not None and code == 0:
            problems.add("the instance exited 0 with a project file at the socket path; it must "
                         "fail (measured exit %s)" % code)
        info = os.lstat(path)
        if stat.S_ISSOCK(info.st_mode):
            problems.add("the file was replaced by a socket: THE DEFECT (0-byte socket)")
        if os.path.getsize(path) != size_before:
            problems.add("the file is now %d bytes, was %d" % (os.path.getsize(path), size_before))
        after = digest(path)
        if after != before:
            problems.add("the file is NOT byte-identical: sha256 %s -> %s" % (before, after))
        refused_error(instance.stderr_text(), problems, "refused", path)
        assert_no_listener(path, problems)
    finally:
        instance.close()
    return outcome(name, problems)


def case_stale_socket(binary):
    """A crashed run's socket file: unlinking it is correct - and must be audible."""
    name = "a stale socket is still cleaned up, is reported, and the instance listens"
    problems = Problems()
    instance = PathInstance(binary)
    try:
        path = instance.socket_path
        stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        stale.bind(path)   # creates the socket FILE...
        stale.close()      # ...and leaves it behind, exactly like a SIGKILLed run
        if not stat.S_ISSOCK(os.lstat(path).st_mode):
            problems.add("the fixture did not leave a socket file at %s" % path)
        instance.spawn()
        try:
            client = connect(instance)
        except (Blocked, Timeout) as error:
            problems.add("the instance never listened on the stale socket path: %s" % error)
            return outcome(name, problems)
        reply = client.call(1, "control.ping", timeout=PING_TIMEOUT)
        client.close()
        if reply.get("ok") is not True or not (reply.get("result") or {}).get("pong"):
            problems.add("control.ping on the recovered socket answered %r" % reply)
        log = instance.read_log()
        if "unlinking the stale socket file" not in log or path not in log:
            problems.add("the app log does not say the stale socket %s was unlinked and does not "
                         "name it: %r" % (path, log[-600:]))
        if typed_errors(log):
            problems.add("a stale socket was refused instead of replaced: %r" % typed_errors(log))
    finally:
        instance.close()
    return outcome(name, problems)


def case_free_path(binary):
    """The ordinary case must not regress: a free path binds, silently."""
    name = "a free path still binds normally (no refusal, no unlink line)"
    problems = Problems()
    instance = start_instance(binary)
    try:
        path = instance.socket_path
        if os.path.lexists(path):
            problems.add("the harness left something at the fresh socket path %s" % path)
        try:
            client = connect(instance)
        except (Blocked, Timeout) as error:
            problems.add("the instance never listened on a free path: %s" % error)
            return outcome(name, problems)
        reply = client.call(1, "control.ping", timeout=PING_TIMEOUT)
        client.close()
        if reply.get("ok") is not True or not (reply.get("result") or {}).get("pong"):
            problems.add("control.ping on a fresh path answered %r" % reply)
        log = instance.read_log()
        if "unlinking the stale socket file" in log:
            problems.add("the instance claims it unlinked a stale socket at a path that did not "
                         "exist: %r" % log[-600:])
        if typed_errors(log):
            problems.add("a free path was refused: %r" % typed_errors(log))
    finally:
        instance.close()
    return outcome(name, problems)


def case_directory(binary):
    """A directory can never be a socket path: invalid_args, and nothing is deleted."""
    name = "a directory at --control-socket is invalid_args, and survives with its contents"
    problems = Problems()
    instance = PathInstance(binary)
    try:
        path = instance.socket_path
        os.makedirs(path)
        keeper = os.path.join(path, "song.mmp")
        with open(keeper, "wb") as handle:
            handle.write(PAYLOAD)
        before = digest(keeper)
        instance.spawn()
        code = exit_code(instance, 60.0, problems)
        if code is not None and code == 0:
            problems.add("the instance exited 0 with a directory at the socket path")
        if not os.path.isdir(path):
            problems.add("the directory at the socket path was removed or replaced")
        if not os.path.isfile(keeper) or digest(keeper) != before:
            problems.add("a file inside the directory was damaged")
        refused_error(instance.stderr_text(), problems, "invalid_args", path)
    finally:
        instance.close()
    return outcome(name, problems)


def case_symlink(binary):
    """lstat, not stat: a symlink is refused, so unlink() can never delete the link."""
    name = "a symlink at --control-socket is refused, and link and target both survive"
    problems = Problems()
    instance = PathInstance(binary)
    try:
        path = instance.socket_path
        target = os.path.join(instance.tmp, "real-song.mmp")
        with open(target, "wb") as handle:
            handle.write(PAYLOAD)
        os.symlink(target, path)
        before = digest(target)
        instance.spawn()
        code = exit_code(instance, 60.0, problems)
        if code is not None and code == 0:
            problems.add("the instance exited 0 with a symlink at the socket path")
        if not os.path.islink(path):
            problems.add("the symlink at the socket path was removed or replaced")
        if not os.path.isfile(target) or digest(target) != before:
            problems.add("the symlink's target was damaged")
        refused_error(instance.stderr_text(), problems, "refused", path)
    finally:
        instance.close()
    return outcome(name, problems)


def case_request_line_cap(binary):
    """One line, no newline, twice the cap: refused typed, then the connection is retired."""
    name = "an over-cap request line is refused typed and the connection is retired"
    problems = Problems()
    instance = start_instance(binary)
    try:
        client = connect(instance)
        client.sock.sendall(b"x" * (REQUEST_LINE_CAP * 2))
        # The harness's own reader, not a request: call() would have to SEND on a
        # connection the server is retiring, which is the thing being tested.
        line = client._read_line(PING_TIMEOUT)  # noqa: SLF001 (harness reader)
        reply = json.loads(line.decode("utf-8", "replace"))
        error = reply.get("error") or {}
        if reply.get("ok") is not False or error.get("kind") != "invalid_args":
            problems.add("an over-cap request line answered %r, expected a typed invalid_args "
                         "refusal" % reply)
        if str(REQUEST_LINE_CAP) not in (error.get("message") or ""):
            problems.add("the refusal does not name the %d-byte cap: %r"
                         % (REQUEST_LINE_CAP, error))
        if reply.get("id") != -1:
            problems.add("the refusal carries id %r: an over-cap line must not be dispatched as "
                         "a request" % reply.get("id"))
        # Retired, not silently buffered: a well-formed request sent now gets no
        # reply inside the bound. (Before the cap existed the junk was buffered,
        # spliced into the next line and answered as a malformed request - which is
        # why this check discriminates rather than merely waiting.)
        try:
            quiet = client.call(7, "control.ping", timeout=5.0)
        except Timeout as no_reply:
            print("no reply to a request sent after the over-cap line (expected): %s" % no_reply)
        else:
            problems.add("the connection is still serving requests after an over-cap line: it "
                         "answered %r, so the line was buffered rather than capped" % quiet)
        client.close()
    finally:
        instance.close()
    return outcome(name, problems)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print("FAIL: no such file: %s" % binary)
        return 1
    return finish([
        case_regular_file(binary),
        case_stale_socket(binary),
        case_free_path(binary),
        case_directory(binary),
        case_symlink(binary),
        case_request_line_cap(binary),
    ])


if __name__ == "__main__":
    sys.exit(main())
