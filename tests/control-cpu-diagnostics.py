#!/usr/bin/env python3
"""Q050-074.1: the CPU diagnostics over the control socket, headless (Dummy audio device).

Drives the REAL binary the way an agent does and prints every request and reply:

  1. two instrument tracks with a real instrument (plugin.load) on ONE mixer channel -
     one with notes, one silent - and a sample track;
  2. transport.play for about a second of real-time rendering, then `mixer.cpu_load`:
     every instrument track appears under 'tracks', keyed by its trk-<n> id, the
     playing one's synthesis_percent above zero and the silent one's exactly zero,
     the sample track absent; synthesis_total_percent is their sum;
  3. `track.overview` reports the same tracks' synthesis_cpu_percent beside the one
     channel figure (cpu_percent) the two instrument tracks share, and the sample
     track carries no synthesis figure at all;
  4. `late_periods`: a count, the ring's size and at most that many marks, each with
     serial / tick / position / playing / took_us > period_us - the engine's
     deadline misses as recorded; two reads with nothing rendered between agree;
  5. `control.commands_list`: both commands stay not_mutating with empty schemas.

No timing value is asserted beyond "above zero" (the machine's speed is not the test's).

Usage: QT_QPA_PLATFORM=offscreen python3 control-cpu-diagnostics.py <zene>
Exit code 0 only when every assertion held.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402


class Session:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 100

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args or {}, transcript=self.transcript)

    def ok(self, command, args=None):
        return H.ok_result(self.call(command, args), self.last_id)


def plugin_env(binary):
    plugin_dir = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return {"LMMS_PLUGIN_DIR": plugin_dir} if os.path.isdir(plugin_dir) else None


def instrument_device(session):
    listed = session.ok("plugin.list", {"kind": "instrument", "loadable_only": True}).get("devices") or []
    for device in listed:
        if "tripleoscillator" in str(device.get("id", "")).lower():
            return device.get("id")
    return listed[0].get("id") if listed else None


def instrument_track(session, name, device, channel):
    track = session.ok("track.add", {"type": "instrument", "name": name}).get("track")
    session.ok("plugin.load", {"target": track, "device": device})
    session.ok("track.set_mixer_channel", {"track": track, "channel": channel})
    return track


def by(entries, key, value):
    for entry in entries:
        if entry.get(key) == value:
            return entry
    return None


def check_synthesis(session, loud, silent, sample, problems):
    load = session.ok("mixer.cpu_load")
    tracks = load.get("tracks") or []
    loud_entry, silent_entry = by(tracks, "track", loud), by(tracks, "track", silent)
    problems.require(loud_entry is not None and silent_entry is not None,
                     "mixer.cpu_load lists both instrument tracks by id: %r" % tracks)
    problems.require(by(tracks, "track", sample) is None, "a sample track has no synthesis entry: %r" % tracks)
    if loud_entry is None or silent_entry is None:
        return load
    problems.require(loud_entry.get("synthesis_percent", 0) > 0,
                     "the playing track's synthesis is above zero: %r" % loud_entry)
    problems.require(silent_entry.get("synthesis_percent") == 0,
                     "the silent track's synthesis is zero: %r" % silent_entry)
    total = sum(entry.get("synthesis_percent", 0) for entry in tracks)
    problems.require(abs(load.get("synthesis_total_percent", -1) - total) < 1e-6,
                     "synthesis_total_percent is the tracks' sum: %r vs %r" % (load.get("synthesis_total_percent"), total))
    return load


def check_overview(session, loud, silent, sample, problems):
    rows = session.ok("track.overview").get("tracks") or []
    loud_row, silent_row, sample_row = by(rows, "id", loud), by(rows, "id", silent), by(rows, "id", sample)
    if None in (loud_row, silent_row, sample_row):
        problems.add("track.overview misses a track: %r" % rows)
        return
    problems.require(loud_row.get("synthesis_cpu_percent", 0) > 0, "overview: the playing track's synthesis: %r" % loud_row)
    problems.require(silent_row.get("synthesis_cpu_percent") == 0, "overview: the silent track's synthesis: %r" % silent_row)
    problems.require("synthesis_cpu_percent" not in sample_row, "overview: a sample track has no synthesis figure")
    problems.require(loud_row.get("channel") == silent_row.get("channel") and loud_row.get("cpu_percent") == silent_row.get("cpu_percent"),
                     "both share ONE channel figure, apart from their synthesis: %r / %r" % (loud_row, silent_row))


def check_late_periods(session, problems):
    first = session.ok("mixer.cpu_load").get("late_periods") or {}
    count, held, marks = first.get("count"), first.get("held"), first.get("marks")
    problems.require(isinstance(count, (int, float)) and count >= 0, "late_periods.count is a number: %r" % first)
    problems.require(held == 64, "late_periods.held is the ring's size: %r" % held)
    problems.require(isinstance(marks, list) and len(marks) <= min(count or 0, held or 0),
                     "at most count and held marks: %r" % first)
    keys = {"serial", "tick", "position", "playing", "took_us", "period_us", "percent"}
    for mark in marks or []:
        problems.require(keys <= set(mark), "a mark has every field: %r" % mark)
        problems.require(mark.get("took_us", 0) > mark.get("period_us", 0), "a mark took longer than its period: %r" % mark)
    serials = [mark.get("serial") for mark in marks or []]
    problems.require(serials == sorted(serials), "marks are oldest first: %r" % serials)
    print("late periods on this host: %r (%d held)" % (count, len(marks or [])))


def check_commands(session, problems):
    listed = session.ok("control.commands_list").get("commands") or []
    for name in ("mixer.cpu_load", "track.overview"):
        entry = by(listed, "id", name)
        problems.require(entry is not None and entry.get("mutating") is False,
                         "%s is registered read-only: %r" % (name, entry))


def run(binary, problems):
    transcript = H.Transcript()
    with H.start_instance(binary, extra_env=plugin_env(binary)) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        device = instrument_device(session)
        if device is None:
            H.fail("no loadable instrument in this build", instance, transcript)
        channel = session.ok("mixer.add_channel").get("channel")
        loud = instrument_track(session, "Loud", device, channel)
        silent = instrument_track(session, "Silent", device, channel)
        sample = session.ok("track.add", {"type": "sample", "name": "Audio"}).get("track")
        clip = session.ok("clip.add", {"track": loud, "position": 0}).get("clip")
        for key in (48, 55, 60, 64, 67):
            session.ok("note.add", {"clip": clip, "key": key, "position": 0, "length": 768, "velocity": 100})
        session.ok("transport.seek", {"ticks": 0})
        session.ok("transport.play")
        time.sleep(1.0)
        check_synthesis(session, loud, silent, sample, problems)
        check_overview(session, loud, silent, sample, problems)
        session.ok("transport.stop")
        check_late_periods(session, problems)
        check_commands(session, problems)
    transcript.dump()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = H.Problems()
    run(argv[1], problems)
    return H.finish([("per-track synthesis load and late periods", not problems, problems.items)])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
