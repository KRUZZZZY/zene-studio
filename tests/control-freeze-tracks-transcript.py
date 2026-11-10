#!/usr/bin/env python3
"""RAW control-surface transcript for freeze.tracks (Q050-327), against the REAL binary.

Builds two audible instrument tracks through the commands an agent has, then:

  1. freeze.tracks on both returns at once with ONE task id; task.list lists that
     task exactly once while it runs, and it ends 'done';
  2. track.get_state reads both tracks frozen with their audio loaded, each take a
     real file; control.undo_depth grew by exactly ONE;
  3. ONE control.undo thaws both tracks;
  4. a second batch is cancelled through task.cancel while its renders run: the
     task ends 'cancelled', neither track is frozen and the undo depth is unchanged;
  5. the refusals: one track only, and a track that is already frozen.

The child renders are the shipped CLI render (BounceInPlace), so this is the real
render path. Exit 0 only when every check held; 77 (Skipped, never Passed) when
this build has no loadable instrument.

Usage: QT_QPA_PLATFORM=offscreen python3 control-freeze-tracks-transcript.py <zene>
"""

import os
import sys
import time

import control_socket_harness as H
from freeze_bounce_evidence import (Recorder, Session, load_audible_instrument,
                                    report_on_abort, report_results)

CLIP_TICKS = 192


def build_track(session, name):
    track = session.result("track.add", {"type": "instrument", "name": name}).get("track")
    if not track or not load_audible_instrument(session, track):
        return None
    clip = session.result("clip.add", {"track": track, "position": 0, "length": CLIP_TICKS}).get("clip")
    session.result("note.add", {"clip": clip, "key": 60, "position": 0, "length": CLIP_TICKS // 2,
                                "velocity": 120})
    return track


def frozen_state(session, track):
    state = session.result("track.get_state", {"track": track})
    entry = state.get("track") if isinstance(state.get("track"), dict) else state
    return bool(entry.get("frozen")), bool(entry.get("frozen_audio_ready")), entry.get("frozen_audio") or ""


def task_entries(session, task_id):
    tasks = session.result("task.list").get("tasks") or []
    return [t for t in tasks if t.get("id") == task_id]


def wait_task(session, task_id, timeout=300):
    deadline = time.time() + timeout
    while time.time() < deadline:
        entries = task_entries(session, task_id)
        if entries and entries[0].get("state") in ("done", "failed", "cancelled"):
            return entries[0]
        time.sleep(0.2)
    return (task_entries(session, task_id) or [{}])[0]


def depth(session):
    return session.result("control.undo_depth").get("depth")


def check_batch(session, recorder, outdir, a, b):
    """Steps 1-3: one task, both frozen, one undo step, one undo thaws both."""
    before = depth(session)
    started = session.result("freeze.tracks", {"tracks": [a, b], "out_dir": outdir})
    task_id = started.get("task")
    recorder.check("freeze.tracks answers at once with one batch and one task id",
                   isinstance(task_id, int) and task_id > 0 and len(started.get("tracks") or []) == 2,
                   "reply=%r" % started)
    recorder.check("task.list lists the batch's task exactly once",
                   len(task_entries(session, task_id)) == 1, "entries=%r" % task_entries(session, task_id))
    ended = wait_task(session, task_id)
    recorder.check("the batch's task ends done", ended.get("state") == "done", "task=%r" % ended)
    for track in (a, b):
        frozen, ready, audio = frozen_state(session, track)
        recorder.check("%s is frozen with its take loaded" % track, frozen and ready and os.path.isfile(audio),
                       "frozen=%r ready=%r audio=%r" % (frozen, ready, audio))
    after = depth(session)
    recorder.check("the batch is ONE undo step", isinstance(before, int) and after == before + 1,
                   "before=%r after=%r" % (before, after))
    session.result("control.undo")
    thawed = [frozen_state(session, t)[0] for t in (a, b)]
    recorder.check("one control.undo thaws both tracks", thawed == [False, False], "frozen=%r" % thawed)


def check_cancel(session, recorder, outdir, a, b):
    """Step 4: task.cancel on a running batch freezes nothing and records nothing."""
    before = depth(session)
    second = session.result("freeze.tracks", {"tracks": [a, b], "out_dir": os.path.join(outdir, "cancelled")})
    cancelled = session.result("task.cancel", {"id": second.get("task")})
    recorder.check("task.cancel accepts the running batch", cancelled.get("cancelling") is True, "%r" % cancelled)
    ended = wait_task(session, second.get("task"))
    recorder.check("the cancelled batch's task ends cancelled", ended.get("state") == "cancelled", "task=%r" % ended)
    states = [frozen_state(session, t)[0] for t in (a, b)]
    recorder.check("a cancelled batch freezes no track", states == [False, False], "frozen=%r" % states)
    recorder.check("a cancelled batch records no undo step", depth(session) == before,
                   "before=%r after=%r" % (before, depth(session)))


def check_refusals(session, recorder, outdir, a, b):
    """Step 5: one track, and a batch holding a frozen track, are refused whole."""
    refusal = session.typed_error("freeze.tracks", {"tracks": [a]})
    recorder.check("one track is refused (a batch needs two)", refusal.get("kind") == "refused"
                   or "two or more" in str(refusal), "error=%r" % refusal)
    session.result("freeze.track", {"track": a, "out": os.path.join(outdir, "single.wav")})
    refusal = session.typed_error("freeze.tracks", {"tracks": [a, b]})
    recorder.check("a batch with a frozen track is refused whole", a in str(refusal), "error=%r" % refusal)
    recorder.check("the refused batch left the other track unfrozen", frozen_state(session, b)[0] is False, "")


def run_checks(session, recorder, outdir):
    a = build_track(session, "Batch A")
    b = build_track(session, "Batch B")
    if a is None or b is None:
        return False
    check_batch(session, recorder, outdir, a, b)
    check_cancel(session, recorder, outdir, a, b)
    check_refusals(session, recorder, outdir, a, b)
    return True


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    audible = False
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        try:
            audible = run_checks(session, recorder, instance.tmp)
        except BaseException:
            report_on_abort(recorder, transcript, instance)
            raise
    report_results(recorder)
    transcript.dump()
    if not audible:
        H.ok("no audible instrument in this build: Skipped, never Passed")
        return 77
    if recorder.problems:
        recorder.problems.report("freeze.tracks control-surface transcript")
        return 1
    H.ok("freeze.tracks control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
