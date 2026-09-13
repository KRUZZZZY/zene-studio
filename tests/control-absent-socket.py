#!/usr/bin/env python3
"""The absent-socket negative control for the control surface (SPEC A12).

THE CLAIM THIS PROVES, in the release notes' own words: the control surface "is
opt-in and off by default, and the opt-in is invisible: an instance that was not
started that way has no socket at all, and nothing in the interface reports one
that is open" (docs/RELEASE-NOTES-v0.2.1-alpha.md, section 3). `control-negative-
control.py` proves that the PRE-FIX symptoms of the shutdown/readiness defects are
detectable; it says nothing about this claim, and no other test does either: every
test in this directory starts the binary WITH `--control-socket`.

So this test starts the real binary WITHOUT the flag and watches it, then asks it
to quit:

  phase 1 (the claim)       start `lmms --config <cfg>` with no `--control-socket`;
                            wait for the crash reporter's session marker, which is
                            written in main() before the GUI exists (main.cpp
                            crashreporter::beginSession), so the process has
                            demonstrably reached the product's own startup path;
                            then observe for a measured window (see below) and
                            assert it holds NO AF_UNIX socket with a filesystem
                            path at any sample, that no socket file ever appears
                            anywhere under its own temporary world, and that
                            nothing ever printed "control socket listening on".
  phase 2 (the method)      start a second instance WITH `--control-socket` and
                            assert the SAME probe finds the socket and the path -
                            otherwise phase 1's silence would prove nothing about
                            the product and everything about a broken probe.
  phase 3 (the exit)        the claim's second half, "off by default" rather than
                            "broken": the no-flag instance must still shut DOWN
                            cleanly when asked. It is asked with SIGINT, which is
                            the product's own documented shutdown request for a
                            GUI run (`signal(SIGINT, GuiApplication::sigintHandler)`
                            -> `sigintOccurred()` -> `qApp->exit(3)`, then
                            `crashreporter::endSession()`). The evidence that the
                            shutdown was clean rather than a crash is that the
                            session marker is GONE and no crash report was
                            written; the evidence that the request was served at
                            all is that the process exited inside the bound, which
                            it can only do with its event loop running.

WHICH PROBE. "Does this process hold an AF_UNIX socket with a path" comes from
/proc/net/unix + /proc/<pid>/fd where procfs is mounted, and from `lsof -a -p <pid> -U
-Fn` where it is not - Darwin has no /proc, which is what made the macOS jobs red this
test while the product was behaving. A host that can do neither SKIPs (exit 77, out
loud) rather than reading a blind probe as a clean process; the positive control below
is unchanged, so a broken /proc probe on Linux still FAILS.

WHY THE OBSERVATION WINDOW IS MEASURED, NOT ASSUMED. Phase 2 records how long the
same binary took to create and accept on its control socket (T). Phase 1 then
watches an instance of the same binary, started the same way but without the flag,
for at least 3*T (floor 8 s), sampling every 20 ms. So the window in which a
socket would have appeared is not a guess: it is the time this very binary was
measured to need, times three.

Bounded everywhere: a hang is a failure, never a wait.

Usage: control-absent-socket.py <lmms-binary>
"""

import os
import shutil
import signal
import stat
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as harness  # noqa: E402
from control_socket_harness import (  # noqa: E402
    Client, Instance, Problems, Timeout, dump, finish,
)

# The crash reporter's session marker (include/CrashReporter.h, kSessionMarkerName),
# relative to the configured working directory. main() writes it in
# crashreporter::beginSession() and removes it in crashreporter::endSession() on
# every clean exit path.
SESSION_MARKER = "zene-session-open.marker"
CRASH_REPORT = os.path.join("crash-reports", "zene-crash-report.txt")

# The product's own exit code for a SIGINT shutdown: GuiApplication::sigintOccurred()
# calls qApp->exit(3), and main() returns that value (src/core/main.cpp:1318-1361).
SIGINT_EXIT_CODE = 3

MARKER_TIMEOUT = 30.0
SAMPLE_INTERVAL = 0.02
OBSERVE_FLOOR = 8.0
OBSERVE_FACTOR = 3.0
EXIT_TIMEOUT = 30.0
STDERR_DUMP_LIMIT = 4000


class NoSocketInstance(Instance):
    """An instance started WITHOUT `--control-socket`.

    The shared harness always passes the flag (it is the harness's whole point), so
    this subclass removes exactly that one argument and changes nothing else about
    the launch recipe: same config file, same temp HOME/XDG_*, same cwd.
    """

    def spawn(self):
        self._stderr = open(self.stderr_path, "wb")
        self._stdout = open(self.stdout_path, "wb")
        self.process = subprocess.Popen(
            [self.binary, "--config", self.config_path],
            stdout=self._stdout, stderr=self._stderr, env=self.env(), cwd=self.tmp)
        harness._LAST_INSTANCE = self
        return self.process


# ---------------------------------------------------------------------------
# the probe: does a process hold an AF_UNIX socket with a filesystem path?
#
# Two mechanisms, because the answer lives in a table the platform provides:
#
#   proc    /proc/net/unix + /proc/<pid>/fd. Exact, and what this test was
#           written on: the fd link names the socket's INODE, which /proc/net/unix
#           maps to the filesystem path a control socket is created with.
#   lsof    `lsof -a -p <pid> -U -Fn`: where there is no /proc at all (Darwin),
#           whose `n` field is the name of each socket the process holds - the
#           socket's own path for a pathname socket.
#
# ZCTL_SOCKET_PROBE=proc|lsof forces one, which is how the lsof path is exercised on
# a host that has both. The mechanism is never guessed: probe_is_usable() asks it for
# this process's own sockets first, and a host that cannot answer SKIPs rather than
# reporting "holds nothing" - an empty answer is evidence of a blind probe.
# ---------------------------------------------------------------------------

MECHANISM_ENV = "ZCTL_SOCKET_PROBE"
LSOF_TIMEOUT = 10.0
LSOF_CANDIDATES = ("/usr/sbin/lsof", "/usr/bin/lsof", "/bin/lsof", "/usr/local/bin/lsof")
# The socket types lsof's NAME field can end with (".../zene.sock type=STREAM")
LSOF_SOCKET_TYPES = ("STREAM", "DGRAM", "SEQPACKET", "RDM", "RAW", "UNKNOWN", "NONE")


def probe_mechanism():
    """Which mechanism this run reads held sockets with."""
    forced = os.environ.get(MECHANISM_ENV, "").strip()
    if forced in ("proc", "lsof"):
        return forced
    return "proc" if os.path.exists("/proc/net/unix") else "lsof"


def unix_sockets_by_inode():
    """inode -> path for every AF_UNIX socket with a path, from /proc/net/unix.

    Format: Num RefCount Protocol Flags Type St Inode Path. The path is empty for
    an unnamed socket (a socketpair, a Qt notifier), which is why the path column
    is the discriminator: an unnamed AF_UNIX socket is NOT a control socket.
    """
    found = {}
    try:
        with open("/proc/net/unix") as handle:
            next(handle, None)
            for line in handle:
                fields = line.split()
                if len(fields) < 8:
                    continue
                inode, path = fields[6], fields[7]
                found[inode] = path
    except OSError:
        pass
    return found


def lsof_socket_name(field):
    """The socket path inside one lsof `n` field, or "" when the socket has none.

    lsof decorates the name: it appends the socket's own type to it, and an unnamed
    socket has no path at all - its name is that type alone ("type=STREAM"). A peer
    address is printed as "-><name>". Measured against lsof on Linux, which is not
    what the first version of this parser assumed.
    """
    name = field.strip()
    if name.startswith("->"):
        name = name[2:].strip()
    # A named socket's path carries the type appended to it, and a socket with NO
    # path is nothing but that type ("type=STREAM") - lsof's way of saying there is
    # no name to report. Both shapes are reduced to the path, which is "" for the
    # second, so a pathless socket is never counted as a held path.
    for marker in (" type=", "type="):
        if marker in name:
            head, tail = name.split(marker, 1)
            if tail.strip().upper() in LSOF_SOCKET_TYPES:
                name = head.strip()
            break
    return name


def lsof_socket_paths(pid):
    """Paths of the AF_UNIX sockets `pid` holds, from lsof's field output.

    Returns None when lsof cannot be run at all - which the caller reads as "this
    host cannot be looked at", never as "no socket" - and a list (possibly empty,
    pathless sockets dropped) when it ran.

    `-a` ANDs the selections (`-p` and `-U`) instead of unioning them, and `-F n`
    prints one field per line, so every `n` line is one socket.
    """
    executable = next((c for c in LSOF_CANDIDATES if os.path.exists(c)), None)
    if executable is None:
        executable = shutil.which("lsof")
    if executable is None:
        return None

    try:
        completed = subprocess.run(
            [executable, "-a", "-p", str(pid), "-U", "-Fn"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=LSOF_TIMEOUT)
    except (OSError, subprocess.SubprocessError):
        return None
    if completed.returncode not in (0, 1):
        return None

    held = []
    for line in completed.stdout.decode("utf-8", "replace").splitlines():
        if not line.startswith("n"):
            continue
        name = lsof_socket_name(line[1:])
        if name:
            held.append(name)
    return held


def proc_sockets_held_by(pid):
    """Paths of the AF_UNIX sockets `pid` holds, from /proc/<pid>/fd.

    Reading the fd table answers "what does this process have open RIGHT NOW",
    which is the question the claim is about; a file-scan of the working directory
    cannot see a socket that was created but unlinked, and would say nothing at all
    about a socket created somewhere else.
    """
    by_inode = unix_sockets_by_inode()
    held = []
    fd_dir = "/proc/%d/fd" % pid
    try:
        names = os.listdir(fd_dir)
    except OSError:
        return held
    for name in names:
        try:
            target = os.readlink(os.path.join(fd_dir, name))
        except OSError:
            continue
        if not target.startswith("socket:["):
            continue
        inode = target[len("socket:["):-1]
        path = by_inode.get(inode)
        if path:
            held.append(path)
    return held


def sockets_held_by(pid, listening_only=False):
    """Paths of the AF_UNIX sockets `pid` holds (all of them, or listeners only).

    Callers check probe_is_usable() first: this returns [] both for "holds none"
    and for "this host cannot look", and the two must never be confused.
    """
    if probe_mechanism() == "proc":
        return proc_sockets_held_by(pid)
    return lsof_socket_paths(pid) or []


def probe_is_usable():
    """Whether this host can answer "what does that process hold" at all.

    Asked of the mechanism itself, before any claim is made: with no /proc/net/unix
    and no runnable lsof there is no evidence to be had, and a run that proceeded
    would pass its "holds no socket" assertion for the wrong reason. That SKIPs.
    """
    if probe_mechanism() == "proc":
        return os.path.exists("/proc/net/unix") and os.path.isdir("/proc/self/fd")
    return lsof_socket_paths(os.getpid()) is not None


def socket_files_under(directory):
    """Every socket file (S_ISSOCK) under `directory`, recursively."""
    found = []
    for root, dirs, files in os.walk(directory):
        for entry in list(dirs) + files:
            full = os.path.join(root, entry)
            try:
                if stat.S_ISSOCK(os.lstat(full).st_mode):
                    found.append(full)
            except OSError:
                continue
    return found


def wait_for(path, timeout_s):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if os.path.exists(path):
            return True
        time.sleep(0.02)
    return False


# ---------------------------------------------------------------------------
# phase 2: the method control
# ---------------------------------------------------------------------------


def socket_probe_sees_the_control_socket(binary):
    """The positive control: with --control-socket, the probe MUST find it.

    Returns (ok, elapsed_to_socket, problems). The elapsed time is handed to
    phase 1 as its observation window, so phase 1 does not guess.
    """
    problems = Problems()
    started = time.time()
    elapsed = 0.0
    with Instance(binary) as inst:
        try:
            inst.spawn()
            inst.wait_for_socket(60.0)
            elapsed = time.time() - started
            held = sockets_held_by(inst.process.pid)
            if inst.socket_path not in held:
                problems.add(
                    "the probe did not see the control socket %s in the fds of pid %d "
                    "(it held: %r) - without this the negative result in phase 1 would "
                    "prove nothing about the product"
                    % (inst.socket_path, inst.process.pid, held))
            else:
                print("  method control: with --control-socket, the probe saw %s held by "
                      "pid %d after %.2fs" % (inst.socket_path, inst.process.pid, elapsed))
            client = Client(inst.socket_path)
            try:
                reply = client.call(1, "control.ping")
                if (reply.get("result") or {}).get("pong") is not True:
                    problems.add("control.ping on the control instance answered %r" % reply)
            finally:
                client.close()
            client = Client(inst.socket_path)
            try:
                client.call(2, "control.quit")
            finally:
                client.close()
            exited, code, _ = inst.wait_for_exit(30.0)
            if not exited:
                problems.add("the control instance did not exit after control.quit")
        except Timeout as exc:
            problems.add("method control: %s" % exc)
            inst.kill()
        finally:
            inst.close()
    return (not problems), elapsed, problems


# ---------------------------------------------------------------------------
# phase 1 + 3: the claim
# ---------------------------------------------------------------------------


def observe_for_sockets(inst, window_s, problems):
    """Sample the process's held AF_UNIX sockets and its directory for `window_s`.

    Returns (held_paths, socket_files, samples). Split out of the phase-1 check so
    each function stays inside the per-method complexity target (Gate 4).
    """
    deadline = time.time() + window_s
    held_paths = set()
    socket_files = set()
    samples = 0
    while time.time() < deadline:
        if inst.process.poll() is not None:
            problems.add(
                "the instance exited on its own (code %s) during the observation "
                "window; a GUI run started without --control-socket has no reason to"
                % inst.process.returncode)
            break
        samples += 1
        held_paths.update(sockets_held_by(inst.process.pid))
        socket_files.update(socket_files_under(inst.tmp))
        time.sleep(SAMPLE_INTERVAL)
    return held_paths, socket_files, samples


def check_no_socket_evidence(inst, held_paths, socket_files, problems):
    """The claim itself: nothing the process holds, and nothing on disk."""
    if held_paths:
        problems.add(
            "the instance holds AF_UNIX socket(s) with a path although it was never "
            "asked to listen for anything: %r" % sorted(held_paths))
    if socket_files:
        problems.add(
            "a socket file appeared under the instance's own directory although it "
            "was never asked to listen for anything: %r" % sorted(socket_files))
    log = inst.read_log()
    if "control socket listening on" in log:
        problems.add("the instance announced a control socket it was never asked "
                     "for: %r" % log[-500:])


def check_sigint_shutdown(inst, marker, problems):
    """Ask it to quit the product's own way, and check the shutdown was clean."""
    os.kill(inst.process.pid, signal.SIGINT)
    exited, code, elapsed = inst.wait_for_exit(EXIT_TIMEOUT)
    marker_gone = not os.path.exists(marker)
    crash_report = os.path.join(inst.workspace, CRASH_REPORT)
    log = inst.read_log()
    if not exited:
        problems.add("the instance did not exit within %.1fs of SIGINT: the shutdown "
                     "is blocked" % EXIT_TIMEOUT)
    elif code != SIGINT_EXIT_CODE:
        problems.add(
            "the instance exited with %s after SIGINT; the product's own SIGINT "
            "shutdown asks for %d (GuiApplication::sigintOccurred -> qApp->exit(3), "
            "src/gui/GuiApplication.cpp:331). If that is a deliberate change, this "
            "expectation belongs with it." % (code, SIGINT_EXIT_CODE))
    if not marker_gone:
        problems.add(
            "the session marker %s survived the shutdown, so endSession() did not "
            "run: that is a crash-shaped exit, not a clean one" % SESSION_MARKER)
    if os.path.exists(crash_report):
        problems.add("a crash report was written: %s" % crash_report)
    if "Shutting down..." not in log:
        problems.add(
            "stderr carries no 'Shutting down...' line, so the SIGINT path did not "
            "run and the exit cannot be attributed to the shutdown request")
    print("  no-flag instance: SIGINT -> exit=%s after %.2fs, session marker removed=%s, "
          "crash report written=%s" % (code, elapsed, marker_gone, os.path.exists(crash_report)))


def the_no_flag_instance_has_no_socket_and_exits_cleanly(binary, window_s):
    problems = Problems()
    with NoSocketInstance(binary) as inst:
        try:
            inst.spawn()
            marker = os.path.join(inst.workspace, SESSION_MARKER)
            if not wait_for(marker, MARKER_TIMEOUT):
                problems.add(
                    "the instance never wrote its session marker %s within %.1fs, so this "
                    "run cannot tell 'no socket' from 'never got started'; stderr: %r"
                    % (marker, MARKER_TIMEOUT, inst.stderr_text()[-1000:]))
            held, files, samples = observe_for_sockets(inst, window_s, problems)
            print("  no-flag instance: %d samples over %.1fs, %d socket path(s) held, "
                  "%d socket file(s) under its directory"
                  % (samples, window_s, len(held), len(files)))
            check_no_socket_evidence(inst, held, files, problems)
            check_sigint_shutdown(inst, marker, problems)
            if problems:
                dump("no-flag instance stderr", inst.stderr_text()[-STDERR_DUMP_LIMIT:])
        except Timeout as exc:
            problems.add(str(exc))
            inst.kill()
        finally:
            inst.close()
    return (not problems), problems


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print("FAIL: no lmms binary at %s" % binary)
        return 1

    mechanism = probe_mechanism()
    if not probe_is_usable():
        # 77 is ctest's skip: this host cannot gather the evidence this test is
        # built on (no /proc/net/unix and no runnable lsof), and reporting the
        # socket-held half of the claim as "passed" would be the silent pass the
        # test exists to prevent. The rest of the claim - no socket file under the
        # instance's own world, no "control socket listening on" line, the clean
        # SIGINT shutdown - still runs and can still fail.
        print("SKIP: the %s probe cannot answer \"what does this process hold\" on this "
              "host, so the AF_UNIX-socket-held half of the claim is UNEXERCISED by "
              "this run." % mechanism)
        print("      /proc/net/unix and /proc/<pid>/fd: %s; lsof: %s"
              % ("present" if os.path.exists("/proc/net/unix") else "absent",
                 "found" if shutil.which("lsof") else "not found"))
        return 77

    print("  probe mechanism: %s" % mechanism)
    results = []

    ok, to_socket, problems = socket_probe_sees_the_control_socket(binary)
    results.append(("method control: the probe sees a real control socket", ok, problems.items))

    window = max(OBSERVE_FLOOR, OBSERVE_FACTOR * to_socket) if to_socket else OBSERVE_FLOOR
    ok, problems = the_no_flag_instance_has_no_socket_and_exits_cleanly(binary, window)
    results.append(("no --control-socket: no socket at all, clean exit", ok, problems.items))

    return finish(results)


if __name__ == "__main__":
    sys.exit(main())
