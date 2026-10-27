#!/usr/bin/env python3
"""Q050-089.2: render.realtime - a real-time render through the hardware insert, over the control socket.

Starts the REAL `zene` binary headless on the Dummy device, configured as a four-output interface whose
loopback cable runs from outputs 3/4 back to the input with 100 + 200 frames of device latency, and puts a
box on that cable that halves the level (LMMS_DUMMY_LOOPBACK_GAIN=0.5): outboard gear whose effect a render
can see. One sample track plays a generated tone through a mixer channel whose hardware insert sends to 3/4
and returns from 1/2 (fully wet), and the insert's round trip is pinged. Then:

  1. render.realtime with the insert off is refused (requires) and writes nothing;
  2. render.render (offline) still renders the DRY tone and its reply's hardware_inserts names the insert as
     bypassed, the hardware as unavailable;
  3. render.realtime writes a WAV that opens: its reply's path, frames and sha256 match the file, it names
     the insert it played (latency 812, measured) and the delay compensation it dropped;
  4. the file holds the PROCESSED return: sample for sample it is the offline render scaled by the box's
     0.5, aligned to the frame (the compensation head is dropped), 6 dB down;
  5. meter.measure_file measures it, 6 dB below the offline render;
  6. an insert sending to 7/8, which the four-output device cannot carry, is refused (refused, naming the
     pair and beyond_device) and writes nothing;
  7. a second instance whose device delivers no input (no loopback) refuses it too: no fallback to offline.

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-realtime-export.py <zene>
Exit 0 only when every check held.
"""

import array
import hashlib
import math
import os
import sys
import wave

import control_socket_harness as H
from freeze_bounce_evidence import REQUEST_IDS, Recorder, Session, report_results

GAIN = 0.5
ROUND_TRIP = 100 + 200 + 2 * 256  # the device's latency plus one period of output and one of capture staging
INTERFACE = {"LMMS_DUMMY_OUTPUT_CHANNELS": "4"}
ENV = dict(INTERFACE, LMMS_DUMMY_LOOPBACK="1", LMMS_DUMMY_LOOPBACK_OUTPUT="3", LMMS_DUMMY_OUTPUT_LATENCY="100",
           LMMS_DUMMY_INPUT_LATENCY="200", LMMS_DUMMY_LOOPBACK_GAIN=str(GAIN))
REALTIME_TIMEOUT = 120.0


def kind(reply):
    return (reply.get("error") or {}).get("kind")


def realtime(session, out, **extra):
    """render.realtime: it plays the song in real time, so it gets a longer budget than a plain request."""
    args = dict({"out": out}, **extra)
    return session.client.call(next(REQUEST_IDS), "render.realtime", args, timeout=REALTIME_TIMEOUT,
                                transcript=session.transcript)


def left_channel(path):
    """The file's left channel as floats in -1..1, and its frame count."""
    with wave.open(path, "rb") as handle:
        width, frames = handle.getsampwidth(), handle.getnframes()
        raw = handle.readframes(frames)
    if width != 2:
        raise AssertionError("expected 16-bit PCM, got width %d" % width)
    samples = array.array("h")
    samples.frombytes(raw)
    return [samples[i] / 32768.0 for i in range(0, len(samples), 2)], frames


def onset(samples, threshold):
    return next((i for i, value in enumerate(samples) if abs(value) > threshold), -1)


def rms(samples):
    return math.sqrt(sum(v * v for v in samples) / max(1, len(samples)))


def build(session):
    """A one-bar song: a sample track playing half a bar of tone (from the middle of the bar) through its own
    mixer channel. The channel is made first: a track's channel choice is bounded by the channels there are."""
    chan = session.result("mixer.add_channel").get("channel")
    track = session.result("track.add", {"type": "sample", "name": "Tone"}).get("track")
    session.result("sample.generate", {"track": track, "position": 96, "length": 96, "kind": "tone",
                                       "frequency": 440, "amplitude": 0.5})
    routed = session.result("track.set_mixer_channel", {"track": track, "channel": chan})
    if routed.get("channel") != chan:
        raise AssertionError("the track was not routed to %s: %r" % (chan, routed))
    return chan


def check_needs_insert(session, recorder, tmp):
    out = os.path.join(tmp, "no-insert.wav")
    reply = realtime(session, out)
    recorder.check("with no insert on, render.realtime is refused and writes nothing",
                   kind(reply) == "requires" and not os.path.exists(out), "reply=%r" % reply)


def set_up_insert(session, recorder, chan):
    made = session.result("mixer.hardware_insert", {"channel": chan, "enabled": True, "send": "3/4", "return": "1/2"})
    recorder.check("the insert sends to 3/4 and returns from 1/2", made.get("enabled") is True, "reply=%r" % made)
    session.result("mixer.hardware_insert_ping", {"channel": chan})
    insert = {}
    for _ in range(100):
        state = session.result("mixer.hardware_get", {"channel": chan})
        insert = ((state.get("channels") or [{}])[0].get("insert") or {})
        if insert.get("ping") in ("done", "failed"):
            break
        H.time.sleep(0.1)
    recorder.check("the ping measures the round trip", insert.get("measured") == ROUND_TRIP, "insert=%r" % insert)


def check_offline(session, recorder, chan, path):
    reply = session.result("render.render", {"out": path, "format": "wav"})
    report = reply.get("hardware_inserts") or {}
    inserts = report.get("inserts") or []
    recorder.check("render.render says the hardware was unavailable and the insert bypassed",
                   report.get("mode") == "offline" and report.get("hardware") == "unavailable"
                   and report.get("bypassed") is True and [i.get("channel") for i in inserts] == [chan],
                   "hardware_inserts=%r" % report)
    samples, _ = left_channel(path)
    peak = max(abs(v) for v in samples)
    recorder.check("the offline render is the dry tone (peak ~0.5)", abs(peak - 0.5) < 0.02, "peak=%.4f" % peak)
    return samples


def check_realtime(session, recorder, chan, path, dry):
    reply = realtime(session, path)
    result = reply.get("result") or {}
    recorder.check("render.realtime answers with the file it wrote", reply.get("ok") is True and result.get("path") == path
                   and os.path.isfile(path), "reply=%r" % reply)
    if not os.path.isfile(path):
        return
    wet, frames = left_channel(path)
    with open(path, "rb") as handle:
        digest = hashlib.sha256(handle.read()).hexdigest()
    played = ((result.get("hardware_inserts") or {}).get("inserts") or [{}])[0]
    recorder.check("its frames and sha256 are the file's own", result.get("frames") == frames and result.get("sha256") == digest
                   and result.get("mode") == "realtime", "frames=%d reply=%r" % (frames, result))
    recorder.check("it names the insert it played and the compensation it dropped",
                   played.get("channel") == chan and played.get("latency_frames") == ROUND_TRIP
                   and played.get("latency_source") == "measured"
                   and result.get("latency_compensation_frames") == ROUND_TRIP, "reply=%r" % result)
    song = frames - int(result.get("tail_frames", 0))
    lag = onset(wet, 0.01 * GAIN) - onset(dry, 0.01)
    recorder.check("the processed return lines up with the song to the frame", lag == 0,
                   "onset wet=%d dry=%d" % (onset(wet, 0.01 * GAIN), onset(dry, 0.01)))
    window = min(song, len(dry))
    residual = max(abs(wet[k] - GAIN * dry[k]) for k in range(window))
    ratio = rms(wet[:window]) / max(1e-9, rms(dry[:window]))
    recorder.check("the file is the insert's return: the dry render x 0.5, sample for sample",
                   abs(ratio - GAIN) < 0.01 and residual < 0.002, "ratio=%.5f residual=%.6f window=%d" % (ratio, residual, window))


def check_measured(session, recorder, wet_path, dry_path):
    wet = session.result("meter.measure_file", {"path": wet_path})
    dry = session.result("meter.measure_file", {"path": dry_path})
    a, b = wet.get("integrated_lufs"), dry.get("integrated_lufs")
    recorder.check("meter.measure_file measures the real-time render, 6 dB below the dry render",
                   isinstance(a, (int, float)) and isinstance(b, (int, float)) and abs((b - a) - 6.02) < 0.5,
                   "wet=%r dry=%r" % (wet, dry))


def check_unavailable(session, recorder, chan, tmp):
    session.result("mixer.hardware_insert", {"channel": chan, "send": "7/8"})
    out = os.path.join(tmp, "beyond.wav")
    reply = realtime(session, out)
    message = (reply.get("error") or {}).get("message", "")
    recorder.check("an insert the device cannot carry (7/8) is refused, named, and nothing is written",
                   kind(reply) == "refused" and "7/8" in message and "beyond_device" in message and not os.path.exists(out),
                   "reply=%r" % reply)
    session.result("control.undo")


def check_no_input(binary, recorder):
    """A device that delivers no input cannot bring a return back: refused, no offline fallback."""
    transcript = H.Transcript()
    with H.start_instance(binary, workingdir=None, extra_env=INTERFACE) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        chan = build(session)
        session.result("mixer.hardware_insert", {"channel": chan, "enabled": True, "send": "3/4", "return": "1/2"})
        out = os.path.join(instance.tmp, "no-input.wav")
        reply = realtime(session, out)
        message = (reply.get("error") or {}).get("message", "")
        recorder.check("a device with no input is refused (no offline fallback) and nothing is written",
                       kind(reply) == "refused" and "no input" in message and not os.path.exists(out), "reply=%r" % reply)


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
        chan = build(session)
        check_needs_insert(session, recorder, instance.tmp)
        set_up_insert(session, recorder, chan)
        dry_path = os.path.join(instance.tmp, "offline.wav")
        wet_path = os.path.join(instance.tmp, "realtime.wav")
        dry = check_offline(session, recorder, chan, dry_path)
        check_realtime(session, recorder, chan, wet_path, dry)
        check_measured(session, recorder, wet_path, dry_path)
        check_unavailable(session, recorder, chan, instance.tmp)
    check_no_input(argv[1], recorder)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("real-time hardware insert export")
        return 1
    H.ok("real-time hardware insert export (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
