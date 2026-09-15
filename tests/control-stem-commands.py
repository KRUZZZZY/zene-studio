#!/usr/bin/env python3
"""RAW control-surface transcript for the `stem.*` group (0.3.0, feature row 26,
board task #653).

The acceptance evidence for the offline stem-separation surface, produced by
driving the REAL `zene` binary headless and printing every request and reply
verbatim.

The engine for this feature landed long before the ids did (include/StemSeparation/:
StemJobManager, StemModelStore, the two backends; five registered tests, and
`StemSplitPipelineTest` proves the separation itself end to end). What this file
proves is the thing that was MISSING: that the engine was reachable ONLY through
`tools/stem_split_cli.py` (outside the socket) and a GUI-only clip action, and
that the DECLARED `stem.*` ids now drive it over the socket, end to end.

It runs the REAL pipeline on the committed 458-byte stub ONNX graph
(tests/data/stub-4stem-linear.onnx) through the shipped CLI, so nothing here
needs the 166 MB HTDemucs download and nothing is faked: the same
`ExternalProcessStemSeparator` -> `stem_split_cli.py` -> onnxruntime path a real
model would take.

What it drives, in order:

  1. `stem.get_state`                      the engine's own facts: the backend,
                                           the model file, 44100 Hz, the
                                           343980-frame / 7.8 s segment, and
                                           `realtime: false` - the group makes
                                           NO live-mode claim, because HTDemucs
                                           cannot run on an audio block;
  2. the refusals                          a missing `source`, a relative path,
                                           a file that is not there, a 48 kHz
                                           file (no resampler - SPEC-stem-split
                                           OQ-1) and a non-audio file are each
                                           typed, and NO job is created by any
                                           of them;
  3. `stem.job_start` + a ping             THE ASYNCHRONY: the id comes back
                                           immediately, `stem.job_status` shows
                                           the job running, and `control.ping`
                                           still answers - the surface is NOT
                                           held for the separation (the whole
                                           point of the job manager, and the
                                           thing the child-process renders get
                                           wrong, docs/RENDER-CHILD-WAIT.md);
  4. `stem.job_status` to completion       the poll the agent actually uses;
  5. `stem.job_result`                     four float32 RIFF/WAVE files (drums,
                                           bass, other, vocals), each the mix's
                                           own length, with sha256; a re-issue
                                           into the same directory reports the
                                           files it REWROTE;
  6. `stem.job_cancel`                     a second job, slowed by
                                           LMMS_STEM_CHUNK_DELAY_MS (the
                                           separator's own test hook) so the
                                           cancel lands deterministically:
                                           cancelled, then a second cancel is
                                           refused, and its stems cannot be
                                           written (not completed);
  7. `stem.model_get_state`                the store's real path (the stub,
                                           from LMMS_STEM_MODEL), the UNPINNED
                                           default spec, and the file's own
                                           SHA-256, cross-checked against the
                                           hash this script computes locally;
  8. `stem.model_download`                 refused three ways - the unpinned
                                           default spec, an http:// URL, and a
                                           spec with no size/sha256 - which is
                                           the "never bundled, always verified"
                                           policy working. The performing path
                                           is NOT exercised here and the docs
                                           say so: CI has no pinned artefact to
                                           fetch, and a real transfer would
                                           block the surface for its duration;
  9. `control.transactions`                the A16 record: the whole group is
                                           `not_mutating`, so a separation
                                           leaves NO project transaction.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path.

Usage: QT_QPA_PLATFORM=offscreen python3 control-stem-commands.py <zene>
Exit code 0 only when every assertion held. 77 means this host cannot run the
backend at all (no python onnxruntime / no CLI script), which is a SKIP
(ctest SKIP_RETURN_CODE 77), never a pass.
"""

import hashlib
import itertools
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import wave

import control_socket_harness as H

# The stub model and the CLI shipped in the tree: without these the backend is
# unavailable and the run is a SKIP, not a failure.
TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
SOURCE_DIR = os.path.dirname(TESTS_DIR)
STUB_MODEL = os.path.join(TESTS_DIR, "data", "stub-4stem-linear.onnx")
CLI_PATH = os.path.join(SOURCE_DIR, "tools", "stem_split_cli.py")

# The model contract's own constants (include/StemSeparation/StemTypes.h):
# 44100 Hz, a 343980-frame segment, and the fixed 4-stem order.
SAMPLE_RATE = 44100
SEGMENT_FRAMES = 343980
STEM_ORDER = ["drums", "bass", "other", "vocals"]

# One source for the happy path and one for the cancel path. 16 s at 44.1 kHz is
# three chunks of the 7.8 s segment, so the slowed job has three sleeps of
# CHUNK_DELAY_MS in it and a cancel cannot race the finish.
SOURCE_SECONDS = 16
CHUNK_DELAY_MS = 1500

# The job is a background thread; these bounds are for the poll, not for a
# single socket read (every call answers in milliseconds).
JOB_TIMEOUT = 240.0
POLL_INTERVAL = 0.5

REQUEST_IDS = itertools.count(1)


def python_with_onnxruntime():
    """The interpreter the backend needs, or None."""
    for candidate in ("/usr/bin/python3", sys.executable, "python3"):
        if not candidate:
            continue
        try:
            done = subprocess.run([candidate, "-c", "import onnxruntime"],
                                  capture_output=True, timeout=60)
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0:
            return candidate
    return None


def onnxruntime_site_dir(python):
    """The site-packages root that carries onnxruntime, or None.

    The shared harness launches the instance with HOME (and XDG_*) redirected
    into its own temp directory - the headless recipe of AGENT-TOOLING.md
    section 4 - and that hides a USER-SITE onnxruntime: an interpreter probed
    from this script's own environment imports it, while the same interpreter
    inside the sandboxed instance does not. Measured on the box this test was
    written on (`HOME=<tmp> /usr/bin/python3 -c "import onnxruntime"` ->
    ModuleNotFoundError, with the module installed under the real HOME's
    ~/.local/lib/python3.x/site-packages), and it is why this function exists.

    Passing the directory through PYTHONPATH keeps the instance's interpreter
    and this script's on the SAME installation, which is the thing the test is
    about; nothing here pretends a missing dependency is present. A host that
    has no onnxruntime anywhere still reaches the SKIP below.
    """
    try:
        done = subprocess.run(
            [python, "-c", "import onnxruntime, os; "
                           "print(os.path.dirname(os.path.dirname(onnxruntime.__file__)))"],
            capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    if done.returncode != 0:
        return None
    return done.stdout.strip() or None


def write_source_wav(path, seconds=SOURCE_SECONDS, rate=SAMPLE_RATE, freq=220.0):
    """A stereo PCM16 WAV at the model's own rate - what render.render writes."""
    frames = rate * seconds
    samples = bytearray()
    for i in range(frames):
        left = int(0.35 * 32767.0 * math.sin(2.0 * math.pi * freq * i / rate))
        right = int(0.22 * 32767.0 * math.sin(2.0 * math.pi * (freq * 1.5) * i / rate))
        samples += struct.pack("<hh", left, right)
    with wave.open(path, "wb") as handle:
        handle.setnchannels(2)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(bytes(samples))
    return frames


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def wav_facts(path):
    """(bytes, frames, audio_format, channels, rate, bits) of one WAV, or None."""
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as handle:
        header = handle.read(12)
        if len(header) != 12 or header[0:4] != b"RIFF" or header[8:12] != b"WAVE":
            return None
        fmt = None
        data_bytes = None
        while True:
            chunk_header = handle.read(8)
            if len(chunk_header) < 8:
                break
            chunk_id, chunk_size = struct.unpack("<4sI", chunk_header)
            payload = handle.read(chunk_size + (chunk_size & 1))
            if chunk_id == b"fmt ":
                fmt = payload
            elif chunk_id == b"data":
                data_bytes = chunk_size
        if fmt is None or data_bytes is None:
            return None
        audio_format, channels, rate, _, _, bits = struct.unpack("<HHIIHH", fmt[:16])
    return (os.path.getsize(path), data_bytes // (channels * (bits // 8)) if bits else 0,
            audio_format, channels, rate, bits)


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None, timeout=None):
        return self.client.call(next(REQUEST_IDS), command, args, timeout=timeout,
                                transcript=self.transcript)

    def result(self, command, args=None, timeout=None):
        reply = self.call(command, args, timeout=timeout)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def ok_and_result(self, command, args=None):
        """(ok, result) of a reply.

        Distinct from result() because a stem job OBJECT carries its own `error`
        field (empty unless the job failed), so "the reply has no `error` key" is
        the wrong question for the verbs that return one - the reply's own `ok`
        flag is the right one.
        """
        reply = self.call(command, args)
        return reply.get("ok") is True, (reply.get("result") or {})

    def timed(self, command, args=None):
        started = time.time()
        result = self.result(command, args)
        return result, time.time() - started


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence=""):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def error_kind(result):
    error = result.get("error")
    return error.get("kind") if isinstance(error, dict) else None


def job_state(status):
    """The state of the FIRST job in a stem.job_status result.

    stem.job_status returns one shape for both asks - `{count, running, jobs[]}`
    - so a single-job poll reads `jobs[0]`, and an empty list means the id was
    refused (the caller sees the typed error, not a state).
    """
    jobs = status.get("jobs") or []
    return jobs[0].get("state") if jobs else None


def error_message(result):
    error = result.get("error")
    return (error.get("message") or "") if isinstance(error, dict) else repr(result)[:200]


def check_get_state(session, recorder):
    ok, state = session.ok_and_result("stem.get_state")
    recorder.check("stem.get_state answers", ok, repr(state)[:200])
    if not ok:
        return state
    recorder.check("the engine reports itself available (stub model + CLI + python)",
                   state.get("available") is True, "available=%r error=%r"
                   % (state.get("available"), state.get("error")))
    recorder.check("the backend is named", bool(state.get("backend")),
                   "backend=%r" % (state.get("backend"),))
    recorder.check("the model contract's constants are the engine's own",
                   state.get("sample_rate") == SAMPLE_RATE
                   and state.get("segment_frames") == SEGMENT_FRAMES,
                   "rate=%r segment=%r" % (state.get("sample_rate"), state.get("segment_frames")))
    recorder.check("the 7.8 s lookahead is reported",
                   abs(float(state.get("lookahead_seconds", 0)) - 7.8) < 0.01,
                   "lookahead=%r" % (state.get("lookahead_seconds"),))
    recorder.check("no live mode is claimed (realtime: false)",
                   state.get("realtime") is False, "realtime=%r reason=%r"
                   % (state.get("realtime"), state.get("realtime_reason")))
    recorder.check("the stem order is the model contract's",
                   state.get("stem_order") == STEM_ORDER, "order=%r" % (state.get("stem_order"),))
    return state


def check_refusals(session, recorder, outdir):
    missing = session.result("stem.job_start")
    recorder.check("stem.job_start with no arguments is refused typed",
                   error_kind(missing) == "invalid_args", repr(missing)[:200])
    relative = session.result("stem.job_start", {"source": "mix.wav"})
    recorder.check("a relative source is refused typed",
                   error_kind(relative) == "invalid_args", repr(relative)[:200])
    absent = session.result("stem.job_start", {"source": os.path.join(outdir, "nope.wav")})
    recorder.check("a source that is not there is refused typed",
                   error_kind(absent) == "refused" and "no source file" in error_message(absent),
                   "kind=%r message=%r" % (error_kind(absent), error_message(absent)))
    not_audio = os.path.join(outdir, "not-audio.txt")
    with open(not_audio, "w") as handle:
        handle.write("this is not audio\n")
    undecodable = session.result("stem.job_start", {"source": not_audio})
    recorder.check("a file the decoders cannot read is refused typed",
                   error_kind(undecodable) == "refused", "kind=%r message=%r"
                   % (error_kind(undecodable), error_message(undecodable)))
    wrong_rate = os.path.join(outdir, "48k.wav")
    write_source_wav(wrong_rate, seconds=1, rate=48000)
    refused_rate = session.result("stem.job_start", {"source": wrong_rate})
    recorder.check("a file that is not 44100 Hz is refused, naming the rate and OQ-1",
                   error_kind(refused_rate) == "refused"
                   and "44100" in error_message(refused_rate)
                   and "OQ-1" in error_message(refused_rate),
                   "kind=%r message=%r" % (error_kind(refused_rate), error_message(refused_rate)))
    status = session.result("stem.job_status")
    recorder.check("none of the refusals created a job",
                   status.get("count") == 0 and status.get("running") == 0,
                   "status=%r" % (status,))
    unknown = session.result("stem.job_status", {"job_id": 999999})
    recorder.check("stem.job_status on an id this instance never issued is not_found",
                   error_kind(unknown) == "not_found", repr(unknown)[:200])
    return status


def start_job(session, recorder, source, label):
    ok, started = session.ok_and_result("stem.job_start", {"source": source})
    accepted = ok and isinstance(started.get("job_id"), int)
    recorder.check("%s: stem.job_start returns an id immediately" % label, accepted,
                   repr(started)[:200])
    if not accepted:
        return None
    recorder.check("%s: the job reports the mix it was given" % label,
                   started.get("frames") == SOURCE_SECONDS * SAMPLE_RATE
                   and started.get("sample_rate") == SAMPLE_RATE,
                   "frames=%r rate=%r" % (started.get("frames"), started.get("sample_rate")))
    return started["job_id"]


def poll_until(session, job_id, wanted, timeout=JOB_TIMEOUT):
    """Poll stem.job_status until the state is in `wanted`; returns (state, seen, seconds)."""
    deadline = time.time() + timeout
    seen = []
    started = time.time()
    while time.time() < deadline:
        status = session.result("stem.job_status", {"job_id": job_id})
        state = job_state(status)
        if state not in seen:
            seen.append(state)
        if state in wanted:
            return state, seen, time.time() - started
        time.sleep(POLL_INTERVAL)
    return (seen[-1] if seen else None), seen, time.time() - started


def check_a_job_does_not_hold_the_surface(session, recorder, job_id):
    """The point of the whole design: a long job must not freeze the control surface."""
    status = session.result("stem.job_status", {"job_id": job_id})
    running = job_state(status) in ("queued", "running", "cancel_requested")
    pong, elapsed = session.timed("control.ping")
    recorder.check("the job is outstanding while the surface is pinged", running,
                   "state=%r" % (job_state(status),))
    recorder.check("control.ping answers WHILE a separation job is outstanding",
                   pong.get("pong") is True and elapsed < 5.0,
                   "ping=%r after %.2fs" % (pong, elapsed))
    return status


def check_result(session, recorder, job_id, outdir, frames_expected):
    relative = session.result("stem.job_result", {"job_id": job_id, "out": "stems"})
    recorder.check("stem.job_result refuses a relative out typed",
                   error_kind(relative) == "invalid_args", repr(relative)[:200])
    bad_format = session.result("stem.job_result",
                               {"job_id": job_id, "out": outdir, "format": "flac"})
    recorder.check("stem.job_result refuses a format it does not write",
                   error_kind(bad_format) == "invalid_args", repr(bad_format)[:200])

    result = session.result("stem.job_result", {"job_id": job_id, "out": outdir})
    recorder.check("stem.job_result writes the four stems",
                   result.get("count") == 4 and len(result.get("stems") or []) == 4,
                   repr(result)[:240])
    entries = result.get("stems") or []
    names = [entry.get("name") for entry in entries]
    recorder.check("the stem files are named for the model contract's order",
                   names == ["%s.wav" % stem for stem in STEM_ORDER], "names=%r" % (names,))
    recorder.check("the result states the format it wrote",
                   result.get("format") == "wav" and result.get("sample_format") == "float32",
                   "format=%r sample_format=%r" % (result.get("format"),
                                                   result.get("sample_format")))

    on_disk = [os.path.join(outdir, name) for name in names]
    recorder.check("every reported file is on disk",
                   all(os.path.isfile(path) for path in on_disk), "dir=%r" % (sorted(os.listdir(outdir)),))
    facts = [wav_facts(path) for path in on_disk]
    recorder.check("every stem is a float32 stereo RIFF/WAVE at 44100 Hz",
                   all(fact and fact[2] == 3 and fact[3] == 2 and fact[4] == SAMPLE_RATE
                       and fact[5] == 32 for fact in facts), "facts=%r" % (facts,))
    recorder.check("every stem is as long as the mix it came from",
                   all(fact and fact[1] == frames_expected for fact in facts),
                   "frames=%r expected=%r" % ([fact[1] if fact else None for fact in facts],
                                              frames_expected))
    hashes = [entry.get("sha256") for entry in entries]
    recorder.check("each reported sha256 is the file's own hash",
                   all(len(h or "") == 64 for h in hashes)
                   and all(h == sha256_of(path) for h, path in zip(hashes, on_disk)),
                   "hashes=%r" % (hashes,))

    again = session.result("stem.job_result", {"job_id": job_id, "out": outdir})
    recorder.check("a second write into the same directory reports the same four files",
                   again.get("count") == 4
                   and [e.get("name") for e in again.get("stems") or []] == names,
                   repr(again)[:240])
    recorder.check("a re-write is byte for byte the same audio",
                   [e.get("sha256") for e in again.get("stems") or []] == hashes,
                   "second=%r first=%r" % ([e.get("sha256") for e in again.get("stems") or []],
                                           hashes))
    return result


def check_cancel(session, recorder, source, outdir):
    job_id = start_job(session, recorder, source, "the cancel job")
    if job_id is None:
        return
    state, seen, _ = poll_until(session, job_id, ("running",), timeout=60.0)
    recorder.check("the slowed job reaches `running` (the cancel has a subject)",
                   state == "running", "state=%r seen=%r" % (state, seen))
    accepted, cancelled = session.ok_and_result("stem.job_cancel", {"job_id": job_id})
    recorder.check("stem.job_cancel answers with the job's state",
                   accepted and cancelled.get("state") in ("cancel_requested", "cancelled"),
                   "accepted=%r state=%r" % (accepted, cancelled.get("state")))
    final, seen_after, seconds = poll_until(session, job_id, ("cancelled", "failed"), timeout=120.0)
    recorder.check("the job settles as cancelled",
                   final == "cancelled", "final=%r seen=%r after %.1fs" % (final, seen_after, seconds))
    again = session.result("stem.job_cancel", {"job_id": job_id})
    recorder.check("a second cancel of a finished job is refused typed",
                   error_kind(again) == "refused" and "already cancelled" in error_message(again),
                   "kind=%r message=%r" % (error_kind(again), error_message(again)))
    refused = session.result("stem.job_result", {"job_id": job_id, "out": outdir})
    recorder.check("a cancelled job's stems cannot be written (not completed)",
                   error_kind(refused) == "refused" and "not completed" in error_message(refused),
                   "kind=%r message=%r" % (error_kind(refused), error_message(refused)))


def check_model_store(session, recorder, model_dir):
    ok, state = session.ok_and_result("stem.model_get_state")
    recorder.check("stem.model_get_state answers", ok, repr(state)[:200])
    if not ok:
        return
    recorder.check("the store resolves the model the environment pins",
                   state.get("path") == STUB_MODEL and state.get("present") is True,
                   "path=%r present=%r" % (state.get("path"), state.get("present")))
    recorder.check("the store's directory is the one LMMS_STEM_MODEL_DIR names",
                   state.get("dir") == model_dir, "dir=%r" % (state.get("dir"),))
    spec = state.get("spec") or {}
    recorder.check("the default spec is UNPINNED (models are never bundled)",
                   spec.get("pinned") is False and state.get("download_allowed") is False,
                   "pinned=%r allowed=%r" % (spec.get("pinned"), state.get("download_allowed")))
    card = spec.get("model_card_url") or ""
    reason = state.get("download_reason") or ""
    recorder.check("the refusal names the model card an operator must go to",
                   bool(card) and card in reason, "reason=%r" % (reason,))

    hashed = session.result("stem.model_get_state", {"hash": True})
    recorder.check("the optional hash is the file's own SHA-256",
                   hashed.get("sha256") == sha256_of(STUB_MODEL),
                   "reported=%r local=%r" % (hashed.get("sha256"), sha256_of(STUB_MODEL)))
    recorder.check("with nothing pinned there is no match verdict to claim",
                   hashed.get("matches_spec") is None, "matches=%r" % (hashed.get("matches_spec"),))

    default = session.result("stem.model_download")
    recorder.check("a download of the unpinned default spec is refused typed",
                   error_kind(default) == "refused" and "unpinned" in error_message(default),
                   "kind=%r message=%r" % (error_kind(default), error_message(default)))
    insecure = session.result("stem.model_download",
                             {"url": "http://example.invalid/model.onnx",
                              "sha256": "0" * 64, "size_bytes": 10})
    recorder.check("an http:// download is refused typed (HTTPS only)",
                   error_kind(insecure) == "refused" and "HTTPS" in error_message(insecure),
                   "kind=%r message=%r" % (error_kind(insecure), error_message(insecure)))
    unpinned = session.result("stem.model_download",
                             {"url": "https://example.invalid/model.onnx"})
    recorder.check("a spec with no sha256/size is refused typed (pinning is not optional)",
                   error_kind(unpinned) == "refused"
                   and "pinned" in error_message(unpinned),
                   "kind=%r message=%r" % (error_kind(unpinned), error_message(unpinned)))


def check_transactions(session, recorder):
    records = session.result("control.transactions")
    entries = records.get("transactions") or []
    named = [entry for entry in entries if str(entry.get("command", "")).startswith("stem.")]
    recorder.check("the whole stem.* group leaves no project transaction",
                   not named, "records=%r" % (named[:2],))


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report_results(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-66s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)


def run_checks(session, instance, recorder, outdir):
    check_get_state(session, recorder)
    check_refusals(session, recorder, outdir)

    source = os.path.join(outdir, "mix.wav")
    frames = write_source_wav(source)
    job_id = start_job(session, recorder, source, "the happy path")
    if job_id is not None:
        check_a_job_does_not_hold_the_surface(session, recorder, job_id)
        state, seen, seconds = poll_until(session, job_id, ("completed", "failed", "cancelled"))
        recorder.check("the job completes", state == "completed",
                       "state=%r seen=%r after %.1fs" % (state, seen, seconds))
        recorder.check("the poll saw the job in a non-terminal state first",
                       bool([s for s in seen if s in ("queued", "running")]),
                       "seen=%r" % (seen,))
        if state == "completed":
            check_result(session, recorder, job_id, os.path.join(outdir, "stems"), frames)
    check_cancel(session, recorder, source, os.path.join(outdir, "cancelled"))
    check_model_store(session, recorder, instance.extra_env["LMMS_STEM_MODEL_DIR"])
    check_transactions(session, recorder)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.isfile(STUB_MODEL) or not os.path.isfile(CLI_PATH):
        print("SKIP: the stub model or the reference CLI is missing from this tree "
              "(model=%r cli=%r)" % (STUB_MODEL, CLI_PATH))
        return 77
    python = python_with_onnxruntime()
    if python is None:
        print("SKIP: no python interpreter with the onnxruntime module on this host, so the "
              "external-process backend cannot run (the C++ in-process backend was not "
              "compiled in either: no ONNX Runtime SDK at configure time)")
        return 77

    recorder = Recorder()
    transcript = H.Transcript()
    workdir = tempfile.mkdtemp(prefix="zstem-run-")
    instance = None
    try:
        env = {
            "LMMS_STEM_MODEL": STUB_MODEL,
            "LMMS_STEM_CLI": CLI_PATH,
            "LMMS_STEM_PYTHON": python,
            "LMMS_STEM_CHUNK_DELAY_MS": str(CHUNK_DELAY_MS),
        }
        # The instance's HOME is redirected into its own temp directory, so a
        # user-site onnxruntime is invisible to it unless its directory travels
        # in PYTHONPATH (see onnxruntime_site_dir).
        site_dir = onnxruntime_site_dir(python)
        if site_dir:
            existing = os.environ.get("PYTHONPATH")
            env["PYTHONPATH"] = site_dir if not existing else site_dir + os.pathsep + existing
        instance = H.Instance(argv[1], extra_env=env)
        # The store's own directory, sandboxed: the default model directory is
        # read from the environment before anything else, so the real one is
        # never touched by this test.
        instance.extra_env["LMMS_STEM_MODEL_DIR"] = os.path.join(instance.tmp, "models")
        instance.spawn()
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        print("model:    %s" % STUB_MODEL)
        print("cli:      %s" % CLI_PATH)
        print("python:   %s" % python)
        print("site:     %s" % (os.environ.get("PYTHONPATH") or site_dir or "(none)"))
        session.result("control.version")
        try:
            run_checks(session, instance, recorder, workdir)
            check_quit(session, instance, recorder)
        except (H.Timeout, H.Blocked) as error:
            # A hang or a dead instance is a FAILURE, never a skip (the harness's
            # own rule). Print what the instance itself says before the traceback
            # so a reader of this transcript gets the diagnosis, not just the
            # exception - the harness's own diagnosis printer only covers its
            # bounded reads.
            print("")
            print("---- the instance stopped answering ----")
            print("alive:      %r" % (instance.alive(),))
            print("returncode: %r" % (instance.process.returncode if instance.process else None,))
            print("stderr tail:")
            print(instance.stderr_text()[-3000:])
            print("---- end ----")
            raise
    finally:
        if instance is not None and instance.alive():
            instance.kill()
        shutil.rmtree(workdir, ignore_errors=True)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("stem.* control-surface transcript")
        return 1
    H.ok("stem.* control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
