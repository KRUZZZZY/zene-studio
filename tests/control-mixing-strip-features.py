#!/usr/bin/env python3
"""The strip's Console section, per-strip meter modes, delay offsets and input meters, over the control socket.

Starts the REAL `zene` binary headless (its own throwaway config) and proves, through the commands an agent
(and the interface) takes:

  1. Q050-088: window.mixer_view 'show_console' is set and answered; mixer.console_strip sets and reads one
     channel's trim, polarity, EQ, its Console's drive and glue compressor - the other channel keeps its own -,
     one undo puts the section back, and bad values or unknown channels are refused, changing nothing;
  2. Q050-108: mixer.set_strip 'meter_mode' sets one strip's meter, mixer.strip_get answers it and its readings;
  3. Q050-339: pdc.set_offset sets and reads an audio track's, an instrument track's and a channel's delay;
  4. Q050-340: meter.inputs answers every capture channel and the armed tracks' inputs; window.input_meters
     opens the window and answers what it shows.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-mixing-strip-features.py <zene>
Exit 0 only when every check held.
"""

import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def kind(reply):
    return (reply.get("error") or {}).get("kind")


def check_console(session, recorder, chans):
    view = session.result("window.mixer_view", {"show_console": True})
    recorder.check("window.mixer_view shows the Console section", view.get("show_console") is True, "reply=%r" % view)
    applied = session.call("mixer.console_apply", {"drive": 30})
    recorder.check("mixer.console_apply puts a Console on every channel", applied.get("ok"), "reply=%r" % applied)
    wanted = {"trim_db": 2.5, "polarity_left": True, "polarity_right": False, "eq_low_db": 3.0, "eq_mid_db": -2.0,
              "eq_mid_freq": 1500.0, "eq_high_db": 1.0, "drive": 55.0, "glue": True, "glue_threshold_db": -20.0,
              "glue_ratio": 4.0, "glue_makeup_db": 3.0}
    args = dict(wanted)
    args["channel"] = chans[0]
    set_reply = session.call("mixer.console_strip", args)
    recorder.check("mixer.console_strip writes the section", set_reply.get("ok"), "reply=%r" % set_reply)
    read = session.result("mixer.console_strip", {"channel": chans[0]})
    recorder.check("every field reads back", read.get("console") is True and all(read.get(k) == v for k, v in wanted.items()),
                   "reply=%r" % read)
    other = session.result("mixer.console_strip", {"channel": chans[1]})
    recorder.check("the other channel keeps its own", other.get("trim_db") == 0 and other.get("glue") is False
                   and other.get("drive") == 30, "reply=%r" % other)
    strip = session.result("mixer.strip_get", {"channel": chans[0]})
    recorder.check("mixer.strip_get sees the same trim and polarity", strip.get("trim_db") == 2.5 and strip.get("polarity_left") is True,
                   "reply=%r" % strip)
    bad = session.call("mixer.console_strip", {"channel": chans[0], "glue_ratio": 50})
    recorder.check("an out-of-range ratio is refused, typed", kind(bad) == "invalid_args", "reply=%r" % bad)
    missing = session.call("mixer.console_strip", {"channel": "ch-99999", "trim_db": 1})
    recorder.check("an unknown channel is refused, typed", kind(missing) == "not_found", "reply=%r" % missing)
    unchanged = session.result("mixer.console_strip", {"channel": chans[0]})
    recorder.check("a refused call changes nothing", unchanged.get("glue_ratio") == 4.0, "reply=%r" % unchanged)
    session.result("control.undo")
    undone = session.result("mixer.console_strip", {"channel": chans[0]})
    recorder.check("one undo puts the section back", undone.get("trim_db") == 0 and undone.get("glue") is False
                   and undone.get("drive") == 30 and undone.get("eq_low_db") == 0, "reply=%r" % undone)
    session.result("window.mixer_view", {"show_console": False})


def check_meter_modes(session, recorder, chans):
    vu = session.call("mixer.set_strip", {"channel": chans[0], "meter_mode": "vu"})
    lufs = session.call("mixer.set_strip", {"channel": chans[1], "meter_mode": "lufs"})
    recorder.check("mixer.set_strip sets each strip's meter mode", vu.get("ok") and lufs.get("ok"), "vu=%r lufs=%r" % (vu, lufs))
    first = session.result("mixer.strip_get", {"channel": chans[0]})
    second = session.result("mixer.strip_get", {"channel": chans[1]})
    third = session.result("mixer.strip_get", {"channel": chans[2]})
    recorder.check("each strip answers its own mode", first.get("meter_mode") == "vu" and second.get("meter_mode") == "lufs"
                   and third.get("meter_mode") == "default", "modes=%r" % [first.get("meter_mode"), second.get("meter_mode"), third.get("meter_mode")])
    meters = second.get("meters") or {}
    recorder.check("the readings are answered (null until measured)", all(k in meters for k in
                   ("vu_left", "vu_right", "lufs_momentary", "lufs_short_term", "true_peak_dbtp", "vu_reference_dbfs")), "reply=%r" % meters)
    bad = session.call("mixer.set_strip", {"channel": chans[0], "meter_mode": "ppm"})
    recorder.check("an unknown meter mode is refused, typed", kind(bad) == "invalid_args", "reply=%r" % bad)
    view = session.result("window.mixer_view")
    recorder.check("the view's own meter mode is untouched", view.get("meter_mode") == "peak", "reply=%r" % view.get("meter_mode"))


def check_offsets(session, recorder, chans):
    audio = session.result("track.add", {"type": "sample"}).get("track")
    instrument = session.result("track.add", {"type": "instrument"}).get("track")
    for target, samples in ((audio, 37), (instrument, -12), (chans[0], 53)):
        reply = session.call("pdc.set_offset", {"target": target, "offset_samples": samples})
        recorder.check("pdc.set_offset sets %s" % target, reply.get("ok") and (reply.get("result") or {}).get("offset_samples") == samples,
                       "reply=%r" % reply)
    reads = [session.result("pdc.set_offset", {"target": t}).get("offset_samples") for t in (audio, instrument, chans[0], chans[1])]
    recorder.check("each target reads its own offset back", reads == [37, -12, 53, 0], "reads=%r" % reads)
    report = session.call("pdc.report")
    recorder.check("pdc.report still answers with offsets in the graph", report.get("ok"), "reply=%r" % report)
    strips = session.result("window.mixer_view").get("strips") or []
    master_id = next((s.get("channel") for s in strips if s.get("type") == "master"), None)
    master = session.call("pdc.set_offset", {"target": master_id, "offset_samples": 10})
    recorder.check("the master is refused, typed", bool(master_id) and kind(master) == "refused", "master=%r reply=%r" % (master_id, master))
    session.result("control.undo")
    recorder.check("one undo puts the channel's previous offset back",
                   session.result("pdc.set_offset", {"target": chans[0]}).get("offset_samples") == 0, "")


def check_inputs(session, recorder):
    state = session.result("meter.inputs")
    recorder.check("meter.inputs answers the channels and the armed tracks",
                   isinstance(state.get("channels"), list) and isinstance(state.get("armed"), list) and "channel_count" in state,
                   "reply=%r" % state)
    shown = session.call("window.input_meters")
    recorder.check("window.input_meters opens the window and answers its meters",
                   shown.get("ok") and (shown.get("result") or {}).get("shown") is True, "reply=%r" % shown)


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
        session.result("control.set_undo_coalescing", {"window_ms": 0})
        chans = [session.result("mixer.add_channel").get("channel") for _ in range(3)]
        check_console(session, recorder, chans)
        check_meter_modes(session, recorder, chans)
        check_offsets(session, recorder, chans)
        check_inputs(session, recorder)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("strip features control-surface driver")
        return 1
    H.ok("strip features control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
