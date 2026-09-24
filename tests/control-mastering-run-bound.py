#!/usr/bin/env python3
"""The bounded-reply proof for `mastering.run` (DEFECT-D3).

THE CLAIM UNDER TEST, in one sentence: a `mastering.run` request is answered
inside the client's own socket bound however long the render behind it takes, and
the run it started is still completable and readable when it is done.

WHY THIS IS A REGISTERED CTEST AND NOT A UNIT TEST. DEFECT-D3 was measured twice
in the 2026-09-24 certification sweep, on a real instance, as a socket-protocol
violation: "hang >30s waiting for mastering.run" (evidence
`/tmp/zene-cert-w2r3-rw/run-6413{,-replay}/`). The product's own record of the
cause is `docs/RENDER-CHILD-WAIT.md:120-126` - the handler started the render
child and then WAITED for it on the dispatch thread, so the control surface
answered nothing until the child was done - and the child's own PERFLOG line in
that evidence reads `Project Render | 15.80user, 3.46system 19.27elapsed`, i.e.
the render was real and the silence was the wait. Nothing but a real socket, a
real child process and a real clock can measure that.

HOW THE 30 S SILENCE IS MADE DETERMINISTIC. The shipped fixture
(`tools/auto-mastering-demo.py`) renders in seconds on this box, so a run of it
would never outlast the bound and this test would pass vacuously against the
defect. The render child is therefore HELD by the product's own test hook -
`ZENE_MASTER_SLOW_CHILD_MS` (src/core/MasteringJob.cpp, the shape
`LMMS_STEM_CHUNK_DELAY_MS` uses for the stem cancellation path) - for longer than
the socket bound. Only the child master runs are held: the instance that serves
the socket never runs a MasteringJob, and the variable is unset in every ordinary
run. This is what makes the red/green pair load-bearing: with the fix reverted
the handler blocks on that held child and this test reports NO REPLY, which is
exactly DEFECT-D3.

WHAT IT ASSERTS, all read off the wire:

  * the request is ANSWERED inside `control_socket_harness.SOCKET_TIMEOUT`
    (30 s, the module's own documented bound - not a timeout raised to hide a
    wait), and the answer is the documented ACK: `state: "running"`, naming
    `mastering.get_state` as the verb to poll;
  * THE SURFACE IS STILL ANSWERABLE while that run is in flight: a second
    command (`control.ping`, the probe whose whole purpose is to be answerable
    when everything else is busy) is answered promptly, and a second
    `mastering.run` is refused `busy` rather than queueing behind work the caller
    cannot see;
  * the run COMPLETES and its result is reachable: `mastering.get_state` reaches
    `state: "completed"` with the run's own document in `last_run` - one project
    render, five measured candidates, six wav files on disk matching the paths
    the document reports;
  * the run's scratch is NOT in the shared temp directory: no `zene-master-*`
    entry appears there for the run (the pre-fix shape left
    `/tmp/zene-master-<pid>-<ms>.mmp` and its `.json.stderr` behind, which is the
    same certification sweep's leakage observation).

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-mastering-run-bound.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path, or no shipped fixture generator).
"""

from __future__ import annotations

import glob
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)
import mastering_probe_lib as M  # noqa: E402  (path set above)

from mastering_probe_lib import (  # noqa: E402  (path set above)
    CANDIDATES, SOURCE_RENDER, Recorder, Session, wav_names,
)

#: How long the render child is HELD by the product's test hook. Longer than
#: SOCKET_TIMEOUT (30 s) with margin, so the pre-fix handler is still blocked
#: when the client's bound expires - the defect, made deterministic.
CHILD_HOLD_MS = 40000

#: What a SECOND command may cost while a run is in flight. This is the
#: "unbounded silent block also wedges any client that queues behind it" half of
#: DEFECT-D3, and it is deliberately far below SOCKET_TIMEOUT: the surface must
#: be answering, not merely eventually answering.
BUSY_SURFACE_BOUND_S = 10.0

#: How long the run itself may take to complete after its ACK. The held child
#: (40 s) plus its own engine start and the fixture's render.
COMPLETION_BOUND_S = 180.0

#: The scratch name the pre-fix shape leaked into the shared temp directory.
LEGACY_SCRATCH_GLOB = "zene-master-*"


def legacy_scratch():
    """The pre-fix scratch entries in the shared temp directory, by path."""
    return tuple(sorted(glob.glob(os.path.join(tempfile.gettempdir(), LEGACY_SCRATCH_GLOB))))


def check_ack(recorder, ack, elapsed):
    """The reply is the documented ACK, and it arrived inside the wire bound."""
    recorder.check("mastering.run is answered inside the client's own 30 s bound",
                   elapsed < H.SOCKET_TIMEOUT,
                   "reply took %.2fs, bound %.1fs" % (elapsed, H.SOCKET_TIMEOUT))
    recorder.check("mastering.run answers with the documented ACK, not the result",
                   ack.get("state") == "running",
                   "state=%r" % ack.get("state"))
    recorder.check("the ACK names the verb that reports the run",
                   ack.get("poll") == "mastering.get_state",
                   "poll=%r" % ack.get("poll"))
    recorder.check("the ACK says the render is asynchronous, so a client does not read it as done",
                   "ACCEPTED" in (ack.get("note") or ""),
                   "note=%r" % (ack.get("note") or "")[:120])


def check_surface_answers_while_running(session, recorder, outdir):
    """A second command, and a second run, while the first render is in flight."""
    started = time.monotonic()
    ping = session.call("control.ping")
    elapsed = time.monotonic() - started
    recorder.check("control.ping is answered while a mastering run is in flight",
                   ping.get("ok") is True and elapsed < BUSY_SURFACE_BOUND_S,
                   "ok=%r after %.2fs" % (ping.get("ok"), elapsed))

    error = session.typed_error("mastering.run", {"out_dir": outdir})
    recorder.check("a second mastering.run while one is in flight is refused busy, typed",
                   error.get("kind") == "busy" and bool(error.get("message")),
                   "%r" % error)


def wait_for_completion(session, recorder):
    """Polls mastering.get_state for the terminal state, and returns that state."""
    deadline = time.monotonic() + COMPLETION_BOUND_S
    state = {}
    while time.monotonic() < deadline:
        state = session.result("mastering.get_state")
        if state.get("state") in ("completed", "failed"):
            return state
        time.sleep(0.5)
    return state


def check_completion(recorder, state, outdir):
    """The run completed, and its own document is reachable and matches the disk."""
    recorder.check("the run reaches a terminal state inside the completion bound",
                   state.get("state") == "completed",
                   "state=%r error=%r" % (state.get("state"), state.get("error")))
    last = state.get("last_run") or {}
    recorder.check("the completed run's own document is reachable through get_state",
                   state.get("has_run") is True
                   and last.get("render_count") == 1
                   and last.get("candidate_count") == CANDIDATES,
                   "has_run=%r render_count=%r candidate_count=%r"
                   % (state.get("has_run"), last.get("render_count"),
                      last.get("candidate_count")))
    on_disk = wav_names(outdir)
    recorder.check("the run's document reports the candidates that are on disk",
                   len(on_disk) == CANDIDATES + 1 and SOURCE_RENDER in on_disk,
                   "files=%r" % (on_disk,))
    reported = {os.path.basename(row.get("file") or "")
                for row in last.get("candidates") or []}
    recorder.check("every reported candidate file is the file that exists",
                   reported <= set(on_disk) and len(reported) == CANDIDATES,
                   "reported=%r on_disk=%r" % (sorted(reported), on_disk))


def check_no_legacy_scratch(recorder, before):
    """The run's own scratch is not left in the shared temp directory."""
    after = legacy_scratch()
    recorder.check("the run leaves no zene-master-* scratch in the shared temp directory",
                   after == before, "appeared=%r" % sorted(set(after) - set(before)))


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    recorder.check("control.quit answered", reply.get("ok") is True, "%r" % reply)
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-66s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    problems = recorder.problems()
    if problems:
        print("")
        print("FAIL: the mastering.run bounded-reply proof has %d failed check(s)"
              % len(problems))
        return 1
    print("")
    print("PASS: %d checks, every one a measured number" % len(recorder.results))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]):
        print("cannot run: no binary at %s" % argv[1])
        return 2

    recorder = Recorder()
    transcript = H.Transcript()
    scratch_before = legacy_scratch()
    # The child that renders is HELD: the instance launches it, and it inherits
    # this environment. See this file's header for why the hold is what makes the
    # claim falsifiable.
    with H.start_instance(argv[1],
                          extra_env={"ZENE_MASTER_SLOW_CHILD_MS": str(CHILD_HOLD_MS)}) as instance:
        project = M.shipped_fixture(os.path.join(instance.tmp, "mastering-fixture"))
        if project is None:
            print("cannot run: no shipped fixture generator at %s/tools/auto-mastering-demo.py"
                  % M.REPO_ROOT)
            return 77
        outdir = os.path.join(instance.tmp, "candidates")

        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance:  %s" % argv[1])
        print("socket:    %s" % instance.socket_path)
        print("fixture:   %s" % project)
        print("child hold: %d ms (the render outlasts the %s s socket bound)"
              % (CHILD_HOLD_MS, H.SOCKET_TIMEOUT))

        session.result("project.open", {"path": project})
        recorder.check("the fixture session is not empty",
                       session.result("mastering.get_state").get("session_empty") is False,
                       "session_empty=%r"
                       % session.result("mastering.get_state").get("session_empty"))

        started = time.monotonic()
        ack = session.result("mastering.run", {"out_dir": outdir}, timeout=H.SOCKET_TIMEOUT)
        elapsed = time.monotonic() - started
        print("mastering.run ACK after %.2fs: %r" % (elapsed, ack.get("state")))
        check_ack(recorder, ack, elapsed)

        check_surface_answers_while_running(session, recorder, outdir)
        state = wait_for_completion(session, recorder)
        check_completion(recorder, state, outdir)
        check_no_legacy_scratch(recorder, scratch_before)

        check_quit(session, instance, recorder)

    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("mastering.run is answered inside the socket bound, and the run is readable")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
