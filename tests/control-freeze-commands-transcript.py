#!/usr/bin/env python3
"""RAW control-surface transcript for the bounce.* / freeze.* command groups.

The acceptance evidence for freeze / bounce-in-place, produced by driving the
REAL `lmms` binary headless and printing every request and reply verbatim.

What it drives, in order:

  1. `track.add` + `plugin.load` + `clip.add` x2 + `note.add`   an AUDIBLE
     fixture (two clips on an instrument track), so a render has something to
     measure;
  2. `render.render`                   the reference, in dBFS, measured;
  3. `bounce.in_place`                  frames and sha256 checked against the
     file on disk, and the bounced audio itself measured (a bounce that produced
     silence fails here); with start/end ticks, a shorter region bounce;
  4. `control.transactions`             the bounce records NO transaction (the
     SPEC A16 not_mutating claim);
  5. `freeze.track`                     the take is reported, its audio is
     LOADED, and the clips are untouched (`muted_clips` == 0);
  6. `render.render` (frozen)           THE PROOF: the frozen session still
     renders audio within a tolerance of the reference. Silence here means the
     source stopped and the take never played;
  7. `project.save` + `project.open`    the freeze survives the round trip -
     state, path and audio - and renders audio again after the reload;
  8. `freeze.unfreeze` + `render.render`  the source comes back, within
     tolerance of the reference again;
  9. `freeze.track` + `control.undo`    SPEC A16: the freeze comes back off the
     Track's own checkpoint;
 10. `freeze.region`                    a region covering the second clip mutes
     exactly that clip (read back through `arrangement.get_state`), records it,
     and `freeze.unfreeze` unmutes exactly that clip;
 11. refusals                           already frozen, not frozen, a bad range,
     a partial range, an unknown track - every one typed;
 12. `control.transactions`             the A16 records of the verbs.

Started through the shared harness (tests/control_socket_harness.py), so this
file adds no second launch path.

Usage: QT_QPA_PLATFORM=offscreen python3 control-freeze-commands-transcript.py <lmms>
Exit code 0 only when every assertion held; 77 (ctest Skipped, never Passed) when
this build has no loadable instrument to make an audible fixture with.
"""

import array
import hashlib
import math
import os
import sys
import wave

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 1000))

# The fixture is measured in ticks: 4/4, so 192 ticks to the bar.
CLIP_TICKS = 192          # one bar per clip
REGION_START = 192        # the second clip starts on bar 2
REGION_END = 384          # ... and ends on bar 3 (it is fully inside a region)
SILENT_DBFS = -60.0       # below this, "the render produced no audio"
TAKE_TOLERANCE_DB = 6.0   # the take is re-sampled and carries the render's tail


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        return self.client.call(next(REQUEST_IDS), command, args, transcript=self.transcript)

    def result(self, command, args=None):
        """The reply, or {'error': ...} so a failed call is visible in a check."""
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


def wav_measure(path):
    """The file's own frame count and RMS in dBFS, measured not asserted."""
    with wave.open(path, "rb") as handle:
        width = handle.getsampwidth()
        frames = handle.getnframes()
        raw = handle.readframes(frames)
    if width == 2:
        samples, scale = array.array("h"), 32768.0
    elif width == 4:
        samples, scale = array.array("f"), 1.0
    else:
        raise AssertionError("unsupported WAV sample width %d" % width)
    samples.frombytes(raw)
    if not len(samples):
        return frames, float("-inf")
    total = 0.0
    for value in samples:
        total += float(value) * float(value)
    return frames, 20.0 * math.log10(math.sqrt(total / len(samples)) / scale + 1e-20)


def render_dbfs(session, path):
    """render.render the live session and measure what it wrote."""
    rendered = session.result("render.render", {"out": path, "format": "wav"})
    if not rendered.get("path"):
        return None, rendered
    return wav_measure(path)[1], rendered


# ---------------------------------------------------------------------------
# the fixture
# ---------------------------------------------------------------------------

def load_instrument(session, track):
    """Load the first loadable instrument the catalogue offers onto `track`.

    None when this build offers no instrument that instantiates - then the
    fixture could not be made audible and the run is Skipped rather than
    quietly passing on silence.
    """
    catalogue = session.result("plugin.list", {"kind": "instrument", "loadable_only": True})
    for device in (catalogue.get("devices") or [])[:5]:
        loaded = session.result("plugin.load", {"target": track, "device": device.get("id")})
        if loaded.get("plugin"):
            print("instrument: %s (%s)" % (device.get("name"), device.get("format")))
            return device.get("id")
    return None


def build_fixture(session, instance, transcript):
    """The audible fixture, built through the commands an agent has."""
    added = session.result("track.add", {"type": "instrument", "name": "Freeze Target"})
    track = added.get("track")
    if not track:
        H.fail("track.add returned no track id (%r)" % added, instance, transcript)
    if not load_instrument(session, track):
        print("no loadable instrument in this build: the audibility checks cannot run")
        print("kinds: %r" % session.result("plugin.list").get("counts_by_kind"))
        return None
    clips = []
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
    """The track's clips and their mute flags, from arrangement.get_state."""
    state = session.result("arrangement.get_state")
    held = [c for c in (state.get("clips") or []) if c.get("track") == track]
    recorder.check("%s: arrangement.get_state lists the track's two clips" % label,
                   len(held) == 2, "clips=%s" % [c.get("id") for c in held])
    return sorted(bool(c.get("muted")) for c in held)


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------

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


def check_the_take_plays(session, recorder, outdir, reference):
    """THE PROOF: the frozen session still renders audio, close to the reference."""
    measured, rendered = render_dbfs(session, os.path.join(outdir, "frozen-session.wav"))
    recorder.check("a frozen session renders a file", rendered.get("path") is not None
                   and int(rendered.get("frames", 0)) > 0, "render=%r" % rendered)
    recorder.check("a frozen session is NOT silent: the take played",
                   measured is not None and measured > SILENT_DBFS,
                   "frozen dBFS=%r reference dBFS=%r" % (measured, reference))
    recorder.check("the frozen render is within %.1f dB of the unfrozen one" % TAKE_TOLERANCE_DB,
                   measured is not None and reference is not None
                   and abs(measured - reference) <= TAKE_TOLERANCE_DB,
                   "frozen=%r reference=%r" % (measured, reference))


def check_survives_save_load(session, fixture, recorder, outdir):
    """The freeze is project state: save, reopen, and the take is still there."""
    saved_path = os.path.join(outdir, "frozen-take.mmp")
    saved = session.result("project.save", {"path": saved_path})
    recorder.check("project.save wrote the frozen session",
                   saved.get("file") == saved_path and os.path.exists(saved_path),
                   "file=%r" % saved.get("file"))
    with open(saved_path, errors="replace") as handle:
        text = handle.read()
    recorder.check("the saved project carries the take in the track's own XML",
                   "<frozen" in text and 'metadata="1"' in text,
                   "frozen element present=%r" % ("<frozen" in text))

    reopened = session.result("project.open", {"path": saved_path})
    recorder.check("project.open reloads the frozen session",
                   reopened.get("file") == saved_path, "file=%r" % reopened.get("file"))
    state = session.result("track.get_state", {"track": fixture["track"]})
    recorder.check("the freeze survives the round trip, audio and all",
                   state.get("frozen") is True and state.get("frozen_audio_ready") is True,
                   "frozen=%r audio_ready=%r" % (state.get("frozen"),
                                                 state.get("frozen_audio_ready")))
    measured, rendered = render_dbfs(session, os.path.join(outdir, "reloaded-session.wav"))
    recorder.check("the reloaded session still plays the take",
                   rendered.get("path") is not None and measured is not None
                   and measured > SILENT_DBFS, "dBFS=%r" % measured)


def check_unfreeze_restores(session, fixture, recorder, outdir, reference):
    """freeze.unfreeze puts the source back."""
    unfrozen = session.result("freeze.unfreeze", {"track": fixture["track"]})
    recorder.check("freeze.unfreeze drops the take",
                   unfrozen.get("frozen") is False and not unfrozen.get("audio")
                   and int(unfrozen.get("muted_clips", -1)) == 0,
                   "frozen=%r audio=%r muted_clips=%r" % (unfrozen.get("frozen"),
                                                          unfrozen.get("audio"),
                                                          unfrozen.get("muted_clips")))
    measured, rendered = render_dbfs(session, os.path.join(outdir, "unfrozen-again.wav"))
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
    measured, rendered = render_dbfs(session, os.path.join(outdir, "undone.wav"))
    recorder.check("the undone session renders its source again",
                   rendered.get("path") is not None and measured is not None
                   and measured > SILENT_DBFS, "dBFS=%r" % measured)


def check_region_freeze(session, fixture, recorder):
    """freeze.region mutes exactly the clips inside it and no others."""
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
    for command in ("bounce.in_place", "freeze.track", "freeze.region", "freeze.unfreeze"):
        unknown = session.typed_error(command, {"track": "trk-999999", "start": 0, "end": 1})
        recorder.check("%s: an unknown track id is not_found" % command,
                       unknown.get("kind") == "not_found",
                       "kind=%r message=%r" % (unknown.get("kind"), unknown.get("message")))
    state = session.result("track.get_state", {"track": fixture["track"]})
    recorder.check("a refused freeze left the track unfrozen", state.get("frozen") is False,
                   "frozen=%r" % state.get("frozen"))


def inverse_ops(records):
    return [((r.get("inverse") or {}).get("op")) for r in records]


def check_transactions(session, recorder):
    """The freeze verbs are true_inverse records; the bounce is not a record."""
    report = session.result("control.transactions")
    records = [r for r in (report.get("transactions") or [])
               if str(r.get("command", "")).startswith("freeze.")]
    classes = [r.get("class") for r in records]
    recorder.check("every freeze.* call left an A16 record", len(records) >= 4,
                   "%d records" % len(records))
    recorder.check("every freeze.* record is true_inverse and reversible",
                   bool(records) and classes == ["true_inverse"] * len(classes)
                   and all(r.get("reversible") is True for r in records),
                   "classes=%s" % classes)
    ops = inverse_ops(records)
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


def report_results(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-58s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
            recorder.problems.add("%s (%s)" % (name, evidence))


def run_checks(session, instance, recorder, transcript):
    """Every check, in the order the docstring lists them."""
    fixture = build_fixture(session, instance, transcript)
    if fixture is None:
        return None
    outdir = instance.tmp
    reference, _ = render_dbfs(session, os.path.join(outdir, "reference.wav"))
    print("reference render: %s dBFS" % reference)
    whole = check_bounce(session, fixture, recorder, outdir)
    check_bounce_region(session, fixture, recorder, outdir, whole)
    check_bounce_is_not_mutating(session, recorder)
    check_freeze_track(session, fixture, recorder)
    check_the_take_plays(session, recorder, outdir, reference)
    check_survives_save_load(session, fixture, recorder, outdir)
    check_unfreeze_restores(session, fixture, recorder, outdir, reference)
    check_undo(session, fixture, recorder, outdir)
    check_region_freeze(session, fixture, recorder)
    check_refusals(session, fixture, recorder)
    check_transactions(session, recorder)
    check_quit(session, instance, recorder)
    return fixture


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)
        session.result("control.version")
        fixture = run_checks(session, instance, recorder, transcript)

    report_results(recorder)
    transcript.dump()
    if fixture is None:
        H.ok("no loadable instrument in this build: Skipped, never Passed")
        return 77
    if recorder.problems:
        print("")
        recorder.problems.report("bounce.*/freeze.* control-surface transcript")
        return 1
    H.ok("bounce.*/freeze.* control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
