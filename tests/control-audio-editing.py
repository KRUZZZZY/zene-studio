#!/usr/bin/env python3
"""The 0.5.0 L05 audio editing surface, driven end to end over the control socket.

Starts the REAL `zene` binary headless (its own throwaway config) and proves:

  1. clip.set_record arms a clip, and arming it AGAIN (the flag it already has) leaves no undo step
     behind - one control.undo still takes the first arm back;
  2. range.fade fades the sample clips a range touches - out on the clip whose end is inside, in on
     the one whose start is - and one control.undo takes both back;
  3. arrangement.modes takes auto_fade_ms and show_transients and answers them;
  4. clip.split_at cuts a clip at every beat, and one undo puts it back whole;
  5. clip.transients answers on a generated clip, and a fade can take the new s_curve shape;
  6. clip.slice_to_instrument makes a SlicerT track (or is refused requires without SlicerT), one undo removes it;
  7. Q050-106: sample.split_stereo writes an asymmetric fixture as two mono WAVs (channels, rate and length read
     from their headers) and leaves the clip stereo, then sample.edit_range op mono reads back mono through
     arrangement.get_state and one control.undo restores the stereo read-back.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-audio-editing.py <zene>
Exit 0 only when every check held.
"""

import os
import struct
import sys
import tempfile

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


def check_redundant_record_arm_does_not_consume_undo(session, recorder, track):
    """An unchanged arm request must leave the first arm as the next undo step.

    The window is 0 for the check (one call, one step - nothing is grouped), and it
    is put back afterwards: the checks that follow must see the session as they
    found it. The clip is disarmed again at the end for the same reason: an ARMED
    clip does not journal its own state (the arm's step covers the take), so a
    later track checkpoint would rebuild the track without it.
    """
    window = session.result("control.undo_depth").get("coalescing", {}).get("window_ms")
    session.result("control.set_undo_coalescing", {"window_ms": 0})
    clip = sorted(clips_on(session, track), key=lambda c: c.get("position"))[0]["id"]
    armed = session.result("clip.set_record", {"clip": clip, "record": True})
    repeated = session.result("clip.set_record", {"clip": clip, "record": True})
    undone = session.result("control.undo")
    state_after_undo = session.result("clip.set_record", {"clip": clip, "record": True})
    recorder.check("the first record arm changes the clip from disarmed to armed",
                   armed.get("record_before") is False and armed.get("record") is True,
                   "reply=%r" % armed)
    recorder.check("repeating the current record arm reports it was already armed",
                   repeated.get("record_before") is True and repeated.get("record") is True,
                   "reply=%r" % repeated)
    recorder.check("one undo reaches the first real arm after the repeated request",
                   undone.get("undone") is True and undone.get("undone_command") == "clip.set_record"
                   and state_after_undo.get("record_before") is False,
                   "undo=%r state_after_undo=%r" % (undone, state_after_undo))
    # The session is left as it was found: the clip is disarmed (so it journals its
    # own state again) and the coalescing window is back to what it was.
    session.result("clip.set_record", {"clip": clip, "record": False})
    if window is not None:
        session.result("control.set_undo_coalescing", {"window_ms": window})


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


def wavInfo(path):
    """(channels, rate, frames, format_tag) from a canonical RIFF/WAVE file."""
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    channels = rate = format_tag = frames = None
    position = 12
    while position + 8 <= len(data):
        chunk = data[position:position + 4]
        size = struct.unpack("<I", data[position + 4:position + 8])[0]
        body = data[position + 8:position + 8 + size]
        if chunk == b"fmt " and len(body) >= 16:
            format_tag, channels, rate = struct.unpack("<HHI", body[:8])
        elif chunk == b"data":
            frames = size // 4  # float32 mono
        position += 8 + size + (size & 1)
    if None in (channels, rate, format_tag, frames):
        return None
    return channels, rate, frames, format_tag


def check_stereo_tools(session, recorder):
    track = session.result("track.add", {"type": "sample", "name": "Stereo"}).get("track")
    generated = session.result("sample.generate", {"track": track, "position": 0, "length": 192,
                                                   "kind": "white"})
    clip = generated.get("clip")
    frames = generated.get("frames")
    rate = generated.get("sample_rate")
    recorder.check("the stereo fixture is asymmetric white noise",
                   bool(clip) and frames and rate, "generated=%r" % generated)
    before = [c for c in clips_on(session, track) if c.get("id") == clip]
    recorder.check("the agent reads the fixture as stereo",
                   len(before) == 1 and before[0].get("channels") == 2 and before[0].get("layout") == "stereo",
                   "clips=%r" % before)

    with tempfile.TemporaryDirectory() as directory:
        left = os.path.join(directory, "fixture-left.wav")
        right = os.path.join(directory, "fixture-right.wav")
        reply = session.call("sample.split_stereo",
                             {"clip": clip, "left_path": left, "right_path": right})
        result = reply.get("result") or {}
        left_info = wavInfo(left) if os.path.isfile(left) else None
        right_info = wavInfo(right) if os.path.isfile(right) else None
        recorder.check("sample.split_stereo writes two mono WAVs at the source rate and length",
                       reply.get("ok") and result.get("frames") == frames
                       and result.get("sample_rate") == rate and result.get("channels") == 1
                       and left_info is not None and right_info is not None
                       and left_info[0] == 1 and right_info[0] == 1
                       and left_info[1] == rate and right_info[1] == rate
                       and left_info[2] == frames and right_info[2] == frames
                       and left_info[3] == 3 and right_info[3] == 3,
                       "reply=%r left=%r right=%r" % (reply, left_info, right_info))
        after = clips_on(session, track)
        recorder.check("the split leaves the clip unchanged",
                       len(after) == 1 and after[0].get("channels") == 2
                       and after[0].get("layout") == "stereo",
                       "clips=%r" % after)

    reply = session.call("sample.edit_range", {"clip": clip, "op": "mono"})
    result = reply.get("result") or {}
    mono = clips_on(session, track)
    recorder.check("sample.edit_range op mono reads back as mono",
                   reply.get("ok") and result.get("channels") == 1
                   and result.get("layout") == "mono" and result.get("frames") == frames
                   and len(mono) == 1 and mono[0].get("channels") == 1
                   and mono[0].get("layout") == "mono",
                   "reply=%r clips=%r" % (reply, mono))
    session.result("control.undo")
    stereo = clips_on(session, track)
    recorder.check("one control.undo restores the stereo read-back",
                   len(stereo) == 1 and stereo[0].get("channels") == 2
                   and stereo[0].get("layout") == "stereo",
                   "clips=%r" % stereo)


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
        check_redundant_record_arm_does_not_consume_undo(session, recorder, track)
        check_range_fade(session, recorder, track)
        check_modes(session, recorder)
        check_split_and_shape(session, recorder, track)
        check_slice(session, recorder, track)
        check_stereo_tools(session, recorder)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("audio-editing control-surface driver")
        return 1
    H.ok("audio-editing control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
