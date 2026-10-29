#!/usr/bin/env python3
"""Q050-489: a MIDI and an audio launcher workflow over the control socket, each edited, undone and recorded to the
arrangement while it plays, read back and RENDERED (docs/050-features/Q050-489.md).

WHAT IT PROVES, on the real binary:
  MIDI  a cell holding a pattern with notes, launched: the render sounds; the playing cell CLEARED: the launcher
        reports its column idle and the render is silent again (before Q050-489 the column kept playing and the
        render kept the pattern); the undo puts the cell back without launching it; the cell launched with
        Arrangement Record armed and stopped lands ONE instrument clip carrying the pattern's notes, which the
        arrangement reports and the render sounds.
  AUDIO a cell holding a tone WAV, launched: the render sounds; the playing cell pointed at a SILENT WAV: the render
        is silent at once (before Q050-489 it kept the tone until some later launch); its undo: the tone again; the
        cell recorded to the arrangement lands ONE sample clip on its sample track.
  LOAD  the project saved and reopened holds the same cells and launches nothing.

The pattern's notes are put in by the test: a fresh instance's own saved project gets four notes in its pattern
store's first clip (there is no command that writes into the pattern store), played by a copy of the project's own
TripleOscillator, and is reopened. A launched cell is HEARD on the live master (meter.get_state) while the transport
runs - render.render renders the saved project, and launch state is by contract not saved - and the clips the
recordings land in the arrangement are RENDERED.

Usage: QT_QPA_PLATFORM=offscreen python3 control-session-transitions.py <zene>
Exit 0 only when every check held.
"""

import math
import os
import re
import struct
import sys
import tempfile
import time
import wave

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, render_measure, report_results

#: Quieter than this is silence (a 16-bit render of nothing measures -inf); louder than SOUND is a sound.
SILENT_DBFS = -90.0
SOUND_DBFS = -60.0
WAIT_SECONDS = 20.0
NOTES = "".join('<note key="57" vol="100" pan="0" len="24" pos="%d" type="0"/>' % pos for pos in (0, 48, 96, 144))


def write_wav(path, amplitude, seconds=1.0, rate=44100):
    frames = int(seconds * rate)
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(b"".join(struct.pack("<h", int(amplitude * 32767 * math.sin(2 * math.pi * 440 * i / rate)))
                                    for i in range(frames)))


def columns(session):
    """{track: (phase, scene)} for every column the launcher reports as not idle."""
    return {int(c.get("track", -1)): (c.get("phase"), int(c.get("scene", -1)))
            for c in session.result("session.get_state").get("columns", [])}


def wait_column(session, track, phase):
    """Waits until column @track reports @phase ("idle": not listed). True when it did."""
    deadline = time.time() + WAIT_SECONDS
    while time.time() < deadline:
        now = columns(session).get(track, ("idle", -1))[0]
        if now == phase:
            return True
        time.sleep(0.05)
    return False


def slot(session, track, scene):
    for cell in session.result("session.get_state").get("slots", []):
        if int(cell.get("track", -1)) == track and int(cell.get("scene", -1)) == scene:
            return cell
    return {}


def clips_on(session, track):
    """The clips arrangement.get_state reports on @track (a track.list entry)."""
    for entry in session.result("arrangement.get_state").get("tracks", []):
        if entry.get("id") == track.get("id"):
            return entry.get("clips", [])
    return []


def clip_id(clip):
    return clip.get("id") if isinstance(clip, dict) else clip


def live(session, name):
    """The master's momentary loudness (LUFS, last 400 ms) after the engine has played this state for 0.8 s; the
    tap was armed once, before the workflows (meter.arm is an undo step, so it is never re-armed between edits)."""
    time.sleep(0.8)
    reading = session.result("meter.get_state").get("live", {}).get("momentary_lufs")
    playing = session.result("transport.get_state").get("playing")
    print("live   %-28s %s LUFS (transport playing=%r)" % (name, reading, playing))
    return -999.0 if reading is None else float(reading)


def level(session, workdir, name):
    measured, _, reply = render_measure(session, os.path.join(workdir, name + ".wav"))
    print("render %-28s %s dBFS %s" % (name, measured, "" if measured is not None else reply))
    return -999.0 if measured is None or reply.get("error") else measured


def build_fixture(session, workdir, recorder):
    """The instance's own project, with four notes in pattern 0, reopened; a 3 x 2 grid, quantisation none."""
    plain = os.path.join(workdir, "plain.mmp")
    recorder.check("the fresh project saves", "error" not in session.result("project.save", {"path": plain}), plain)
    with open(plain, encoding="utf-8") as handle:
        text = handle.read()
    store = text.find('<trackcontainer type="patternstore">')
    match = re.compile(r'<midiclip ([^>]*?)/>').search(text, store)
    recorder.check("the pattern store has a clip to write notes into", store >= 0 and match is not None, "store=%d" % store)
    if store < 0 or match is None:
        return None
    text = text[:match.start()] + "<midiclip %s>%s</midiclip>" % (match.group(1), NOTES) + text[match.end():]
    # The template's pattern instrument (Kicker) is not a module every build compiles; the song's own first track is
    # TripleOscillator, so the pattern plays through a copy of that instrument instead.
    synth = re.search(r'<instrument name="tripleoscillator">.*?</instrument>', text, re.S)
    kicker = re.compile(r'<instrument name="kicker">.*?</instrument>', re.S).search(text, store)
    recorder.check("the template has a TripleOscillator to play the pattern", synth is not None, "")
    if synth is not None and kicker is not None:
        text = text[:kicker.start()] + synth.group(0) + text[kicker.end():]
    fixture = os.path.join(workdir, "fixture.mmp")
    with open(fixture, "w", encoding="utf-8") as handle:
        handle.write(text)
    recorder.check("the fixture opens", "error" not in session.result("project.open", {"path": fixture}), fixture)
    tracks = session.result("track.list").get("tracks", [])
    kinds = [t.get("type") for t in tracks]
    recorder.check("column 0 is an instrument track and column 1 a sample track", kinds[:2] == ["instrument", "sample"],
                   "tracks=%r" % kinds)
    # render.render refuses a song with no clip at all: a silent anchor (an empty clip on a track of its own, outside
    # the grid) gives the song its length without making a sound.
    anchor = session.result("track.add", {"type": "instrument", "name": "Anchor"}).get("track")
    session.result("clip.add", {"track": anchor, "position": 0, "length": 768})
    session.result("session.set_grid", {"tracks": 3, "scenes": 2})
    session.result("session.set_quantisation", {"quantisation": "none"})
    return tracks


def midi_workflow(session, workdir, recorder, tracks):
    session.result("session.set_slot", {"track": 0, "scene": 0, "type": "midi", "pattern": 0, "name": "Beat"})
    recorder.check("nothing launched: the master is silent", live(session, "midi-idle") < SILENT_DBFS, "")
    session.result("session.launch_slot", {"track": 0, "scene": 0})
    recorder.check("the MIDI cell launches", wait_column(session, 0, "playing"), "columns=%r" % columns(session))
    recorder.check("the launched MIDI cell sounds", live(session, "midi-launched") > SOUND_DBFS, "")

    cleared = session.result("session.clear_slot", {"track": 0, "scene": 0})
    recorder.check("clear_slot empties the playing cell", "error" not in cleared and not slot(session, 0, 0), "%r" % cleared)
    recorder.check("the cleared cell's column goes idle", wait_column(session, 0, "idle"), "columns=%r" % columns(session))
    recorder.check("the cleared cell sounds no more", live(session, "midi-cleared") < SILENT_DBFS, "")
    session.result("control.undo")
    recorder.check("the undo puts the cell back", slot(session, 0, 0).get("name") == "Beat", "%r" % slot(session, 0, 0))
    time.sleep(0.2)
    recorder.check("the undo launches nothing", 0 not in columns(session), "columns=%r" % columns(session))

    session.result("session.arrangement_record_arm", {"armed": True})
    session.result("session.launch_slot", {"track": 0, "scene": 0})
    wait_column(session, 0, "playing")
    time.sleep(1.0)
    session.result("session.stop_slot", {"track": 0, "scene": 0})
    recorder.check("the recorded cell stops", wait_column(session, 0, "idle"), "columns=%r" % columns(session))
    landed = session.result("session.arrangement_record_land")
    session.result("session.arrangement_record_arm", {"armed": False})
    recorder.check("the MIDI performance lands one clip", landed.get("clips") == 1, "land=%r" % landed)
    clips = clips_on(session, tracks[0])
    recorder.check("the arrangement reports the landed clip on the cell's track", len(clips) == 1, "clips=%r" % clips)
    print("arrangement clip: %r" % (clips[:1],))
    notes = session.result("note.list", {"clip": clip_id(clips[0])}) if clips else {}
    count = len(notes.get("notes", [])) if isinstance(notes.get("notes"), list) else notes.get("count", -1)
    recorder.check("the landed clip carries the pattern's notes", count and count > 0, "notes=%r" % str(notes)[:200])
    recorder.check("the landed clip sounds in the arrangement", level(session, workdir, "midi-landed") > SOUND_DBFS, "")
    session.result("control.undo")
    recorder.check("one undo takes the landing back", not clips_on(session, tracks[0]), "clips=%r" % clips_on(session, tracks[0]))


def audio_workflow(session, workdir, recorder, tracks):
    tone, hush = os.path.join(workdir, "tone.wav"), os.path.join(workdir, "hush.wav")
    write_wav(tone, 0.5)
    write_wav(hush, 0.0)
    session.result("session.set_slot", {"track": 1, "scene": 0, "type": "audio", "source": tone, "name": "Tone"})
    session.result("session.launch_slot", {"track": 1, "scene": 0})
    recorder.check("the audio cell launches", wait_column(session, 1, "playing"), "columns=%r" % columns(session))
    recorder.check("the launched audio cell sounds", live(session, "audio-launched") > SOUND_DBFS, "")
    session.result("session.set_slot", {"track": 1, "scene": 0, "source": hush})
    recorder.check("the edit keeps the cell playing", columns(session).get(1, ("idle",))[0] == "playing",
                   "columns=%r" % columns(session))
    recorder.check("the playing cell sounds its new (silent) file at once",
                   live(session, "audio-edited") < SILENT_DBFS, "")
    session.result("control.undo")
    recorder.check("the undo puts the tone back in the playing cell", live(session, "audio-undone") > SOUND_DBFS, "")
    session.result("session.stop_slot", {"track": 1, "scene": 0})
    wait_column(session, 1, "idle")

    session.result("session.arrangement_record_arm", {"armed": True})
    session.result("session.launch_slot", {"track": 1, "scene": 0})
    wait_column(session, 1, "playing")
    time.sleep(1.0)
    session.result("session.stop_slot", {"track": 1, "scene": 0})
    wait_column(session, 1, "idle")
    landed = session.result("session.arrangement_record_land")
    session.result("session.arrangement_record_arm", {"armed": False})
    recorder.check("the audio performance lands one clip", landed.get("clips") == 1, "land=%r" % landed)
    clips = clips_on(session, tracks[1])
    recorder.check("the arrangement reports the landed sample clip", len(clips) == 1, "clips=%r" % clips)


def reload_check(session, workdir, recorder):
    before = session.result("session.get_state")
    path = os.path.join(workdir, "after.mmp")
    session.result("project.save", {"path": path})
    session.result("project.open", {"path": path})
    after = session.result("session.get_state")
    recorder.check("save and load keep every cell", after.get("slots") == before.get("slots"),
                   "before=%r after=%r" % (before.get("slots"), after.get("slots")))
    recorder.check("a reopened project launches nothing", not after.get("columns"), "columns=%r" % after.get("columns"))


def pattern_names(session):
    """{pattern number: its pattern track's name}, as session.pattern_list reports them."""
    return {int(p.get("pattern", -2)): p.get("name") for p in session.result("session.pattern_list").get("patterns", [])}


def pattern_track_of(session, name):
    """The track.list entries of the pattern track named @name."""
    return [t for t in session.result("track.list").get("tracks", []) if t.get("type") == "pattern" and t.get("name") == name]


def pattern_track_undo(session, recorder):
    """Deleting the pattern track a cell plays empties the cell; ONE control.undo brings the track and the cell back."""
    target = slot(session, 0, 0)
    name = pattern_names(session).get(int(target.get("pattern", -1)))
    tracks = pattern_track_of(session, name)
    recorder.check("the MIDI cell's pattern track is found", target.get("type") == "midi" and len(tracks) == 1,
                   "slot=%r tracks=%r" % (target, tracks))
    if len(tracks) != 1:
        return
    session.result("track.remove", {"track": tracks[0].get("id")})
    recorder.check("deleting the pattern track empties its cell", not slot(session, 0, 0), "%r" % slot(session, 0, 0))
    undone = session.result("control.undo")
    back = slot(session, 0, 0)
    played = pattern_names(session).get(int(back.get("pattern", -2)))
    recorder.check("one undo brings back the track and the cell playing it",
                   back.get("name") == target.get("name") and played == name, "undo=%r slot=%r" % (undone, back))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with tempfile.TemporaryDirectory(prefix="q050-489-") as workdir, H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        tracks = build_fixture(session, workdir, recorder)
        if tracks:
            # The cells are HEARD on the live master while the transport runs (render.render renders the saved
            # project, which by contract holds no launch state); the landed arrangement clips are rendered.
            session.result("meter.arm", {"enabled": True})
            session.result("transport.play")
            midi_workflow(session, workdir, recorder, tracks)
            audio_workflow(session, workdir, recorder, tracks)
            session.result("transport.stop")
            pattern_track_undo(session, recorder)
            reload_check(session, workdir, recorder)
        session.call("control.quit")
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("session transitions over the control socket")
        return 1
    H.ok("session transitions over the control socket (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
