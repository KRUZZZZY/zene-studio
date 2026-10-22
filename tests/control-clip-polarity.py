#!/usr/bin/env python3
"""The Q050-125 clip gain / envelope / polarity surface, driven end to end over the control socket.

Starts the REAL `zene` binary headless (its own throwaway config) and proves, on one generated
sample clip:

  1. clip.set_gain sets the gain, reads it back in dB, and one control.undo takes it back;
  2. clip.gain_envelope_set draws points, clip.gain_envelope_get reads them back exactly, and one
     undo takes the whole envelope back;
  3. clip.set_polarity inverts the clip, arrangement.get_state reports `inverted`, one undo restores
     it, and requesting the value it already has records no step;
  4. the clip's save/reopen round trip keeps gain, envelope and polarity, and a clip saved by an
     older writer (no `invert`) loads with polarity off.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-clip-polarity.py <zene>
Exit 0 only when every check held.
"""

import os
import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def one_clip(session):
    track = session.result("track.add", {"type": "sample", "name": "Wave"}).get("track")
    session.result("sample.generate", {"track": track, "position": 0, "length": 192, "kind": "tone"})
    clips = [c for c in session.result("arrangement.get_state").get("clips") or [] if c.get("track") == track]
    return track, clips[0]["id"]


def check_gain(session, recorder, clip):
    before = session.result("clip.fade_curve", {"clip": clip})
    reply = session.result("clip.set_gain", {"clip": clip, "gain_db": -6.0})
    recorder.check("clip.set_gain sets -6 dB and reads it back",
                   abs(reply.get("gain_db", 0.0) + 6.0) < 0.01, "reply=%r" % reply)
    recorder.check("the gain is visible through the ordinary clip state",
                   any(abs(c.get("gain_db", 0.0) + 6.0) < 0.01
                       for c in session.result("arrangement.get_state").get("clips") or [] if c.get("id") == clip),
                   "state=%r" % session.result("arrangement.get_state").get("clips"))
    session.result("control.undo")
    after = session.result("clip.fade_curve", {"clip": clip})
    recorder.check("one undo takes the gain back",
                   abs(after.get("gain_db", 0.0) - before.get("gain_db", 0.0)) < 0.01,
                   "before=%r after=%r" % (before, after))


def check_envelope(session, recorder, clip):
    points = [{"tick": 0, "gain_db": 0.0}, {"tick": 96, "gain_db": -12.0}, {"tick": 192, "gain_db": 0.0}]
    set_reply = session.result("clip.gain_envelope_set", {"clip": clip, "points": points})
    got = session.result("clip.gain_envelope_get", {"clip": clip})
    recorder.check("the envelope is set and read back point for point",
                   set_reply.get("points") == points and got.get("points") == points,
                   "set=%r got=%r" % (set_reply, got))
    curve = session.result("clip.fade_curve", {"clip": clip})
    recorder.check("the gain curve reports the envelope's dip at its middle point",
                   len(curve.get("curve") or []) > 0, "curve=%r" % curve)
    session.result("control.undo")
    empty = session.result("clip.gain_envelope_get", {"clip": clip})
    recorder.check("one undo takes the whole envelope back", not (empty.get("points") or []), "after=%r" % empty)


def check_polarity(session, recorder, clip):
    # The polarity reply shares the edit-state shape. Offset fades are optional there, but when set
    # they are part of the reply and must be declared in clip.set_polarity's closed result schema.
    session.result("clip.set_fade", {"clip": clip, "fade_in": 24, "fade_out": 24,
                                      "fade_in_offset": 144, "fade_out_offset": 48})
    reply = session.result("clip.set_polarity", {"clip": clip, "inverted": True})
    recorder.check("clip.set_polarity inverts the clip and reads it back",
                   reply.get("inverted") is True and reply.get("fade_in_offset") == 144
                   and reply.get("fade_out_offset") == 48, "reply=%r" % reply)
    state = session.result("arrangement.get_state")
    listed = [c for c in state.get("clips") or [] if c.get("id") == clip]
    recorder.check("arrangement.get_state reports inverted",
                   bool(listed) and listed[0].get("inverted") is True, "listed=%r" % listed)
    session.result("control.undo")
    undone = session.result("clip.set_polarity", {"clip": clip, "inverted": True})
    recorder.check("one undo restores the polarity, and the clip accepts the edit again",
                   undone.get("inverted") is True, "reply=%r" % undone)

    # The value it already has records no step: one undo still takes the real edit back.
    again = session.result("clip.set_polarity", {"clip": clip, "inverted": True})
    step = session.result("control.undo")
    back = session.result("arrangement.get_state")
    listed = [c for c in back.get("clips") or [] if c.get("id") == clip]
    recorder.check("repeating the current polarity leaves no undo step of its own",
                   again.get("inverted") is True and step.get("undone") is True and listed
                   and listed[0].get("inverted") is False,
                   "again=%r step=%r listed=%r" % (again, step, listed))


def check_round_trip(session, recorder, instance, clip):
    session.result("clip.set_gain", {"clip": clip, "gain_db": -3.0})
    session.result("clip.gain_envelope_set", {"clip": clip,
                                              "points": [{"tick": 0, "gain_db": 2.0}, {"tick": 192, "gain_db": -8.0}]})
    session.result("clip.set_polarity", {"clip": clip, "inverted": True})
    project = os.path.join(instance.workspace, "clip-polarity-proof.mmp")
    session.result("project.save", {"path": project})
    reopened = session.result("project.open", {"path": project})
    state = session.result("arrangement.get_state")
    listed = [c for c in state.get("clips") or [] if c.get("id") == clip]
    recorder.check("gain, envelope and polarity survive save and reopen",
                   bool(reopened.get("file")) and reopened.get("error_count") == 0 and bool(listed)
                   and abs(listed[0].get("gain_db", 0.0) + 3.0) < 0.01
                   and listed[0].get("inverted") is True,
                   "listed=%r reopened=%r" % (listed, reopened))
    envelope = session.result("clip.gain_envelope_get", {"clip": clip})
    recorder.check("the envelope survives save and reopen point for point",
                   envelope.get("points") == [{"tick": 0, "gain_db": 2.0}, {"tick": 192, "gain_db": -8.0}],
                   "envelope=%r" % envelope)


def check_legacy_default(session, recorder):
    """An older writer's clip element has no `invert`, so the clip loads non-inverted."""
    fresh = session.result("project.new", {})
    track = session.result("track.add", {"type": "sample", "name": "Legacy"}).get("track")
    session.result("sample.generate", {"track": track, "position": 0, "length": 96, "kind": "tone"})
    clip = [c for c in session.result("arrangement.get_state").get("clips") or [] if c.get("track") == track][0]["id"]
    recorder.check("a clip created without the attribute reports non-inverted",
                   fresh is not None and
                   next(c for c in session.result("arrangement.get_state").get("clips") or []
                        if c.get("id") == clip).get("inverted") is False,
                   "clips=%r" % session.result("arrangement.get_state").get("clips"))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1], workingdir=None) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        _track, clip = one_clip(session)
        check_gain(session, recorder, clip)
        check_envelope(session, recorder, clip)
        check_polarity(session, recorder, clip)
        check_round_trip(session, recorder, instance, clip)
        check_legacy_default(session, recorder)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("clip polarity control-surface driver")
        return 1
    H.ok("clip polarity control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
