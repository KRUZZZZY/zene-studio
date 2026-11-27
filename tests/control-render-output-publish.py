#!/usr/bin/env python3
"""A failed render never costs the file at the caller's `out` (cluster 5735).

THE CLAIM UNDER TEST, measured on the REAL binary over --control-socket:

  1. PUBLISH ON SUCCESS. A render that finishes writes the file the caller asked for,
     over whatever was there before (a previous render), and the reply's bytes/sha256
     describe THAT file. No worker file is left in the directory afterwards.

  2. A FAILED RENDER DOES NOT TOUCH `out`. `render.render` lets the child write a path
     of its own and moves it to the caller's path only after the child finished
     successfully, so a child that fails leaves the caller's file exactly as it was,
     byte for byte, instead of replacing it with a truncated WAV. Here the child cannot
     write past a fixed file size - the same class of failure as a full disk or the
     render TIMEOUT the finding describes, since such a child never gets to tidy up
     after itself.

  3. A FAILED RENDER CREATES NOTHING. With no file at `out` before the call, the
     failure leaves no file there either, and no worker file beside it.

  4. A SYMLINK AT `out` STAYS A SYMLINK, WHOLE CHAIN. When `out` is a link to a link to
     a file, the render replaces the FINAL file and every link in the chain is still a
     link with the text it had (absolute and relative texts both); a failed render
     leaves the final file and all the links as they were. No worker file is left in
     any directory of the chain.

  5. A TIMED-OUT RENDER IS CLEANED UP. With ZENE_CONTROL_RENDER_TIMEOUT_MS=1 the
     render is refused, the file at `out` is byte-identical, no worker file is left,
     and no render child of the instance is still running (or left unreaped) when
     the reply arrives (/proc; the sub-check is noted and skipped where there is none).

THE FAULT IS INJECTED, NOT TIMED: the test lowers RLIMIT_FSIZE (soft) around the
instance's spawn, so the app and every child it spawns hit a fixed FILE SIZE instead
of a wall-clock delay. No sleeps and no races, and the render's own output (megabytes
at the song this builds) is far above the limit, so the failure is certain. A host
without POSIX resource limits cannot inject it and reports ctest "skipped" (exit 77,
the reason printed), never "passed". ONLY that case is a skip.

RED BEFORE THE FIX: `renderSession` handed the caller's own path to the child, so the
truncated child output was written at `out`, destroying the file that was there
(measured: 240 bytes there before the call, 524288 bytes of truncated RIFF after it).

Usage: QT_QPA_PLATFORM=offscreen python3 control-render-output-publish.py <zene>
Exit codes: 0 every check held; 1 a check failed or the run could not complete (a
missing binary, an instance that did not start or stopped answering, a bounded wait
that expired: the reason and the app log are printed); 2 bad usage; 77 the RLIMIT_FSIZE
fault cannot be injected on this host (CannotInject, the only skip).
"""

from __future__ import annotations

import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402

#: One engine start plus one render. A render runs in a child process and the surface
#: does not answer until it finishes (docs/RENDER-CHILD-WAIT.md).
RENDER_TIMEOUT = 180.0

#: The file size the injected limit allows. The song below renders megabytes at any
#: sane tempo, so the child cannot finish inside it: that IS the injected failure.
FILE_SIZE_LIMIT = 512 * 1024

#: A file that must survive a failed render: the caller's previous good render, as far
#: as this command is concerned - it never looks at what was already there.
PREVIOUS = b"PREVIOUS-GOOD-RENDER-OF-THIS-SONG\n" * 16

#: 8 bars of clip at the default tempo: far more audio than FILE_SIZE_LIMIT holds.
CLIP_TICKS = 8 * 768

#: A longer song for the timeout instance, so the render is still running when the 1 ms
#: bound expires.
LONG_CLIP_TICKS = 64 * 768

#: The instance reads this (positive int, milliseconds) in place of its 600000 ms child bound.
TIMEOUT_ENV = {"ZENE_CONTROL_RENDER_TIMEOUT_MS": "1"}

#: The worker names the command stages under, beside the destination (the same
#: `.zene-render-` / `.zene-backup-` prefixes RenderQueue uses for its own jobs).
WORKER_PREFIXES = (".zene-render-", ".zene-backup-")


class CannotInject(Exception):
    """The host cannot inject the fault (no resource/signal): the ONLY skip (exit 77)."""


class Recorder:
    """Collects the named checks and their evidence, so a failure names the check."""

    def __init__(self):
        self.results = []

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))

    def problems(self):
        return [(name, evidence) for name, passed, evidence in self.results if not passed]


class Session:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 700

    def call(self, command, args=None, timeout=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args or {}, timeout=timeout,
                                transcript=self.transcript)

    def ok(self, command, args=None):
        reply = self.call(command, args)
        if not reply.get("ok"):
            H.fail("%s was refused: %r" % (command, reply), transcript=self.transcript)
        return reply.get("result")

    def render(self, args):
        return self.call("render.render", args, timeout=RENDER_TIMEOUT)


def workers(directory):
    """Worker files the command left behind in the caller's directory."""
    return sorted(name for name in os.listdir(directory) if name.startswith(WORKER_PREFIXES))


def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def song_with_clip(session, name, ticks=CLIP_TICKS):
    track = session.ok("track.add", {"name": name, "type": "instrument"})["track"]
    session.ok("clip.add", {"track": track, "position": 0, "length": ticks})
    return track


def check_a_successful_render_publishes(session, outdir, recorder):
    """A render that finishes replaces the file at `out`, and nothing is left over."""
    out = os.path.join(outdir, "published.wav")
    with open(out, "wb") as handle:
        handle.write(PREVIOUS)
    reply = None
    for attempt in (1, 2):
        reply = session.render({"out": out, "format": "wav"})
        if reply.get("ok"):
            break
        # This build's render child has been measured writing a correct file and then
        # aborting at shutdown (control-socket-integration.py). The retry is printed,
        # never silent, and a real render failure fails both attempts.
        print("note: the publish render failed on attempt %d (%r)"
              % (attempt, (reply.get("error") or {}).get("message")))
    recorder.check("a render that finished is reported ok", reply.get("ok") is True,
                   repr(reply)[:300])
    if not reply.get("ok"):
        return
    result = reply.get("result") or {}
    recorder.check("the reply's path is the path the caller asked for",
                   result.get("path") == out, repr(result.get("path")))
    written = os.path.isfile(out)
    recorder.check("the file at the caller's path is the render, not what was there before",
                   written and digest(out) != hashlib.sha256(PREVIOUS).hexdigest(),
                   "size=%r" % (os.path.getsize(out) if written else None,))
    if written:
        with open(out, "rb") as handle:
            head = handle.read(12)
        recorder.check("the published file is a RIFF/WAVE file",
                       head[:4] == b"RIFF" and head[8:12] == b"WAVE", repr(head))
        recorder.check("the reply's sha256 is the sha256 of the file at that path",
                       result.get("sha256") == digest(out), repr(result.get("sha256")))
        recorder.check("the reply's bytes is the size of the file at that path",
                       result.get("bytes") == os.path.getsize(out),
                       "bytes=%r size=%r" % (result.get("bytes"), os.path.getsize(out)))
    recorder.check("a render that finished leaves no worker file beside the destination",
                   not workers(outdir), repr(workers(outdir)))


def check_the_loudness_report_follows_the_audio(session, outdir, recorder):
    """The report the child wrote beside ITS file is published beside the caller's file.

    The loudness report is a second file a render writes, named after the file it
    describes (LoudnessReport::sidecarPathFor). Staging must not leave it named after a
    worker: the caller asks for `song.wav` and reads `song.wav.loudness.txt`, whose own
    `file` line names `song.wav` - the same two files it got before any of this.
    """
    out = os.path.join(outdir, "reported.wav")
    session.ok("export.set_loudness_report", {"enabled": True})
    try:
        reply = session.render({"out": out, "format": "wav"})
        recorder.check("a render with the loudness report on is reported ok",
                       reply.get("ok") is True, repr(reply)[:300])
        sidecar = out + ".loudness.txt"
        recorder.check("the loudness report sits beside the file the caller asked for",
                       os.path.isfile(sidecar), "expected %r in %r"
                       % (sidecar, sorted(os.listdir(outdir))))
        if os.path.isfile(sidecar):
            with open(sidecar, encoding="utf-8") as handle:
                text = handle.read()
            # The report's fields are "key = value" lines (LoudnessReport.cpp:50).
            named = [line.split("=", 1)[1].strip() for line in text.splitlines()
                     if line.split("=", 1)[0].strip() == "file" and "=" in line]
            recorder.check("the report's own file line names the caller's path",
                           named == [out], repr(named))
            recorder.check("no worker name reaches the report", ".zene-render-" not in text,
                           repr(text[:200]))
        recorder.check("the loudness report's render leaves no worker file",
                       not workers(outdir), repr(workers(outdir)))
    finally:
        session.ok("export.set_loudness_report", {"enabled": False})


def check_a_failed_render_keeps_the_previous_file(session, outdir, recorder):
    """THE BUG: the child cannot finish writing, so the caller's file must be untouched."""
    out = os.path.join(outdir, "kept.wav")
    with open(out, "wb") as handle:
        handle.write(PREVIOUS)
    reply = session.render({"out": out, "format": "wav"})
    recorder.check("a render that cannot finish is refused, not reported ok",
                   reply.get("ok") is False and (reply.get("error") or {}).get("kind") == "refused",
                   repr(reply)[:300])
    written = os.path.isfile(out)
    recorder.check("the file the caller already had is still there after a failed render",
                   written, "out=%r size=%r" % (out, os.path.getsize(out) if written else None))
    if written:
        with open(out, "rb") as handle:
            blob = handle.read()
        recorder.check("the file the caller already had is byte-identical after a failed render",
                       blob == PREVIOUS, "%r bytes on disk, %r expected, head=%r"
                       % (len(blob), len(PREVIOUS), blob[:16]))
    recorder.check("a failed render leaves no worker file beside the destination",
                   not workers(outdir), repr(workers(outdir)))


def check_a_failed_render_creates_nothing(session, outdir, recorder):
    """The finding's own words: no corrupt file at the requested output path."""
    out = os.path.join(outdir, "never-written.wav")
    if os.path.exists(out):
        os.remove(out)
    reply = session.render({"out": out, "format": "wav"})
    recorder.check("a render that cannot finish is refused, not reported ok",
                   reply.get("ok") is False and (reply.get("error") or {}).get("kind") == "refused",
                   repr(reply)[:300])
    written = os.path.exists(out)
    recorder.check("a failed render left no file at the requested output path",
                   not written, "size=%r" % (os.path.getsize(out) if written else None,))
    recorder.check("a failed render that wrote nothing new leaves no worker file",
                   not workers(outdir), repr(workers(outdir)))


class Chain:
    """`out` -> middle -> target: two symlinks and a file, in two directories of their own.

    Absolute link texts, or relative ones (each resolved against the directory of the
    link that holds it, so a resolver that joins against the wrong directory misses).
    """

    def __init__(self, base, tag, relative):
        near = os.path.join(base, "chain-%s-a" % tag)
        far = os.path.join(base, "chain-%s-b" % tag)
        os.makedirs(near)
        os.makedirs(far)
        self.dirs = (near, far)
        self.target = os.path.join(far, "target.wav")
        self.middle = os.path.join(far, "middle.wav")
        self.out = os.path.join(near, "out.wav")
        with open(self.target, "wb") as handle:
            handle.write(PREVIOUS)
        if relative:
            os.symlink("target.wav", self.middle)
            os.symlink(os.path.join("..", os.path.basename(far), "middle.wav"), self.out)
        else:
            os.symlink(self.target, self.middle)
            os.symlink(self.middle, self.out)
        self.texts = {path: os.readlink(path) for path in (self.out, self.middle)}

    def state(self, path):
        return os.readlink(path) if os.path.islink(path) else "NOT A LINK"

    def left_workers(self):
        return {directory: workers(directory) for directory in self.dirs if workers(directory)}


def contents(path):
    try:
        with open(path, "rb") as handle:
            return handle.read()
    except OSError:
        return None


def render_retrying(session, out):
    """The render, once more if the child aborted at shutdown (printed, never silent)."""
    reply = None
    for attempt in (1, 2):
        reply = session.render({"out": out, "format": "wav"})
        if reply.get("ok"):
            break
        print("note: the chain render failed on attempt %d (%r)"
              % (attempt, (reply.get("error") or {}).get("message")))
    return reply


def chain_links_and_workers(recorder, label, chain):
    for path, text in chain.texts.items():
        recorder.check("%s: %s is still a link with the text it had" % (label, os.path.basename(path)),
                       os.path.islink(path) and os.readlink(path) == text,
                       "%r was %r, is %r" % (path, text, chain.state(path)))
    recorder.check("%s: no worker file is left in any directory of the chain" % label,
                   not chain.left_workers(), repr(chain.left_workers()))


def chain_target_checks(recorder, label, chain, result):
    blob = contents(chain.target) or b""
    recorder.check("%s: the final target is a regular file, not a link" % label,
                   os.path.isfile(chain.target) and not os.path.islink(chain.target),
                   repr(chain.state(chain.target)))
    recorder.check("%s: the final target is the render, a RIFF/WAVE file" % label,
                   blob[:4] == b"RIFF" and blob[8:12] == b"WAVE", repr(blob[:12]))
    recorder.check("%s: the reply's sha256 is the sha256 of the final target" % label,
                   result.get("sha256") == hashlib.sha256(blob).hexdigest(),
                   repr(result.get("sha256")))
    recorder.check("%s: the reply's bytes is the size of the final target" % label,
                   result.get("bytes") == len(blob), "bytes=%r size=%r" % (result.get("bytes"), len(blob)))


def check_a_chain_publishes(session, outdir, recorder, tag, relative):
    """A render at the head of a link chain replaces the FINAL file; every link stays a link."""
    label = "%s chain" % ("relative" if relative else "absolute")
    chain = Chain(outdir, tag, relative)
    reply = render_retrying(session, chain.out)
    recorder.check("%s: a render through the chain is reported ok" % label,
                   reply.get("ok") is True, repr(reply)[:300])
    chain_links_and_workers(recorder, label, chain)
    chain_target_checks(recorder, label, chain, reply.get("result") or {})


def check_a_failed_render_keeps_the_chain(session, outdir, recorder):
    """The same chain, a render that cannot finish: the file and the links are as they were."""
    label = "failed render through a chain"
    chain = Chain(outdir, "fail", False)
    reply = session.render({"out": chain.out, "format": "wav"})
    recorder.check("%s: the render is refused, not reported ok" % label,
                   reply.get("ok") is False and (reply.get("error") or {}).get("kind") == "refused",
                   repr(reply)[:300])
    blob = contents(chain.target)
    recorder.check("%s: the final target is byte-identical" % label, blob == PREVIOUS,
                   "%r bytes on disk, head=%r" % (len(blob or b""), (blob or b"")[:16]))
    chain_links_and_workers(recorder, label, chain)


def child_row(entry, parent_pid):
    """(pid, state, cmdline) when /proc/<entry> is a render child, or any zombie, of the parent."""
    try:
        with open("/proc/%s/stat" % entry, "rb") as handle:
            stat = handle.read().decode("utf-8", "replace")
        with open("/proc/%s/cmdline" % entry, "rb") as handle:
            command = handle.read().replace(b"\0", b" ").decode("utf-8", "replace")
    except OSError:
        return None  # gone between the listing and the read
    # Field 3 is the state and field 4 the ppid, counted after the LAST ')' (comm may hold one).
    state, ppid = stat[stat.rindex(")") + 2:].split()[:2]
    # A zombie's cmdline is empty, so a zombie of the instance counts by its state alone.
    if int(ppid) == parent_pid and (state == "Z" or ".zene-render-" in command):
        return (int(entry), state, command[:120])
    return None


def render_children(parent_pid):
    rows = (child_row(entry, parent_pid) for entry in os.listdir("/proc") if entry.isdigit())
    return [row for row in rows if row]


def check_a_timed_out_render_is_cleaned_up(session, instance, recorder):
    """The 1 ms bound expires mid-render: refused, `out` untouched, child gone and reaped."""
    out = os.path.join(instance.tmp, "timed-out.wav")
    with open(out, "wb") as handle:
        handle.write(PREVIOUS)
    reply = session.render({"out": out, "format": "wav"})
    has_proc = os.path.exists("/proc/self/stat")
    left = render_children(instance.process.pid) if has_proc else []  # at once: no wait
    recorder.check("a render that timed out is refused, not reported ok",
                   reply.get("ok") is False and (reply.get("error") or {}).get("kind") == "refused",
                   repr(reply)[:300])
    blob = contents(out)
    recorder.check("the file the caller already had is byte-identical after a timed-out render",
                   blob == PREVIOUS, "%r bytes on disk, head=%r" % (len(blob or b""), (blob or b"")[:16]))
    recorder.check("a timed-out render leaves no worker file beside the destination",
                   not workers(instance.tmp), repr(workers(instance.tmp)))
    if not has_proc:
        print("note: no /proc on this host: the render child scan was not run")
        return
    recorder.check("no render child of the instance is running or unreaped when the reply arrives",
                   not left, repr(left))


class file_size_limit:
    """Caps the size of every file THIS process's children write, for one spawn.

    POSIX RLIMIT_FSIZE, soft limit only: the hard limit is left alone, the previous
    soft limit is restored on exit, and the app inherits the cap through spawn(). A
    host without the `resource` module raises CannotInject, which main() reports as
    "skipped" (exit 77) - the injection is what makes the failure deterministic, so
    such a host must not run a weaker version of this test.
    """

    def __init__(self, limit):
        try:
            import resource
            import signal
            resource.RLIMIT_FSIZE, signal.SIGXFSZ  # noqa: B018  (absent on some hosts)
        except (ImportError, AttributeError) as error:
            raise CannotInject("this host cannot inject RLIMIT_FSIZE: %r" % (error,))
        self.resource = resource
        self.signal = signal
        self.previous = resource.getrlimit(resource.RLIMIT_FSIZE)
        soft, self.hard = self.previous
        self.limit = limit if self.hard == resource.RLIM_INFINITY else min(limit, self.hard)
        self.previous_signal = None

    def __enter__(self):
        try:
            self.previous_signal = self.signal.signal(self.signal.SIGXFSZ, self.signal.SIG_IGN)
        except (AttributeError, ValueError, OSError):
            self.previous_signal = None
        self.resource.setrlimit(self.resource.RLIMIT_FSIZE, (self.limit, self.hard))
        return self

    def __exit__(self, *_):
        self.resource.setrlimit(self.resource.RLIMIT_FSIZE, self.previous)
        if self.previous_signal is not None:
            self.signal.signal(self.signal.SIGXFSZ, self.previous_signal)
        return False


class log_on_error:
    """Prints the instance's app log when its block ends in anything but success or a skip."""

    def __init__(self, instance):
        self.instance = instance

    def __enter__(self):
        return self.instance

    def __exit__(self, kind, value, _trace):
        if kind is not None and not issubclass(kind, (CannotInject, KeyboardInterrupt)):
            print("\nthe instance's run failed: %s: %s" % (kind.__name__, value))
            self.instance.dump_log()
        return False


def in_tmp(check):
    """Adapts a check(session, directory, recorder) to check(session, instance, recorder)."""
    return lambda session, instance, recorder: check(session, instance.tmp, recorder)


def drive(instance, transcript, label, song, ticks, recorder, checks):
    """Waits for the instance, builds the song, runs the checks, quits it."""
    with log_on_error(instance):
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        print("instance (%s): %s" % (label, instance.binary))
        session = Session(client, transcript)
        session.ok("control.version")
        song_with_clip(session, song, ticks)
        for check in checks:
            check(session, instance, recorder)
        if instance.alive():
            session.ok("control.quit", {"save": False})


def check_the_instances(binary, recorder):
    transcript = H.Transcript()
    # 1. A real instance: the normal path, the publish over an existing file, link chains.
    with H.start_instance(binary) as instance:
        drive(instance, transcript, "no limit", "Publish QA", CLIP_TICKS, recorder, [
            in_tmp(check_a_successful_render_publishes),
            in_tmp(check_the_loudness_report_follows_the_audio),
            in_tmp(lambda s, d, r: check_a_chain_publishes(s, d, r, "abs", False)),
            in_tmp(lambda s, d, r: check_a_chain_publishes(s, d, r, "rel", True))])
    # 2. The same song, in an instance whose children cannot write past the limit.
    with file_size_limit(FILE_SIZE_LIMIT):
        instance = H.start_instance(binary)
    with instance:
        drive(instance, transcript, "file size limit %d bytes" % FILE_SIZE_LIMIT,
              "Failed render QA", CLIP_TICKS, recorder, [
                  in_tmp(check_a_failed_render_keeps_the_previous_file),
                  in_tmp(check_a_failed_render_creates_nothing),
                  in_tmp(check_a_failed_render_keeps_the_chain)])
    # 3. A long song, in an instance whose render child gets 1 ms.
    with H.start_instance(binary, extra_env=TIMEOUT_ENV) as instance:
        drive(instance, transcript, "render timeout 1 ms", "Timeout QA", LONG_CLIP_TICKS, recorder,
              [check_a_timed_out_render_is_cleaned_up])
    transcript.dump()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    try:
        check_the_instances(argv[1], recorder)
    except CannotInject as cannot:
        print("SKIPPED: %s" % cannot)
        return 77
    except Exception as error:  # H.Timeout/H.Blocked, OSError, a missing binary, a start failure
        print("FAILED: the run did not complete: %s: %s" % (type(error).__name__, error))
        return 1
    print("")
    for name, passed, evidence in recorder.results:
        print("  %-72s %s" % (name, "ok" if passed else "FAILED"))
    if recorder.problems():
        print("")
        for name, evidence in recorder.problems():
            print("  FAILED: %s\n      %s" % (name, evidence))
        return 1
    H.ok("a failed render never leaves the caller a partial file")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
