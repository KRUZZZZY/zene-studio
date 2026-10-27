#!/usr/bin/env python3
"""0.5.0 F1 hardware I/O (MX-040, MX-041, MX-036, MX-061), driven end to end over the control socket.

Starts the REAL `zene` binary headless on the Dummy device, configured as a four-output interface
(LMMS_DUMMY_OUTPUT_CHANNELS=4) whose loopback cable runs from outputs 3/4 back to the input
(LMMS_DUMMY_LOOPBACK, LMMS_DUMMY_LOOPBACK_OUTPUT=3) with 100 + 200 frames of device latency, and proves:

  1. audio.io_list reports the four outputs the device opened, multichannel;
  2. audio.io_setup names the outputs and their buses; a route can use a bus name;
  3. mixer.hardware_output routes a channel to 3/4 (status ok) and says when a pair is beyond the
     device (7/8: beyond_device); one undo takes a route back;
  4. mixer.cue_master / cue_send build a cue mix on 3/4;
  5. a hardware insert sending to 3/4 and returning from 1/2 measures its round trip with a ping:
     the device's 300 frames plus one engine period of output double buffer and one of capture
     staging (the harness config sets no buffer size, so the engine's default 256-frame period);
     pdc.report lists that measured figure on the channel, then a typed override in its place
     (Q050-089.1: hardware_insert_latency_frames / _source, the figure PDC counts);
  6. the routes, the cue mix and the insert survive project.save / project.open.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-hardware-io.py <zene>
Exit 0 only when every check held.
"""

import os
import sys
import time

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

DEVICE_LATENCY = 100 + 200
ENGINE_PERIOD = 256
ENV = {
    "LMMS_DUMMY_OUTPUT_CHANNELS": "4",
    "LMMS_DUMMY_LOOPBACK": "1",
    "LMMS_DUMMY_LOOPBACK_OUTPUT": "3",
    "LMMS_DUMMY_OUTPUT_LATENCY": "100",
    "LMMS_DUMMY_INPUT_LATENCY": "200",
}


def kind(reply):
    return (reply.get("error") or {}).get("kind")


def check_interface(session, recorder):
    io = session.result("audio.io_list")
    outputs = io.get("outputs") or {}
    recorder.check("audio.io_list reports the four outputs the device opened", outputs.get("granted") == 4
                   and outputs.get("multichannel") is True, "reply=%r" % io)
    setup = session.result("audio.io_setup", {"outputs": {"channels": 4, "names": ["Main L", "Main R", "Phones L", "Phones R"],
                                                          "buses": [{"name": "Main", "channels": "1/2"},
                                                                    {"name": "Phones", "channels": "3/4"}]}})
    buses = [b.get("name") for b in (setup.get("outputs") or {}).get("buses") or []]
    recorder.check("audio.io_setup names the outputs and their buses", buses == ["Main", "Phones"], "reply=%r" % setup)


def check_routes(session, recorder, chan):
    routed = session.result("mixer.hardware_output", {"channel": chan, "output": "Phones"})
    out = routed.get("output") or {}
    recorder.check("a channel is routed to a bus by name, on outputs 3/4", out.get("channels") == "3/4" and out.get("status") == "ok",
                   "reply=%r" % routed)
    beyond = session.result("mixer.hardware_output", {"channel": chan, "output": "7/8"})
    recorder.check("a pair the device did not open says so", (beyond.get("output") or {}).get("status") == "beyond_device",
                   "reply=%r" % beyond)
    session.result("control.undo")
    back = session.result("mixer.hardware_get", {"channel": chan})
    first = ((back.get("channels") or [{}])[0].get("output") or {})
    recorder.check("one undo puts the previous route back", first.get("channels") == "3/4", "reply=%r" % back)
    bad = session.call("mixer.hardware_output", {"channel": chan, "output": "3/5"})
    recorder.check("a pair that is not two neighbours is refused, typed", kind(bad) == "invalid_args", "reply=%r" % bad)


def check_cue(session, recorder, chan):
    cue = session.result("mixer.cue_master", {"cue": "A", "name": "Singer", "output": "3/4", "level": 0.8})
    first = (cue.get("cues") or [{}])[0]
    recorder.check("a cue mix is routed to 3/4", first.get("name") == "Singer" and (first.get("output") or {}).get("status") == "ok",
                   "reply=%r" % cue)
    send = session.result("mixer.cue_send", {"channel": chan, "cue": "A", "level": 0.5})
    recorder.check("a channel sends to it, before its fader by default", send.get("level") == 0.5 and send.get("pre_fader") is True,
                   "reply=%r" % send)


def wait_for_ping(session, chan, seconds=10.0):
    deadline = time.time() + seconds
    insert = {}
    while time.time() < deadline:
        state = session.result("mixer.hardware_get", {"channel": chan})
        insert = ((state.get("channels") or [{}])[0].get("insert") or {})
        if insert.get("ping") in ("done", "failed"):
            return insert
        time.sleep(0.1)
    return insert


def check_insert(session, recorder, chan):
    # The cue and the direct out are taken off 3/4 so the loop carries the click alone.
    session.result("mixer.cue_master", {"cue": "A", "output": "none"})
    session.result("mixer.hardware_output", {"channel": chan, "output": "default"})
    made = session.result("mixer.hardware_insert", {"channel": chan, "enabled": True, "send": "3/4", "return": "1/2"})
    recorder.check("a hardware insert sends to 3/4 and returns from 1/2", made.get("enabled") is True
                   and (made.get("send") or {}).get("status") == "ok"
                   and (made.get("return") or {}).get("status") in ("ok", "stereo_bus"), "reply=%r" % made)
    pinged = session.result("mixer.hardware_insert_ping", {"channel": chan})
    recorder.check("the ping starts measuring", pinged.get("ping") == "measuring", "reply=%r" % pinged)
    insert = wait_for_ping(session, chan)
    expected = DEVICE_LATENCY + 2 * ENGINE_PERIOD
    recorder.check("the ping measures the round trip exactly", insert.get("ping") == "done" and insert.get("measured") == expected
                   and insert.get("latency") == expected, "expected %d, insert=%r" % (expected, insert))


def pdc_insert(session, chan):
    """The channel's hardware insert latency as pdc.report lists it: (frames, source)."""
    for channel in session.result("pdc.report").get("channels") or []:
        if channel.get("id") == chan:
            return channel.get("hardware_insert_latency_frames"), channel.get("hardware_insert_latency_source")
    return None, None


def check_insert_in_pdc(session, recorder, chan):
    """Q050-089.1: pdc.report names the insert's effective latency - the measured figure, then an override."""
    measured = DEVICE_LATENCY + 2 * ENGINE_PERIOD
    shown = pdc_insert(session, chan)
    recorder.check("pdc.report lists the measured insert latency on its channel", shown == (measured, "measured"),
                   "expected (%d, 'measured'), pdc=%r" % (measured, shown))
    session.result("mixer.hardware_insert", {"channel": chan, "latency_frames": 300})
    shown = pdc_insert(session, chan)
    recorder.check("a typed override replaces the measured figure in pdc.report", shown == (300, "override"),
                   "pdc=%r" % (shown,))
    session.result("control.undo")
    shown = pdc_insert(session, chan)
    recorder.check("undoing the override shows the measured figure again", shown == (measured, "measured"),
                   "pdc=%r" % (shown,))


def routed_channel(state):
    """The channel whose own (not default) hardware output is 3/4, or {}."""
    for channel in state.get("channels") or []:
        out = channel.get("output") or {}
        if out.get("channels") == "3/4" and not out.get("default"):
            return channel
    return {}


def check_persistence(session, recorder, chan, path):
    session.result("mixer.cue_master", {"cue": "B", "output": "3/4", "level": 0.6})
    session.result("mixer.hardware_output", {"channel": chan, "output": "3/4"})
    saved = session.call("project.save", {"path": path}).get("ok")
    opened = session.call("project.open", {"path": path}).get("ok")
    state = session.result("mixer.hardware_get")
    insert = routed_channel(state).get("insert") or {}
    cue_b = ((state.get("cues") or [{}, {}])[1].get("output") or {}).get("channels")
    recorder.check("routes, the cue mix and the insert survive a save and open",
                   saved and opened and insert.get("enabled") is True
                   and insert.get("measured") == DEVICE_LATENCY + 2 * ENGINE_PERIOD and cue_b == "3/4", "state=%r" % state)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1], workingdir=None, extra_env=ENV) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        chan = session.result("mixer.add_channel").get("channel")
        check_interface(session, recorder)
        check_routes(session, recorder, chan)
        check_cue(session, recorder, chan)
        check_insert(session, recorder, chan)
        check_insert_in_pdc(session, recorder, chan)
        check_persistence(session, recorder, chan, os.path.join(instance.tmp, "hardware-io.mmp"))
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("hardware I/O control-surface driver")
        return 1
    H.ok("hardware I/O control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
