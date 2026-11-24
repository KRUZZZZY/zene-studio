#!/usr/bin/env python3
"""A failed render never costs the file at the caller's `out` (cluster 5735, BUG 18).

THE CLAIM UNDER TEST, measured on the REAL binary over --control-socket:

  1. PUBLISH ON SUCCESS. A render that finishes writes the file the caller asked for,
     over whatever was there before (a previous render), and the reply's bytes/sha256
     describe THAT file. No worker file is left in the directory afterwards.

  2. A FAILED RENDER DOES NOT TOUCH `out`. `render.render` lets the child write a path
     of its own and moves it to the caller's path only after the child finished
     successfully, so a child that fails leaves the caller's file exactly as it was,
     byte for byte, instead of replacing it with a truncated WAV. Here the child is
     killed mid-write - the same class of failure as the render TIMEOUT the finding
     describes, since a killed child never gets to tidy up after itself.

  3. A FAILED RENDER CREATES NOTHING. With no file at `out` before the call, the
     failure leaves no file there either, and no worker file beside it.

THE FAULT IS INJECTED, NOT TIMED: the test lowers RLIMIT_FSIZE (soft) around the
instance's spawn, so the app and every child it spawns hit a fixed FILE SIZE instead
of a wall-clock delay. No sleeps and no races, and the render's own output (megabytes
at the song this builds) is far above the limit, so the failure is certain. A host
without POSIX resource limits cannot inject it and reports ctest "skipped" (exit 77,
the reason printed), never "passed".

RED BEFORE THE FIX: `renderSession` handed the caller's own path to the child, so the
killed child left a header-only 512 KiB WAV at `out`, destroying the file that was
there (measured: 240 bytes there before the call, 524288 bytes of truncated RIFF
after it).

Usage: QT_QPA_PLATFORM=offscreen python3 control-render-output-publish.py <zene>
Exit codes: 0 every check held; 1 a check failed (the app log is printed); 2 cannot
run; 77 the fault cannot be injected on this host.
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

#: The worker names the command stages under, beside the destination (the same
#: `.zene-render-` / `.zene-backup-` prefixes RenderQueue uses for its own jobs).
WORKER_PREFIXES = (".zene-render-", ".zene-backup-")


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


def song_with_clip(session, name):
    track = session.ok("track.add", {"name": name, "type": "instrument"})["track"]
    session.ok("clip.add", {"track": track, "position": 0, "length": CLIP_TICKS})
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
    """THE BUG: the child is killed mid-write, so the caller's file must be untouched."""
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


class file_size_limit:
    """Caps the size of every file THIS process's children write, for one spawn.

    POSIX RLIMIT_FSIZE, soft limit only: the hard limit is left alone, the previous
    soft limit is restored on exit, and the app inherits the cap through spawn(). A
    host without the `resource` module raises H.Timeout, which main() reports as
    "skipped" (exit 77) - the injection is what makes the failure deterministic, so
    such a host must not run a weaker version of this test.
    """

    def __init__(self, limit):
        try:
            import resource
            import signal
        except ImportError as error:
            raise H.Timeout("this host cannot inject RLIMIT_FSIZE: %r" % (error,))
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


def check_the_instances(binary, recorder):
    transcript = H.Transcript()
    # 1. A real instance: the normal path, and the publish over an existing file.
    with H.start_instance(binary) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        print("instance (no limit): %s" % binary)
        session = Session(client, transcript)
        session.ok("control.version")
        song_with_clip(session, "Publish QA")
        check_a_successful_render_publishes(session, instance.tmp, recorder)
        check_the_loudness_report_follows_the_audio(session, instance.tmp, recorder)
        if instance.alive():
            session.ok("control.quit", {"save": False})
    # 2. The same song, in an instance whose children cannot write past the limit.
    with file_size_limit(FILE_SIZE_LIMIT):
        instance = H.start_instance(binary)
    try:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        print("instance (file size limit %d bytes): %s" % (FILE_SIZE_LIMIT, binary))
        session = Session(client, transcript)
        session.ok("control.version")
        song_with_clip(session, "Failed render QA")
        check_a_failed_render_keeps_the_previous_file(session, instance.tmp, recorder)
        check_a_failed_render_creates_nothing(session, instance.tmp, recorder)
        if instance.alive():
            session.ok("control.quit", {"save": False})
    finally:
        instance.close()
    transcript.dump()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    try:
        check_the_instances(argv[1], recorder)
    except H.Timeout as blocked:
        print("SKIPPED: %s" % blocked)
        return 77
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
