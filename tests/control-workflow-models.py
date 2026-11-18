#!/usr/bin/env python3
"""Q050-159: the manual's pattern / clip / launcher-cell explanation, and its routing example.

The manual used to describe the timeline, the launcher and the mixer separately, so a
musician could not tell what a PatternStore pattern, a timeline clip and a launcher cell
each store, or which track owns the mixer channel a sound reaches. This driver proves the
new manual section and "Your first beat" tour step ship with those distinctions, and then
drives the REAL `zene` binary over the control socket to trace the example's sample track
to its mixer channel with the command names the manual gives.

  1. data/manual/index.html has the "workflows" section, with the groove / fill / kick.wav
     example and the three storage models;
  2. the section names the routing UI (Inspector "To ...", Mix > Routing) and the commands
     (`arrangement.get_state`, `session.pattern_list`, `session.get_state`,
     `track.overview`, `mixer.get_state`, `track.set_mixer_channel`, `session.set_slot`);
  3. data/tours/first-beat.json has one step comparing all three workflows;
  4. `track.overview` reports a sample track's ch-<n> channel, `mixer.get_state` has the
     same id and name, and one `control.undo` of `track.set_mixer_channel` restores the
     previous channel (true_inverse);
  5. the read-only commands the section names answer: `arrangement.get_state`,
     `session.pattern_list` and `session.get_state`.

Usage: QT_QPA_PLATFORM=offscreen python3 tests/control-workflow-models.py <zene>
Exit 0 only when every check held.
"""

import html
import json
import pathlib
import re
import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
MANUAL = ROOT / "data" / "manual" / "index.html"
TOUR = ROOT / "data" / "tours" / "first-beat.json"

SECTION = '<h2 id="workflows">Patterns, clips and cells</h2>'

# The concrete example and the three models, in the section's own words.
MANUAL_NEEDLES = (
    "PatternStore pattern 0",
    "bar 1",
    "bar 9",
    "kick.wav",
    "PatternTrack",
    "PatternClip",
    "placeholder has no instrument and no mixer channel of its own",
    "MIDI clip",
    "launcher cell",
    "audio cell on a sample track's column",
    "MIDI cell plays its referenced pattern through the PatternStore tracks",
)

# The current GUI labels and command names the routing example must use.
ROUTING_NEEDLES = (
    "Inspector's",
    '"To ..."',
    "Mix > Routing",
    "track.overview",
    "mixer.get_state",
    "track.set_mixer_channel",
    "arrangement.get_state",
    "session.pattern_list",
    "session.get_state",
    "session.set_slot",
)

TOUR_NEEDLES = (
    "kick-and-snare PatternStore pattern",
    "bar 1",
    "PatternTrack",
    "PatternClip placeholder",
    "one-bar MIDI fill",
    "bar 9",
    "kick.wav",
    "launcher cell",
    "no mixer channel of its own",
    'Inspector\'s "To ..." channel',
    "Mix > Routing",
    "track.overview",
    "mixer.get_state",
)


def check_manual(recorder):
    text = MANUAL.read_text(encoding="utf-8") if MANUAL.exists() else ""
    start = text.find(SECTION)
    end = text.find('<h2 id="projects">', start + 1) if start >= 0 else -1
    body = text[start:end] if start >= 0 and end > start else ""
    body = re.sub(r"\s+", " ", html.unescape(body))
    recorder.check("the manual has the Patterns, clips and cells section", bool(body),
                   "section=%r file=%s" % (SECTION, MANUAL))
    missing = [needle for needle in MANUAL_NEEDLES if needle not in body]
    recorder.check("the section gives the groove, fill and kick.wav example and the three models",
                   not missing, "missing=%r" % missing)
    missing = [needle for needle in ROUTING_NEEDLES if needle not in body]
    recorder.check("the section names the Inspector, Mix > Routing and the current commands",
                   not missing, "missing=%r" % missing)
    recorder.check("the section keeps the PatternTrack placeholder out of the channel list",
                   "address the song's own instrument and sample tracks" in body,
                   "section does not say which tracks own a channel")


def check_tour(recorder):
    tour = json.loads(TOUR.read_text(encoding="utf-8")) if TOUR.exists() else {}
    steps = tour.get("steps") or []
    matched = [step for step in steps if step.get("title") == "Patterns, clips and cells"]
    recorder.check("the first-beat tour has one Patterns, clips and cells step",
                   len(matched) == 1, "steps=%r" % [step.get("title") for step in steps])
    step = matched[0] if matched else {}
    text = step.get("text", "")
    missing = [needle for needle in TOUR_NEEDLES if needle not in text]
    recorder.check("the tour step compares patterns, clips and cells and names the routing reads",
                   not missing, "missing=%r text=%r" % (missing, text))
    recorder.check("the tour step points at the Inspector",
                   step.get("target") == "shellPanel_inspector" and step.get("panel") == "inspector",
                   "target=%r panel=%r" % (step.get("target"), step.get("panel")))


def matching_entry(entries, key, wanted):
    for entry in entries or []:
        if entry.get(key) == wanted:
            return entry
    return {}


def make_destination_channel(session, recorder, name):
    reply = session.result("mixer.add_channel")
    channel = reply.get("channel")
    recorder.check("mixer.add_channel makes the example's destination channel", bool(channel),
                   "reply=%r" % reply)
    session.result("mixer.rename_channel", {"channel": channel, "name": name})
    return channel


def make_sample_track(session, recorder):
    reply = session.result("track.add", {"type": "sample", "name": "Q050-159 Drums"})
    track = reply.get("track")
    recorder.check("track.add makes the example's sample track", bool(track), "reply=%r" % reply)
    return track


def check_track_channel(session, recorder):
    # The channel exists before the track, so the track's mixer-channel model is
    # built with the destination in range (the same order the GUI's Add Channel
    # then Add Track uses).
    name = "Q050-159 Drum Bus"
    channel = make_destination_channel(session, recorder, name)
    track = make_sample_track(session, recorder)

    recorded = session.result("track.set_mixer_channel", {"track": track, "channel": channel})
    previous = recorded.get("previous")
    routed = recorded.get("channel") == channel and isinstance(previous, str)
    recorder.check("track.set_mixer_channel routes the sample track, naming the previous channel",
                   routed, "route=%r" % recorded)

    entry = matching_entry(session.result("track.overview").get("tracks"), "id", track)
    recorder.check("track.overview reports the sample track's ch-<n> channel",
                   entry.get("channel") == channel, "entry=%r" % entry)

    matched = matching_entry(session.result("mixer.get_state").get("channels"), "id", channel)
    recorder.check("mixer.get_state has the same channel id and name",
                   matched.get("id") == channel and matched.get("name") == name,
                   "channel=%r reply=%r" % (channel, matched))

    session.result("control.undo")
    after = matching_entry(session.result("track.overview").get("tracks"), "id", track)
    recorder.check("one undo of track.set_mixer_channel restores the previous channel",
                   after.get("channel") == previous, "after=%r previous=%r" % (after.get("channel"), previous))
    return track


def check_reads(session, recorder, track):
    arrangement = session.result("arrangement.get_state")
    listed = [item for item in arrangement.get("tracks") or [] if item.get("id") == track]
    recorder.check("arrangement.get_state lists the example's song track", len(listed) == 1,
                   "listed=%r" % listed)

    patterns = session.result("session.pattern_list")
    recorder.check("session.pattern_list answers with the patterns a MIDI cell can play",
                   isinstance(patterns.get("patterns"), list) and isinstance(patterns.get("count"), int),
                   "reply=%r" % patterns)

    state = session.result("session.get_state")
    recorder.check("session.get_state answers with the grid and its cells",
                   isinstance(state.get("grid"), dict) and isinstance(state.get("slots"), list),
                   "reply=%r" % state)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    check_manual(recorder)
    check_tour(recorder)
    with H.start_instance(argv[1], workingdir=None) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        track = check_track_channel(session, recorder)
        check_reads(session, recorder, track)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("patterns, clips and cells control-surface driver")
        return 1
    H.ok("patterns, clips and cells control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
