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


import os
import shutil
import sys
import tempfile

import control_socket_harness as H
from stem_commands_lib import (CHUNK_DELAY_MS, CLI_PATH, SAMPLE_RATE, SOURCE_SECONDS, STUB_MODEL,
                             Recorder, Session, check_a_job_does_not_hold_the_surface, check_cancel,
                             check_get_state, check_model_store, check_refusals, check_result,
                             check_transactions, job_state, onnxruntime_site_dir, poll_until,
                             python_with_onnxruntime, start_job, write_source_wav)

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

