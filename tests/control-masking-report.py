#!/usr/bin/env python3
"""Q050-231.1 over the control socket: mixer.masking_report on the REAL `zene` binary, headless.

Two new mixer channels each get a sample track with generated tones (sample.generate): channel A
200 Hz and 3 kHz, channel B 200 Hz and 800 Hz. With A at front and B at back the driver proves:

  1. the report names the pair (A, B), a range holding 200 Hz, and B as the cut's target, with the
     mixer.channel_eq arguments for it - and no clash at 800 Hz or 3 kHz (only one channel holds those);
  2. three repeated reports are byte-identical;
  3. swapping the priorities moves the target to A;
  4. reporting leaves every channel's strip EQ and control.undo_depth exactly as they were.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-masking-report.py <zene>
Exit 0 only when every check held.
"""

import json
import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def tone_channel(session, name, tones):
    channel = session.result("mixer.add_channel", {}).get("channel")
    track = session.result("track.add", {"type": "sample", "name": name}).get("track")
    session.result("track.set_mixer_channel", {"track": track, "channel": channel})
    for i, hz in enumerate(tones):
        session.result("sample.generate", {"track": track, "position": 192 * i, "length": 192, "kind": "tone",
                                           "frequency": hz, "amplitude": 0.5})
    return channel


def clash_at(report, a, b, hz):
    for c in report.get("clashes") or []:
        if c.get("a") == a and c.get("b") == b and c.get("low_hz", 1e9) <= hz < c.get("high_hz", 0):
            return c
    return None


def eq_state(session):
    channels = [c.get("channel") for c in session.result("routing.matrix").get("channels") or []]
    return {ch: session.result("mixer.channel_eq", {"channel": ch}) for ch in channels}


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
        a = tone_channel(session, "Lead", [200.0, 3000.0])
        b = tone_channel(session, "Pad", [200.0, 800.0])
        args = {"channels": [b, a], "priorities": {a: "front", b: "back"}}
        eq_before = eq_state(session)
        depth_before = session.result("control.undo_depth").get("depth")

        first = session.result("mixer.masking_report", args)
        low = clash_at(first, a, b, 200.0)
        recorder.check("a clash names the pair and a range holding 200 Hz, cut on the back channel",
                       low is not None and low.get("target") == b and low.get("protect") == a
                       and -6.0 <= low.get("cut_db", 0.0) <= -1.0
                       and (low.get("strip_eq") or {}).get("channel") == b, "report=%r" % first)
        recorder.check("no clash where only one channel holds the tone (800 Hz, 3 kHz)",
                       clash_at(first, a, b, 800.0) is None and clash_at(first, a, b, 3000.0) is None,
                       "report=%r" % first)
        repeats = [session.result("mixer.masking_report", args) for _ in range(3)]
        recorder.check("three repeated reports are identical",
                       all(json.dumps(r, sort_keys=True) == json.dumps(first, sort_keys=True) for r in repeats),
                       "first=%r repeats=%r" % (first, repeats))
        swapped = session.result("mixer.masking_report", {"channels": [a, b], "priorities": {a: "back", b: "front"}})
        recorder.check("swapping the priorities moves the cut to the other channel",
                       (clash_at(swapped, a, b, 200.0) or {}).get("target") == a, "swapped=%r" % swapped)
        recorder.check("reporting leaves every strip EQ as it was", eq_state(session) == eq_before,
                       "before=%r" % eq_before)
        recorder.check("reporting records no undo step",
                       session.result("control.undo_depth").get("depth") == depth_before,
                       "before=%r" % depth_before)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("mixer masking report control-surface driver")
        return 1
    H.ok("mixer masking report control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
