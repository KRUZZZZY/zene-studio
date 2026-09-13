#!/usr/bin/env python3
"""RAW control-surface transcript for the record.* crash-recovery group (0.3.0).

The acceptance evidence for RECORDING CRASH RECOVERY, produced by driving the
REAL `lmms` binary headless and printing every request and reply verbatim.

The crash in this test is a REAL one: the first instance is SIGKILLed with a
journalled capture in progress (no clean stop, no shutdown path), and the
recovery is attempted by a SECOND instance started against the same working
directory - which is what "the next start can recover it" means.

What it drives, in order:

  1. instance A: a real WAV on disk (N frames) + `record.journal_begin` +
     `record.journal_update` to M frames (M < N by half a second of audio, i.e.
     inside the documented lag bound);
  2. `record.recovery_get_state`        the capture is visible BEFORE the crash,
     which is what makes the after-crash find meaningful;
  3. SIGKILL                            exit code -9: no clean stop, no disarm,
     no shutdown hook. This is the abnormal exit the feature exists for;
  4. instance B (same working directory):
     `record.recovery_get_state`        THE PROOF: the take is found. The entry's
     numbers are checked against the file on disk, not trusted from the reply -
     frames_journalled is M, frames_in_file is N (measured from the WAV), and
     frames_recoverable is min(M, N) = M, so the offer cannot promise audio that
     is not there;
  5. THE BOUND AS OBSERVED               `frames_in_file - frames_journalled`
     is asserted to be within `journal_lag_bound_frames` (one second of audio),
     and the ring frames a crash loses for good are reported as 65536. The bound
     the code documents is the bound this run measures;
  6. `record.recovery_restore`           the offer is taken: the journal is
     marked restored, and the WAV is byte-identical afterwards (sha256 before
     and after) - the command resolves an offer, it does not touch material;
  7. `record.recovery_get_state` again   the offer is GONE: one entry fewer;
  8. `record.recovery_discard`           on a second take: the journal file is
     removed FROM DISK (asserted with os.path.exists) while the take's WAV stays;
  9. `control.undo`                      takes a `record.journal_begin` back
     through the recorded paired command (a dispatch, not a journal unwind);
 10. `record.journal_finish`             a clean stop removes the journal and the
     take stops being offered;
 11. refusals                            a missing journal, a backwards frame
     count, a relative path, a zero sample rate, a scan dir that is not a
     directory, restoring what is not in progress - every one typed;
 12. `control.transactions`              the A16 records: the journal verbs are
     `snapshot`, the discard is `irreversible` (a typed refusal to undo, naming
     the fallback), the inspector leaves no record.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path and no second launch recipe.

Usage: QT_QPA_PLATFORM=offscreen python3 control-recording-recovery.py <lmms>
Exit code 0 only when every assertion held.
"""

import array
import hashlib
import math
import os
import shutil
import socket
import sys
import tempfile
import wave

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))

RATE = 48000
#: Frames in the crashed take: two seconds of audio at 48 kHz.
TAKE_FRAMES = 2 * RATE
#: Frames the journal was last told about: half a second behind, which is INSIDE
#: the documented lag bound of one second (RATE frames).
JOURNALLED_FRAMES = TAKE_FRAMES - RATE // 2
#: TrackRecorder::RingCapacityFrames - audio that never reached a file.
RING_FRAMES = 65536


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        return self.client.call(next(REQUEST_IDS), command, args, transcript=self.transcript)

    def result(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def typed_error(self, command, args=None):
        reply = self.call(command, args)
        return (reply.get("error") or {}) if reply.get("ok") is False else {}


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def write_take(path, frames):
    """A real 16-bit PCM WAV, written by Python so its frame count is known."""
    samples = array.array("h", [int(12000 * math.sin(i / 40.0)) for i in range(frames)])
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(RATE)
        handle.writeframes(samples.tobytes())
    return path


def sha256(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def journal_path(take):
    return take + ".rec-journal"


def fresh_take(shared, name, frames=2400):
    """A real WAV on disk for a check that needs a take, not a name."""
    return write_take(os.path.join(shared, name), frames)


def begin(session, take, **extra):
    args = {"take": take, "sample_rate": RATE, "channels": 1}
    args.update(extra)
    return session.result("record.journal_begin", args)


def find_take(state, take):
    for entry in (state.get("takes") or []):
        if entry.get("take") == take:
            return entry
    return None


def check_journal_before_the_crash(session, recorder, take):
    """The capture is journalled and visible while it is still in progress."""
    state = session.result("record.recovery_get_state")
    recorder.check("the scan default is the instance's own working directory",
                   state.get("dir") == os.path.dirname(take),
                   "dir=%r take dir=%r" % (state.get("dir"), os.path.dirname(take)))
    entry = find_take(state, take)
    recorder.check("a journalled capture is offered while it is in progress",
                   entry is not None and entry.get("state") == "in_progress",
                   "entry=%r" % entry)
    if entry is None:
        return
    recorder.check("the offer reports the frames the journal recorded",
                   entry.get("frames_journalled") == JOURNALLED_FRAMES,
                   "journalled=%r expected=%r" % (entry.get("frames_journalled"),
                                                  JOURNALLED_FRAMES))
    recorder.check("the offer's recoverable count is the journal's, not the file's",
                   entry.get("frames_recoverable") == JOURNALLED_FRAMES,
                   "recoverable=%r" % entry.get("frames_recoverable"))


def check_the_crash(instance, recorder):
    """SIGKILL: no clean stop, no disarm, no shutdown hook."""
    instance.kill()
    exited, code, waited = instance.wait_for_exit(5.0)
    recorder.check("the first instance is killed abnormally (SIGKILL, -9), not quit",
                   exited and code == -9,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))
    # The socket file is NOT removed: nothing ran to unlink it, which is part of
    # what "abnormal exit" means here. What must be true is that nothing answers.
    try:
        probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        probe.settimeout(1.0)
        probe.connect(instance.socket_path)
        probe.close()
        answered = True
    except OSError:
        answered = False
    recorder.check("the killed instance's control socket is dead, not serving",
                   not answered, "socket connected after the kill=%r" % answered)


def check_recovery_after_the_crash(session, recorder, take):
    """THE PROOF: the next start finds the capture and measures it honestly."""
    state = session.result("record.recovery_get_state")
    entry = find_take(state, take)
    recorder.check("the NEXT start finds the crashed capture", entry is not None,
                   "dir=%r count=%r" % (state.get("dir"), state.get("count")))
    if entry is None:
        return None
    recorder.check("the recovered take is reported in progress, with its file",
                   entry.get("state") == "in_progress"
                   and entry.get("journal") == journal_path(take)
                   and os.path.exists(take),
                   "entry=%r" % entry)
    recorder.check("frames_in_file is the take's OWN frame count (measured, not claimed)",
                   entry.get("frames_in_file") == TAKE_FRAMES,
                   "in_file=%r expected=%r" % (entry.get("frames_in_file"), TAKE_FRAMES))
    recorder.check("frames_journalled is what the journal recorded before the crash",
                   entry.get("frames_journalled") == JOURNALLED_FRAMES,
                   "journalled=%r expected=%r" % (entry.get("frames_journalled"),
                                                  JOURNALLED_FRAMES))
    recorder.check("frames_recoverable is min(journal, file) - the GUARANTEED count",
                   entry.get("frames_recoverable") == JOURNALLED_FRAMES,
                   "recoverable=%r" % entry.get("frames_recoverable"))
    return entry


def check_the_bound_as_observed(entry, recorder):
    """The bound the code documents is the bound this run measures."""
    if entry is None:
        recorder.check("the bound could be measured", False, "no recovered entry")
        return
    beyond = entry.get("frames_beyond_the_journal")
    lag_bound = entry.get("journal_lag_bound_frames")
    recorder.check("the material beyond the journal is inside the stated lag bound",
                   beyond == TAKE_FRAMES - JOURNALLED_FRAMES and lag_bound == RATE
                   and beyond <= lag_bound,
                   "beyond=%r lag_bound=%r" % (beyond, lag_bound))
    recorder.check("the ring frames a crash loses for good are reported, not hidden",
                   entry.get("ring_frames_not_recoverable") == RING_FRAMES,
                   "ring=%r" % entry.get("ring_frames_not_recoverable"))
    recorder.check("frames_beyond_the_journal + frames_journalled is the file's frame count",
                   beyond is not None and entry.get("frames_journalled") is not None
                   and beyond + entry.get("frames_journalled") == entry.get("frames_in_file"),
                   "beyond=%r journalled=%r in_file=%r"
                   % (beyond, entry.get("frames_journalled"), entry.get("frames_in_file")))


def check_restore(session, recorder, take, before_sha):
    """Taking the offer: the journal is resolved and the material is untouched."""
    restored = session.result("record.recovery_restore", {"take": take})
    recorder.check("record.recovery_restore reports the material it recovered",
                   restored.get("restored") is True
                   and restored.get("state") == "restored"
                   and restored.get("frames_recoverable") == JOURNALLED_FRAMES,
                   "result=%r" % restored)
    recorder.check("restore says the audio was not touched, and it was not (sha256)",
                   restored.get("audio_untouched") is True and sha256(take) == before_sha,
                   "untouched=%r sha=%s expected=%s" % (restored.get("audio_untouched"),
                                                        sha256(take)[:12], before_sha[:12]))
    after = session.result("record.recovery_get_state")
    recorder.check("the restored capture is no longer offered",
                   find_take(after, take) is None and os.path.exists(take),
                   "count=%r take exists=%r" % (after.get("count"), os.path.exists(take)))


def check_discard(session, recorder, take):
    """Refusing the offer removes the journal and keeps the audio."""
    begin(session, take)
    discarded = session.result("record.recovery_discard", {"take": take})
    recorder.check("record.recovery_discard reports the journal removed and the audio kept",
                   discarded.get("removed") is True and discarded.get("audio_kept") is True,
                   "result=%r" % discarded)
    recorder.check("the journal file is gone from disk and the take's WAV is not",
                   not os.path.exists(journal_path(take)) and os.path.exists(take),
                   "journal=%r take=%r" % (os.path.exists(journal_path(take)), os.path.exists(take)))


def check_undo_takes_the_journal_back(session, recorder, take):
    """The recorded inverse is the paired COMMAND, and it removes the journal."""
    begin(session, take)
    recorder.check("the journal was written before the undo", os.path.exists(journal_path(take)),
                   "journal exists=%r" % os.path.exists(journal_path(take)))
    undone = session.result("control.undo")
    recorder.check("one control.undo removes the journal by DISPATCHING the paired command",
                   undone.get("undone") is True
                   and undone.get("restored_by") == "record.recovery_discard",
                   "undone=%r" % undone)
    recorder.check("the journal file is gone after the undo",
                   not os.path.exists(journal_path(take)),
                   "journal exists=%r" % os.path.exists(journal_path(take)))


def check_clean_stop(session, recorder, take):
    """A clean stop retires the journal: the offer disappears with it."""
    begin(session, take)
    session.result("record.journal_update", {"take": take, "frames_on_disk": 512})
    finished = session.result("record.journal_finish", {"take": take})
    recorder.check("record.journal_finish reports the journal retired",
                   finished.get("removed") is True and finished.get("state") == "finished",
                   "result=%r" % finished)
    recorder.check("a finished capture is gone from disk and from the offers",
                   not os.path.exists(journal_path(take))
                   and find_take(session.result("record.recovery_get_state"), take) is None,
                   "journal exists=%r" % os.path.exists(journal_path(take)))
    gone = session.typed_error("record.journal_finish", {"take": take})
    recorder.check("finishing a capture with no journal is not_found",
                   gone.get("kind") == "not_found",
                   "kind=%r message=%r" % (gone.get("kind"), gone.get("message")))


def check_refusals(session, recorder, take):
    """Every refusal is typed."""
    missing = session.typed_error("record.journal_update",
                                  {"take": take + "-no-such.wav", "frames_on_disk": 1})
    recorder.check("updating a journal that does not exist is not_found",
                   missing.get("kind") == "not_found",
                   "kind=%r message=%r" % (missing.get("kind"), missing.get("message")))
    relative = session.typed_error("record.journal_begin",
                                   {"take": "relative.wav", "sample_rate": RATE})
    recorder.check("a relative take path is invalid_args",
                   relative.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (relative.get("kind"), relative.get("message")))
    zero_rate = session.typed_error("record.journal_begin",
                                    {"take": take, "sample_rate": 0})
    recorder.check("a zero sample rate is invalid_args",
                   zero_rate.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (zero_rate.get("kind"), zero_rate.get("message")))
    begin(session, take)
    session.result("record.journal_update", {"take": take, "frames_on_disk": 4096})
    backwards = session.typed_error("record.journal_update",
                                    {"take": take, "frames_on_disk": 1024})
    recorder.check("moving the frame count BACKWARDS is refused",
                   backwards.get("kind") == "refused",
                   "kind=%r message=%r" % (backwards.get("kind"), backwards.get("message")))
    session.result("record.journal_finish", {"take": take})
    begin(session, take)
    session.result("record.recovery_restore", {"take": take})
    twice = session.typed_error("record.recovery_restore", {"take": take})
    recorder.check("restoring a journal that is not an interrupted capture is refused",
                   twice.get("kind") == "refused",
                   "kind=%r message=%r" % (twice.get("kind"), twice.get("message")))
    session.result("record.recovery_discard", {"take": take})
    no_scan = session.typed_error("record.recovery_get_state", {"dir": take})
    recorder.check("scanning something that is not a directory is invalid_args",
                   no_scan.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (no_scan.get("kind"), no_scan.get("message")))


def journal_records(session):
    """The A16 records of the record.* group, and only that group's."""
    return [r for r in (session.result("control.transactions").get("transactions") or [])
            if str(r.get("command", "")).startswith("record.")]


def class_by_command(records):
    """The LAST class each command recorded, keyed by command id."""
    classes = {}
    for record in records:
        classes[record.get("command")] = record.get("class")
    return classes


def begin_inverses(records):
    return [(r.get("inverse") or {}) for r in records
            if r.get("command") == "record.journal_begin"]


def every_inverse_is_dispatched(inverses):
    return bool(inverses) and all(i.get("applies") == "command" for i in inverses)


def check_transactions(session, recorder):
    """The A16 records: snapshots, one irreversible, no record for a read."""
    records = journal_records(session)
    classes = class_by_command(records)
    recorder.check("the journal verbs are snapshot-class records",
                   classes.get("record.journal_begin") == "snapshot"
                   and classes.get("record.journal_update") == "snapshot"
                   and classes.get("record.journal_finish") == "snapshot",
                   "classes=%r" % classes)
    recorder.check("the recovery verbs record snapshot and irreversible respectively",
                   classes.get("record.recovery_restore") == "snapshot"
                   and classes.get("record.recovery_discard") == "irreversible",
                   "classes=%r" % classes)
    recorder.check("the read-only inspector left no record at all",
                   "record.recovery_get_state" not in classes,
                   "commands=%s" % sorted(classes))
    inverses = begin_inverses(records)
    recorder.check("a journal_begin inverse is dispatched as a COMMAND",
                   every_inverse_is_dispatched(inverses),
                   "inverses=%s" % inverses[-2:])


def check_irreversible_undo_refusal(session, recorder, take):
    """The discard has no inverse: control.undo fails typed and names the fallback."""
    begin(session, take)
    session.result("record.recovery_discard", {"take": take})
    refused = session.typed_error("control.undo")
    recorder.check("control.undo refuses after an irreversible discard",
                   refused.get("kind") == "irreversible"
                   and "record.recovery_discard" in str(refused.get("message", "")),
                   "kind=%r message=%r" % (refused.get("kind"), refused.get("message")))
    recorder.check("the refusal names the fallback: the take's WAV is kept",
                   "WAV" in str(refused.get("message", "")),
                   "message=%r" % refused.get("message"))
    recorder.check("the take the refusal names is still on disk", os.path.exists(take),
                   "take exists=%r" % os.path.exists(take))


def report_results(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-64s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)


def run_crashed_capture(argv, recorder, shared, transcript):
    """Instance A: journal a capture, prove it is visible, then die abnormally."""
    take = os.path.join(shared, "take-01.wav")
    write_take(take, TAKE_FRAMES)
    before_sha = sha256(take)

    instance = H.start_instance(argv[1], workingdir=shared)
    H.wait_for_socket(instance)
    client = H.connect(instance)
    H.wait_ready(instance, client, transcript)
    session = Session(client, transcript)
    print("instance A: %s (working dir %s)" % (instance.socket_path, shared))
    session.result("control.version")
    begin(session, take, track="trk-0")
    updated = session.result("record.journal_update",
                             {"take": take, "frames_on_disk": JOURNALLED_FRAMES})
    recorder.check("record.journal_update records the frames on disk",
                   updated.get("frames_journalled") == JOURNALLED_FRAMES
                   and updated.get("state") == "in_progress",
                   "result=%r" % updated)
    check_journal_before_the_crash(session, recorder, take)
    check_the_crash(instance, recorder)
    return take, before_sha


def check_transcript_is_evidence(transcript, recorder):
    """The transcript is the run's evidence, not a summary of it."""
    text = "\n".join(transcript.lines)
    recorder.check("the transcript records instance A's journalling verbatim",
                   "record.journal_begin" in text and "record.journal_update" in text,
                   "%d transcript lines" % len(transcript.lines))
    recorder.check("the transcript records the recovery requests verbatim",
                   "record.recovery_get_state" in text and "record.recovery_restore" in text,
                   "%d transcript lines" % len(transcript.lines))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    shared = tempfile.mkdtemp(prefix="zctl-rec-", dir="/tmp")
    transcript = H.Transcript()
    try:
        take, before_sha = run_crashed_capture(argv, recorder, shared, transcript)
        # Re-run the recovery against a fresh instance, and record ITS transcript.
        run_recovery_with_transcript(argv, recorder, shared, take, before_sha, transcript)
    finally:
        shutil.rmtree(shared, ignore_errors=True)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        recorder.problems.report("record.* crash-recovery control-surface transcript")
        return 1
    H.ok("record.* crash-recovery control-surface transcript (every check held)")
    return 0


def run_recovery_with_transcript(argv, recorder, shared, take, before_sha, transcript):
    instance = H.start_instance(argv[1], workingdir=shared)
    H.wait_for_socket(instance)
    client = H.connect(instance)
    H.wait_ready(instance, client, transcript)
    session = Session(client, transcript)
    print("instance B: %s (working dir %s)" % (instance.socket_path, shared))
    session.result("control.version")
    entry = check_recovery_after_the_crash(session, recorder, take)
    check_the_bound_as_observed(entry, recorder)
    check_restore(session, recorder, take, before_sha)
    check_discard(session, recorder, fresh_take(shared, "take-02.wav"))
    check_undo_takes_the_journal_back(session, recorder, fresh_take(shared, "take-03.wav"))
    check_clean_stop(session, recorder, fresh_take(shared, "take-04.wav"))
    check_refusals(session, recorder, fresh_take(shared, "take-05.wav"))
    check_irreversible_undo_refusal(session, recorder, fresh_take(shared, "take-06.wav"))
    check_transactions(session, recorder)
    check_transcript_is_evidence(transcript, recorder)
    reply = session.call("control.quit")
    if reply.get("ok") is True:
        session.client.close()
        instance.wait_for_exit(H.QUIT_TIMEOUT)
    else:
        recorder.check("control.quit answered", False, "%r" % reply)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
