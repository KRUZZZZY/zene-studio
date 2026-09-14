#!/usr/bin/env python3
"""RAW control-surface transcript for the bounce.* / freeze.* command groups.

The acceptance evidence for freeze / bounce-in-place, produced by driving the
REAL `lmms` binary headless and printing every request and reply verbatim.

It builds an AUDIBLE fixture through the commands an agent has (an instrument
track that sounds, two one-bar clips, a note in each) and then, in order:

  1. bounces the whole track and a region of it - each file's frames and sha256
     checked against the bytes on disk, and each bounce's own level measured;
  2. requires the bounce to leave NO A16 transaction (not_mutating);
  3. freezes the track and requires the take to be reported, its audio LOADED
     and the source clips untouched;
  4. DELETES the source notes and requires the frozen session to still render
     audio within a tolerance of the unfrozen reference. This is what proves the
     take plays AND the source stopped: with the notes gone the source cannot
     make a sound, so silence here would mean the take never played. The notes
     are put back (control.undo) and read back before the round trip;
  5. saves the project, greps the file for the take's own attributes, reopens it
     and requires the freeze, its path and its audio to survive - and the
     reopened session to still render audio;
  6. unfreezes and requires the source back, within tolerance of the reference;
  7. freezes again and requires ONE control.undo to take it off through the
     Track's own checkpoint;
  8. freezes a region and requires it to mute exactly the clip that starts
     inside it (read back through arrangement.get_state) and to leave the clip
     outside it alone - then measures the REGION's bar of a render of that
     session: its source clip is muted, so only the take can make that sound;
  9. unfreezes and requires the mute restored, then drives every refusal
     (already frozen, not frozen, a bad range, a partial range, an unknown
     track) and reads the A16 records the verbs left.

The socket wrapper, the check recorder and the WAV measurements live in
tests/freeze_bounce_evidence.py (the split tests/link_sync_evidence.py made, and
for the same Gate 7 reason). The instance is started through the shared harness
(tests/control_socket_harness.py), so this file adds no second launch path.

Usage: QT_QPA_PLATFORM=offscreen python3 control-freeze-commands-transcript.py <lmms>
Exit code 0 only when every assertion held; 77 (ctest Skipped, never Passed) when
this build has no loadable instrument to make an audible fixture with.
"""

import hashlib
import os
import sys

import control_socket_harness as H
from freeze_bounce_evidence import (SILENT_DBFS, NoteTrace, Recorder, Session,
                                    load_audible_instrument, note_ids, render_measure,
                                    report_on_abort, report_results, require_one_note, wav_measure)

# The fixture is measured in ticks: 4/4, so 192 ticks to the bar.
CLIP_TICKS = 192          # one bar per clip
REGION_START = 192        # the second clip starts on bar 2
REGION_END = 384          # ... and ends on bar 3 (it is fully inside a region)
SONG_BARS = 2             # the two clips
RENDER_BARS = 3           # the song plus the bar Song::startExport appends
TAKE_TOLERANCE_DB = 6.0   # the take is re-sampled and carries the render's tail


def build_fixture(session, instance, transcript):
    """The audible fixture, built through the commands an agent has."""
    added = session.result("track.add", {"type": "instrument", "name": "Freeze Target"})
    track = added.get("track")
    if not track:
        H.fail("track.add returned no track id (%r)" % added, instance, transcript)
    if not load_audible_instrument(session, track):
        print("no audible instrument in this build: the audibility checks cannot run")
        print("kinds: %r" % session.result("plugin.list").get("counts_by_kind"))
        return None
    clips = []
    session.watch = NoteTrace(session, track, clips)   # every step, this run (see NoteTrace)
    for position in (0, REGION_START):
        clip = session.result("clip.add", {"track": track, "position": position,
                                           "length": CLIP_TICKS})
        if not clip.get("clip"):
            H.fail("clip.add at %d returned no clip id (%r)" % (position, clip), instance, transcript)
        session.result("note.add", {"clip": clip.get("clip"), "key": 60, "position": 0,
                                    "length": CLIP_TICKS // 2, "velocity": 120})
        clips.append(clip.get("clip"))
    return {"track": track, "clips": clips}


def clip_mutes(session, track, recorder, label):
    """The track's two clips and their mute flags, from arrangement.get_state."""
    state = session.result("arrangement.get_state")
    held = [c for c in (state.get("clips") or []) if c.get("track") == track]
    recorder.check("%s: arrangement.get_state lists the track's two clips" % label,
                   len(held) == 2, "clips=%s" % [c.get("id") for c in held])
    return sorted(bool(c.get("muted")) for c in held)


def check_bounce(session, fixture, recorder, outdir):
    """bounce.in_place writes real audio, and reports it honestly."""
    out = os.path.join(outdir, "bounce.wav")
    bounced = session.result("bounce.in_place", {"track": fixture["track"], "out": out})
    exists = os.path.exists(out)
    recorder.check("bounce.in_place returns the file it wrote",
                   bounced.get("path") == out and exists, "path=%r" % bounced.get("path"))
    if not exists:
        return None
    frames, measured = wav_measure(out)
    digest = hashlib.sha256(open(out, "rb").read()).hexdigest()
    recorder.check("bounce.in_place's sha256 is the file's sha256",
                   bounced.get("sha256") == digest,
                   "reported=%s measured=%s" % (bounced.get("sha256"), digest))
    recorder.check("bounce.in_place's frame count is the file's",
                   int(bounced.get("frames", -1)) == frames,
                   "reported=%r wav=%r" % (bounced.get("frames"), frames))
    recorder.check("the bounce is the track's AUDIO, not silence",
                   frames > 0 and measured > SILENT_DBFS,
                   "frames=%d dBFS=%.2f" % (frames, measured))
    recorder.check("the whole-track bounce covers tick 0",
                   int(bounced.get("start_ticks", -1)) == 0
                   and int(bounced.get("end_ticks", 0)) > 0,
                   "start=%r end=%r" % (bounced.get("start_ticks"), bounced.get("end_ticks")))
    return {"frames": frames, "dbfs": measured}


def check_bounce_region(session, fixture, recorder, outdir, whole):
    """A region bounce covers the ticks asked for, and only those."""
    out = os.path.join(outdir, "bounce-region.wav")
    bounced = session.result("bounce.in_place", {"track": fixture["track"], "out": out,
                                                 "start": REGION_START, "end": REGION_END})
    recorder.check("a region bounce reports the range it was given",
                   int(bounced.get("start_ticks", -1)) == REGION_START
                   and int(bounced.get("end_ticks", 0)) >= REGION_END,
                   "start=%r end=%r" % (bounced.get("start_ticks"), bounced.get("end_ticks")))
    if not os.path.exists(out):
        recorder.check("a region bounce writes its file", False, "no file at %s" % out)
        return
    frames, measured = wav_measure(out)
    recorder.check("a region bounce is shorter than the track and not empty",
                   0 < frames < (whole or {}).get("frames", 1 << 30),
                   "region=%d whole=%r" % (frames, (whole or {}).get("frames")))
    recorder.check("the region bounce carries audio", measured > SILENT_DBFS,
                   "dBFS=%.2f" % measured)


def check_bounce_is_not_mutating(session, recorder):
    """bounce.in_place records NO transaction: it writes an artefact, not state."""
    report = session.result("control.transactions")
    records = [r for r in (report.get("transactions") or [])
               if r.get("command") == "bounce.in_place"]
    recorder.check("bounce.in_place left no A16 transaction (not_mutating)",
                   records == [], "%d records" % len(records))


def check_freeze_track(session, fixture, recorder):
    """freeze.track installs the take and leaves the clips alone."""
    frozen = session.result("freeze.track", {"track": fixture["track"]})
    recorder.check("freeze.track reports the take with its audio loaded",
                   frozen.get("frozen") is True and frozen.get("audio_ready") is True
                   and str(frozen.get("audio", "")).endswith(".wav")
                   and int(frozen.get("frames", 0)) > 0,
                   "frozen=%r audio=%r frames=%r" % (frozen.get("frozen"), frozen.get("audio"),
                                                     frozen.get("frames")))
    recorder.check("a whole-track freeze mutes no clip",
                   int(frozen.get("muted_clips", -1)) == 0,
                   "muted_clips=%r" % frozen.get("muted_clips"))
    recorder.check("the take starts at tick 0 and ends past it",
                   int(frozen.get("start_ticks", -1)) == 0
                   and int(frozen.get("end_ticks", 0)) > 0,
                   "start=%r end=%r" % (frozen.get("start_ticks"), frozen.get("end_ticks")))
    state = session.result("track.get_state", {"track": fixture["track"]})
    recorder.check("track.get_state reports the frozen take",
                   state.get("frozen") is True and state.get("frozen_audio_ready") is True
                   and state.get("frozen_audio") == frozen.get("audio"),
                   "frozen=%r audio_ready=%r audio=%r" % (state.get("frozen"),
                                                          state.get("frozen_audio_ready"),
                                                          state.get("frozen_audio")))
    mutes = clip_mutes(session, fixture["track"], recorder, "freeze.track")
    recorder.check("every clip is unmuted after a whole-track freeze",
                   mutes == [False, False], "mutes=%s" % mutes)
    return frozen


def remove_source_notes(session, fixture, recorder):
    """Delete the fixture's notes: the source can no longer make a sound."""
    removed = 0
    for clip in fixture["clips"]:
        for note in note_ids(session, clip):
            reply = session.result("note.remove", {"clip": clip, "note": note})
            removed += 1 if reply.get("note_count") == 0 else 0
    recorder.check("the source notes were removed from both clips", removed == 2,
                   "removed=%d" % removed)
    return removed


def check_the_take_plays(session, recorder, outdir, reference):
    """THE PROOF: the frozen take plays where its source no longer can.

    The source notes are gone (no clip is muted, so the song's length is
    unchanged), so the ONLY thing that can make this render audible is the take.
    """
    measured, frames, rendered = render_measure(
        session, os.path.join(outdir, "frozen-session.wav"))
    recorder.check("a frozen session renders a file", rendered.get("path") is not None
                   and int(rendered.get("frames", 0)) > 0, "render=%r" % rendered)
    recorder.check("a frozen session is NOT silent with its source notes gone",
                   measured is not None and measured > SILENT_DBFS,
                   "frozen dBFS=%r reference dBFS=%r" % (measured, reference))
    recorder.check("the frozen render is within %.1f dB of the unfrozen one"
                   % TAKE_TOLERANCE_DB,
                   measured is not None and reference is not None
                   and abs(measured - reference) <= TAKE_TOLERANCE_DB,
                   "frozen=%r reference=%r" % (measured, reference))


def restore_source_notes(session, fixture, recorder):
    """Undo the two note removals and require the notes to be back."""
    for _ in fixture["clips"]:
        session.result("control.undo")
    counts = [len(note_ids(session, clip)) for clip in fixture["clips"]]
    recorder.check("the source notes come back off the journal", counts == [1, 1],
                   "notes=%s" % counts)


def check_survives_save_load(session, fixture, recorder, outdir):
    """The freeze is project state: save, reopen, and the take is still there."""
    saved_path = os.path.join(outdir, "frozen-take.mmp")
    saved = session.result("project.save", {"path": saved_path})
    recorder.check("project.save wrote the frozen session",
                   saved.get("file") == saved_path and os.path.exists(saved_path),
                   "file=%r" % saved.get("file"))
    with open(saved_path, errors="replace") as handle:
        text = handle.read()
    audio = str(fixture.get("take", "")).rsplit("/", 1)[-1]
    recorder.check("the saved project carries the take on the track's own element",
                   "frozenAudio=" in text and audio and audio in text,
                   "frozenAudio present=%r file=%r" % ("frozenAudio=" in text, audio))

    reopened = session.result("project.open", {"path": saved_path})
    recorder.check("project.open reloads the frozen session",
                   reopened.get("file") == saved_path, "file=%r" % reopened.get("file"))
    state = session.result("track.get_state", {"track": fixture["track"]})
    recorder.check("the freeze survives the round trip, audio and all",
                   state.get("frozen") is True and state.get("frozen_audio_ready") is True,
                   "frozen=%r audio_ready=%r" % (state.get("frozen"),
                                                 state.get("frozen_audio_ready")))
    measured, frames, rendered = render_measure(
        session, os.path.join(outdir, "reloaded-session.wav"))
    recorder.check("the reloaded session still plays the take",
                   rendered.get("path") is not None and measured is not None
                   and measured > SILENT_DBFS, "frames=%r dBFS=%r" % (frames, measured))


def check_unfreeze_restores(session, fixture, recorder, outdir, reference):
    """freeze.unfreeze puts the source back."""
    unfrozen = session.result("freeze.unfreeze", {"track": fixture["track"]})
    recorder.check("freeze.unfreeze drops the take",
                   unfrozen.get("frozen") is False and not unfrozen.get("audio")
                   and int(unfrozen.get("muted_clips", -1)) == 0,
                   "frozen=%r audio=%r muted_clips=%r" % (unfrozen.get("frozen"),
                                                          unfrozen.get("audio"),
                                                          unfrozen.get("muted_clips")))
    measured, frames, rendered = render_measure(
        session, os.path.join(outdir, "unfrozen-again.wav"))
    recorder.check("the source is audible again after unfreeze",
                   rendered.get("path") is not None and measured is not None
                   and reference is not None and abs(measured - reference) <= TAKE_TOLERANCE_DB,
                   "dBFS=%r reference=%r" % (measured, reference))


def check_undo(session, fixture, recorder, outdir):
    """SPEC A16: one control.undo takes the freeze off through the checkpoint."""
    session.result("freeze.track", {"track": fixture["track"]})
    before = session.result("track.get_state", {"track": fixture["track"]})
    undone = session.result("control.undo")
    after = session.result("track.get_state", {"track": fixture["track"]})
    recorder.check("freeze.track really froze the track before the undo",
                   before.get("frozen") is True, "frozen=%r" % before.get("frozen"))
    recorder.check("one control.undo takes the freeze off",
                   undone.get("undone") is True and after.get("frozen") is False,
                   "undone=%r frozen=%r" % (undone.get("undone"), after.get("frozen")))
    measured, frames, rendered = render_measure(session, os.path.join(outdir, "undone.wav"))
    recorder.check("the undone session renders its source again",
                   rendered.get("path") is not None and measured is not None
                   and measured > SILENT_DBFS, "dBFS=%r" % measured)


def check_region_freeze(session, fixture, recorder, outdir):
    """freeze.region mutes exactly the clips inside it, and its take still sounds."""
    frozen = session.result("freeze.region", {"track": fixture["track"],
                                              "start": REGION_START, "end": REGION_END})
    recorder.check("freeze.region reports the range it covered",
                   frozen.get("frozen") is True
                   and int(frozen.get("start_ticks", -1)) == REGION_START
                   and int(frozen.get("end_ticks", 0)) >= REGION_END,
                   "frozen=%r start=%r end=%r" % (frozen.get("frozen"), frozen.get("start_ticks"),
                                                  frozen.get("end_ticks")))
    recorder.check("freeze.region mutes the clip that starts inside it",
                   int(frozen.get("muted_clips", 0)) == 1,
                   "muted_clips=%r overlapping=%r" % (frozen.get("muted_clips"),
                                                      frozen.get("overlapping_clips")))
    recorder.check("freeze.region reports no overlap for a contained clip",
                   frozen.get("overlapping_clips") == [],
                   "overlapping=%r" % frozen.get("overlapping_clips"))
    mutes = clip_mutes(session, fixture["track"], recorder, "freeze.region")
    recorder.check("the clip inside the region is the muted one", mutes == [False, True],
                   "mutes=%s" % mutes)

    # The region's own source clip is muted, so only the take can make the
    # region's bar of this render audible.
    region_db, frames, rendered = render_measure(
        session, os.path.join(outdir, "region-frozen.wav"),
        bars=RENDER_BARS, first_bar=1, last_bar=2)
    recorder.check("the frozen region's bar still sounds with its source muted",
                   rendered.get("path") is not None and region_db is not None
                   and region_db > SILENT_DBFS,
                   "bar2=%r frames=%r" % (region_db, frames))

    unfrozen = session.result("freeze.unfreeze", {"track": fixture["track"]})
    restored = clip_mutes(session, fixture["track"], recorder, "unfreeze-region")
    recorder.check("freeze.unfreeze unmutes exactly the clip the region froze",
                   unfrozen.get("frozen") is False and restored == [False, False],
                   "frozen=%r mutes=%s" % (unfrozen.get("frozen"), restored))


def check_refusals(session, fixture, recorder):
    """Every refusal is typed, and a refusal writes nothing."""
    frozen = session.result("freeze.track", {"track": fixture["track"]})
    recorder.check("the refusal fixture froze the track", frozen.get("frozen") is True,
                   "%r" % frozen.get("frozen"))
    twice = session.typed_error("freeze.track", {"track": fixture["track"]})
    recorder.check("freezing an already-frozen track is refused", twice.get("kind") == "refused",
                   "kind=%r message=%r" % (twice.get("kind"), twice.get("message")))
    session.result("freeze.unfreeze", {"track": fixture["track"]})
    twice = session.typed_error("freeze.unfreeze", {"track": fixture["track"]})
    recorder.check("unfreezing a track that is not frozen is refused",
                   twice.get("kind") == "refused",
                   "kind=%r message=%r" % (twice.get("kind"), twice.get("message")))
    inverted = session.typed_error("freeze.region", {"track": fixture["track"],
                                                     "start": REGION_END, "end": REGION_START})
    recorder.check("a region whose end is not past its start is invalid_args",
                   inverted.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (inverted.get("kind"), inverted.get("message")))
    partial = session.typed_error("freeze.region", {"track": fixture["track"], "start": 0})
    recorder.check("a region missing its end is invalid_args",
                   partial.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (partial.get("kind"), partial.get("message")))
    half = session.typed_error("bounce.in_place", {"track": fixture["track"], "start": 0})
    recorder.check("a range missing its end is invalid_args", half.get("kind") == "invalid_args",
                   "kind=%r message=%r" % (half.get("kind"), half.get("message")))
    for command in ("bounce.in_place", "freeze.track", "freeze.unfreeze"):
        unknown = session.typed_error(command, {"track": "trk-999999"})
        recorder.check("%s: an unknown track id is not_found" % command,
                       unknown.get("kind") == "not_found",
                       "kind=%r message=%r" % (unknown.get("kind"), unknown.get("message")))
    unknown = session.typed_error("freeze.region", {"track": "trk-999999", "start": 0, "end": 1})
    recorder.check("freeze.region: an unknown track id is not_found",
                   unknown.get("kind") == "not_found",
                   "kind=%r message=%r" % (unknown.get("kind"), unknown.get("message")))
    state = session.result("track.get_state", {"track": fixture["track"]})
    recorder.check("a refused freeze left the track unfrozen", state.get("frozen") is False,
                   "frozen=%r" % state.get("frozen"))


def freeze_records(session):
    """The freeze.* records control.transactions reports, in the order it gives them."""
    report = session.result("control.transactions")
    return [r for r in (report.get("transactions") or [])
            if str(r.get("command", "")).startswith("freeze.")]


def everyRecordIsReversible(records):
    """True when every record both classifies itself reversibly and says so."""
    return all(r.get("reversible") is True for r in records)


def check_freeze_undo_restores_clip_edits(session, fixture, recorder):
    """ONE control.undo per edit, across the freeze's OWN undo.

    Undoing a freeze re-loads the Track, and Track::loadTrack deletes every clip
    and re-creates it from the saved XML. A re-created clip that does not get back
    the journal id its checkpoint names leaves the next control.undo with nothing
    to restore - and ProjectJournal::undo() then unwinds an OLDER step instead, so
    ONE undo takes back an edit nobody asked about (measured before the fix: the
    clip edit's undo cost a whole clip, depth 7 -> 4).
    """
    clip = fixture["clips"][0]
    notes = require_one_note(session, clip)      # never notes[0] on a lost note
    removed = session.result("note.remove", {"clip": clip, "note": notes[0]})
    recorder.check("the freeze-undo fixture removed one clip's note",
                   len(notes) == 1 and removed.get("note_count") == 0,
                   "notes=%s note_count=%r" % (notes, removed.get("note_count")))
    frozen = session.result("freeze.track", {"track": fixture["track"]})
    recorder.check("the freeze-undo fixture froze the track", frozen.get("frozen") is True,
                   "frozen=%r" % frozen.get("frozen"))
    session.result("control.undo")                       # the freeze's own undo
    state = session.result("track.get_state", {"track": fixture["track"]})
    recorder.check("the freeze-undo fixture took the freeze off first",
                   state.get("frozen") is False, "frozen=%r" % state.get("frozen"))
    before = session.result("control.undo_depth").get("depth")
    session.result("control.undo")                       # the clip edit's undo
    counts = [len(note_ids(session, c)) for c in fixture["clips"]]
    after = session.result("control.undo_depth").get("depth")
    recorder.check("a freeze's own undo does not orphan the clip edits made before it",
                   counts == [1, 1], "notes=%s depth %r -> %r" % (counts, before, after))
    # The sharper half: `depth` was read with the clip edit on top of the stack, so
    # a correct journal drops it by exactly one. Before the fix this call dropped it
    # by two - it had skipped the orphaned clip step and unwound an OLDER edit.
    recorder.check("ONE control.undo unwound ONE step, not several",
                   isinstance(before, int) and isinstance(after, int) and before - after == 1,
                   "notes=%s depth %r -> %r (one undo must drop the depth by one)"
                   % (counts, before, after))
    clip_mutes(session, fixture["track"], recorder, "freeze-undo")


def check_transactions(session, recorder):
    """The freeze verbs are true_inverse records; the bounce is not a record."""
    records = freeze_records(session)
    classes = [r.get("class") for r in records]
    ops = [((r.get("inverse") or {}).get("op")) for r in records]
    recorder.check("every freeze.* call left an A16 record", len(records) >= 4,
                   "%d records" % len(records))
    recorder.check("every freeze.* record is true_inverse and reversible",
                   bool(records) and classes == ["true_inverse"] * len(classes)
                   and everyRecordIsReversible(records),
                   "classes=%s" % classes)
    recorder.check("a freeze record's inverse names freeze.unfreeze or freeze.track",
                   "freeze.unfreeze" in ops or "freeze.track" in ops, "ops=%s" % ops)


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def run_checks(session, instance, recorder, transcript):
    """Every check, in the order the docstring lists them."""
    fixture = build_fixture(session, instance, transcript)
    if fixture is None:
        return None
    outdir = instance.tmp
    reference, frames, rendered = render_measure(session, os.path.join(outdir, "reference.wav"))
    print("reference render: %s frames, %s dBFS (from %r)" % (frames, reference,
                                                              rendered.get("path")))
    whole = check_bounce(session, fixture, recorder, outdir)
    check_bounce_region(session, fixture, recorder, outdir, whole)
    check_bounce_is_not_mutating(session, recorder)
    fixture["take"] = check_freeze_track(session, fixture, recorder).get("audio")
    remove_source_notes(session, fixture, recorder)
    check_the_take_plays(session, recorder, outdir, reference)
    restore_source_notes(session, fixture, recorder)
    check_survives_save_load(session, fixture, recorder, outdir)
    check_unfreeze_restores(session, fixture, recorder, outdir, reference)
    check_undo(session, fixture, recorder, outdir)
    check_region_freeze(session, fixture, recorder, outdir)
    check_refusals(session, fixture, recorder)
    check_freeze_undo_restores_clip_edits(session, fixture, recorder)
    check_transactions(session, recorder)
    check_quit(session, instance, recorder)
    return fixture


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    fixture = None
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        session.result("control.version")
        try:
            fixture = run_checks(session, instance, recorder, transcript)
        except BaseException:
            report_on_abort(recorder, transcript, instance)
            raise

    report_results(recorder)
    transcript.dump()
    if fixture is None:
        H.ok("no audible instrument in this build: Skipped, never Passed")
        return 77
    if recorder.problems:
        print("")
        recorder.problems.report("bounce.*/freeze.* control-surface transcript")
        return 1
    H.ok("bounce.*/freeze.* control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
