#!/usr/bin/env python3
"""The 0.5.0 L05 audio editing surface, driven end to end over the control socket.

Starts the REAL `zene` binary headless (its own throwaway config) and proves:

  1. range.fade fades the sample clips a range touches - out on the clip whose end is inside, in on
     the one whose start is - and one control.undo takes both back;
  2. arrangement.modes takes auto_fade_ms and show_transients and answers them;
  3. clip.split_at cuts a clip at every beat, and one undo puts it back whole;
  4. clip.transients answers on a generated clip, and a fade can take the new s_curve shape;
  5. clip.slice_to_instrument makes a SlicerT track (or is refused requires without SlicerT), one undo removes it.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-audio-editing.py <zene>
Exit 0 only when every check held.
"""

import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def clips_on(session, track):
    state = session.result("arrangement.get_state")
    return [c for c in state.get("clips") or [] if c.get("track") == track]


def two_clips(session):
    track = session.result("track.add", {"type": "sample", "name": "Audio"}).get("track")
    for position in (0, 192):
        session.result("sample.generate", {"track": track, "position": position, "length": 192, "kind": "tone"})
    return track


def check_range_fade(session, recorder, track):
    session.result("range.set", {"start": 144, "end": 240, "tracks": [track]})
    reply = session.result("range.fade", {})
    clips = sorted(clips_on(session, track), key=lambda c: c.get("position"))
    recorder.check("range.fade fades out the first clip and in the second",
                   reply.get("faded") == 2 and len(clips) == 2 and clips[0].get("fade_out") == 48 and clips[1].get("fade_in") == 48,
                   "reply=%r clips=%r" % (reply, clips))
    session.result("control.undo")
    clips = clips_on(session, track)
    recorder.check("one undo takes both fades back", all(c.get("fade_in") == 0 and c.get("fade_out") == 0 for c in clips),
                   "clips=%r" % clips)
    session.result("range.clear")


def check_modes(session, recorder):
    reply = session.result("arrangement.modes", {"auto_fade_ms": 5, "show_transients": True})
    recorder.check("arrangement.modes takes the automatic fade and the transient marks",
                   reply.get("auto_fade_ms") == 5 and reply.get("show_transients") is True, "reply=%r" % reply)
    session.result("arrangement.modes", {"auto_fade_ms": 0, "show_transients": False})


def check_split_and_shape(session, recorder, track):
    first = sorted(clips_on(session, track), key=lambda c: c.get("position"))[0]["id"]
    reply = session.result("clip.split_at", {"clip": first, "at": "beats"})
    recorder.check("clip.split_at cuts the clip at every beat", reply.get("pieces") == 4 and len(clips_on(session, track)) == 5,
                   "reply=%r" % reply)
    session.result("control.undo")
    recorder.check("one undo puts it back whole", len(clips_on(session, track)) == 2, "clips=%r" % clips_on(session, track))
    found = session.call("clip.transients", {"clip": first})
    recorder.check("clip.transients answers", found.get("ok") and "transients" in (found.get("result") or {}), "reply=%r" % found)
    shaped = session.call("clip.set_fade", {"clip": first, "fade_in": 24, "fade_in_shape": "s_curve"})
    recorder.check("a fade takes the s_curve shape", shaped.get("ok"), "reply=%r" % shaped)


def check_slice(session, recorder, track):
    first = sorted(clips_on(session, track), key=lambda c: c.get("position"))[0]["id"]
    before = len(session.result("arrangement.get_state").get("tracks") or [])
    reply = session.call("clip.slice_to_instrument", {"clip": first, "at": "beats"})
    kind = (reply.get("error") or {}).get("kind")
    if kind == "requires":
        recorder.check("slicing is refused, typed, without SlicerT", True, "reply=%r" % reply)
        return
    after = len(session.result("arrangement.get_state").get("tracks") or [])
    recorder.check("clip.slice_to_instrument adds a SlicerT track", reply.get("ok") and after == before + 1, "reply=%r" % reply)
    session.result("control.undo")
    after = len(session.result("arrangement.get_state").get("tracks") or [])
    recorder.check("one undo removes the slice track", after == before, "tracks=%d before=%d" % (after, before))


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
        track = two_clips(session)
        check_range_fade(session, recorder, track)
        check_modes(session, recorder)
        check_split_and_shape(session, recorder, track)
        check_slice(session, recorder, track)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("audio-editing control-surface driver")
        return 1
    H.ok("audio-editing control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
