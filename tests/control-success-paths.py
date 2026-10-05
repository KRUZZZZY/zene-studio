#!/usr/bin/env python3
# control-success-paths.py - the socket-side success paths R6.3 could not reach in-process
#
# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
"""Success paths that need a RUNNING instance, held to their result schemas.

tests/src/core/ControlSuccessPathsTest.cpp drives most of the commands the suite had only
ever refused (gate 1, tests/checked-coverage-unreached.txt). Two need what an in-process
test does not have:

  1. `session.arrangement_record_land` needs a PERFORMANCE: the audio thread pushes one event
     per launch and per stop while Arrangement Record is armed, so the engine must be running.
     A slot is launched and stopped while armed, then the land pass must succeed and write the
     clip the pair describes;
  2. `project.audible_diff` renders both files through the running binary as child processes,
     and in-process that binary is the test executable. A project compared with ITSELF must
     come back `differ: false` - an answer, not only a success;
  3. M3.2's File menu / toolbar verbs (project.new, save_as_template, import, export_midi,
     save_version, transport.set_metronome): the instance's HOME is a sandbox, which is what
     lets the default template be written without touching the user's;
  4. 0.5.0 L12's renders through the binary, for the reason (2) gives: project.subproject_add
     and project.subproject_refresh (a project FILE rendered and placed on a track, then
     rendered again), track.flatten (BounceInPlace's render replacing the track) and
     range.bounce (the selection's tracks rendered together onto a new track).

The instance is the shared harness's headless one, so every reply is checked against its own
resultSchema (ZENE_CONTROL_CHECK_RESULTS) and a reply that breaks it arrives as a typed error.

Usage: QT_QPA_PLATFORM=offscreen python3 control-success-paths.py <zene>
"""

import os
import struct
import sys
import time

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def check_arrangement_record_land(session, recorder):
    session.result("track.add", {"type": "instrument", "name": "Performed"})
    session.result("session.set_grid", {"tracks": 1, "scenes": 1})
    slot = session.result("session.set_slot", {"track": 0, "scene": 0, "type": "midi", "pattern": 1,
                                               "name": "take", "mode": "trigger",
                                               "quantisation": "none"})
    recorder.check("a slot is built to perform", "error" not in slot, "reply=%r" % slot)
    session.result("session.set_quantisation", {"quantisation": "none"})
    session.result("transport.play")
    armed = session.result("session.arrangement_record_arm", {"armed": True})
    recorder.check("Arrangement Record arms", "error" not in armed, "reply=%r" % armed)
    session.result("session.launch_slot", {"track": 0, "scene": 0, "quantisation": "none"})
    time.sleep(1.0)
    session.result("session.stop_all")
    time.sleep(0.5)
    status = session.result("session.arrangement_record_status")
    print("arrangement record status: %r" % status)
    landed = session.result("session.arrangement_record_land")
    print("land: %r" % landed)
    recorder.check("the land pass writes the one clip the launch/stop pair describes",
                   landed.get("clips") == 1 and landed.get("pairs") == 1, "reply=%r" % landed)
    session.result("session.arrangement_record_arm", {"armed": False})
    session.result("transport.stop")


def check_audible_diff(session, recorder, outdir):
    path = os.path.join(outdir, "same.mmp")
    session.result("project.save", {"path": path})
    same = session.result("project.audible_diff", {"a": path, "b": path, "mix_only": True})
    print("audible_diff(a, a): %r" % same)
    recorder.check("project.audible_diff succeeds", "error" not in same, "reply=%r" % same)
    recorder.check("a project compared with itself does not differ", same.get("differ") is False,
                   "differ=%r" % same.get("differ"))


def write_one_note_smf(path):
    """A format-0 Standard MIDI File: one track, one middle C of a quarter note."""
    track = bytes([0x00, 0x90, 60, 100, 0x60, 0x80, 60, 0, 0x00, 0xFF, 0x2F, 0x00])
    with open(path, "wb") as handle:
        handle.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 96))
        handle.write(b"MTrk" + struct.pack(">I", len(track)) + track)


def check_project_lifecycle(session, recorder, outdir):
    """M3.2: the File menu / toolbar verbs, each driven to success.

    The instance's HOME is the harness's own sandbox, so project.save_as_template
    writes the SANDBOX's default template, never the user's."""
    new = session.result("project.new")
    recorder.check("project.new replaces the session with one that has no file yet",
                   "error" not in new and new.get("file") == "", "reply=%r" % new)
    metronome = session.result("transport.set_metronome", {"enabled": True})
    recorder.check("transport.set_metronome switches the click on",
                   metronome.get("enabled") is True and metronome.get("previous") is False,
                   "reply=%r" % metronome)
    session.result("transport.set_metronome", {"enabled": False})
    template = session.result("project.save_as_template")
    recorder.check("project.save_as_template writes default.mpt",
                   str(template.get("path", "")).endswith("default.mpt")
                   and os.path.exists(template.get("path", "")), "reply=%r" % template)
    smf = os.path.join(outdir, "one-note.mid")
    write_one_note_smf(smf)
    imported = session.result("project.import", {"path": smf})
    recorder.check("project.import adds the MIDI file's track", (imported.get("tracks_added") or 0) >= 1,
                   "reply=%r" % imported)
    midi = session.result("project.export_midi", {"path": os.path.join(outdir, "notes")})
    recorder.check("project.export_midi writes a .mid", str(midi.get("path", "")).endswith(".mid")
                   and (midi.get("bytes") or 0) > 14, "reply=%r" % midi)
    first = os.path.join(outdir, "versioned.mmp")
    session.result("project.save", {"path": first})
    version = session.result("project.save_version")
    recorder.check("project.save_version writes the next version beside the first",
                   version.get("previous_file") == first and version.get("file") not in (None, first)
                   and os.path.exists(version.get("file") or ""), "reply=%r" % version)


def add_track_with_note(session, name, key):
    """An instrument track with one clip holding one quarter note: something a render can hear."""
    track = session.result("track.add", {"type": "instrument", "name": name}).get("track")
    clip = session.result("clip.add", {"track": track, "position": 0}).get("clip")
    session.result("note.add", {"clip": clip, "key": key, "position": 0, "length": 96, "velocity": 100})
    return track


def check_subprojects(session, recorder, outdir):
    """0.5.0 L12 (WF-045): a saved project becomes a subproject of another and is rendered again,
    each time through the running binary as a child process."""
    session.result("project.new")
    add_track_with_note(session, "Nested", 60)
    sub = os.path.join(outdir, "sub.mmp")
    session.result("project.save", {"path": sub})
    # Saved again under another name, the session is no longer the subproject's own file (which
    # would be refused as a project that contains itself).
    session.result("project.save", {"path": os.path.join(outdir, "host.mmp")})
    added = session.result("project.subproject_add", {"path": sub})
    audio = added.get("audio") or ""
    recorder.check("project.subproject_add renders the file onto a new track",
                   str(added.get("track", "")).startswith("trk-") and os.path.isfile(audio)
                   and os.path.getsize(audio) > 0, "reply=%r" % added)
    refreshed = session.result("project.subproject_refresh", {"track": added.get("track")})
    again = refreshed.get("audio") or ""
    recorder.check("project.subproject_refresh renders it again into a new file",
                   refreshed.get("track") == added.get("track") and again not in ("", audio)
                   and os.path.isfile(again), "reply=%r" % refreshed)


def check_track_flatten(session, recorder):
    """0.5.0 L12 (ED-207): BounceInPlace's render of a track replaces it with an audio track."""
    track = add_track_with_note(session, "Flattened", 62)
    flattened = session.result("track.flatten", {"track": track})
    recorder.check("track.flatten replaces the track with an audio track playing its render",
                   flattened.get("flattened") == track and str(flattened.get("track", "")).startswith("trk-")
                   and flattened.get("track") != track and os.path.isfile(flattened.get("path") or ""),
                   "reply=%r" % flattened)


def check_range_bounce(session, recorder, outdir):
    """0.5.0 L12 (ED-205): the selection's tracks over its ticks render onto a new audio track and
    the sources are muted, so the song sounds as before."""
    track = add_track_with_note(session, "Selected", 64)
    session.result("range.set", {"start": 0, "end": 192, "tracks": [track]})
    out = os.path.join(outdir, "selection.wav")
    bounced = session.result("range.bounce", {"out": out})
    recorder.check("range.bounce renders the selection onto a new track",
                   bounced.get("tracks") == 1 and str(bounced.get("landed_track", "")).startswith("trk-")
                   and os.path.isfile(bounced.get("path") or "") and (bounced.get("bytes") or 0) > 44,
                   "reply=%r" % bounced)
    state = session.result("track.get_state", {"track": track})
    recorder.check("range.bounce mutes the bounced track", state.get("muted") is True, "state=%r" % state)
    # Its undo finds the landed track by id: track.remove and its undo rebuild it as a NEW object, which a
    # guarded pointer no longer names (the undo used to leave the rebuilt track and the mute in place).
    landed = bounced.get("landed_track")
    session.result("track.remove", {"track": landed})
    session.result("control.undo")
    session.result("control.undo")
    gone = session.result("track.get_state", {"track": landed})
    state = session.result("track.get_state", {"track": track})
    recorder.check("range.bounce's undo takes the landed track away after its removal is undone",
                   "error" in gone and state.get("muted") is False, "landed=%r source=%r" % (gone, state))


def check_bounce_source_undo(session, recorder, outdir):
    """A bounce's source mute follows the track rebuilt by track.remove's undo."""
    track = add_track_with_note(session, "Bounce source", 65)
    bounced = session.result("bounce.land", {"track": track, "start": 0, "end": 192,
                                             "out": os.path.join(outdir, "bounce-source.wav")})
    landed = bounced.get("landed_track")
    recorder.check("bounce.land creates a track and mutes its source",
                   str(landed).startswith("trk-")
                   and session.result("track.get_state", {"track": track}).get("muted") is True,
                   "reply=%r" % bounced)
    # Wave 3's checked-command gap: exercise the editor through its public command on real bounced audio.
    tracks = session.result("arrangement.get_state").get("tracks", [])
    clips = next((entry.get("clips", []) for entry in tracks if entry.get("id") == landed), [])
    editor = session.result("window.sample_editor", {"clip": clips[0] if clips else "", "from": 441,
                                                     "to": 882, "unit": "samples", "zoom": "selection"})
    recorder.check("window.sample_editor opens the bounced clip with the requested range",
                   editor.get("from") == 441 and editor.get("to") == 882
                   and editor.get("length") == 441 and editor.get("unit") == "samples", "reply=%r" % editor)
    session.result("track.remove", {"track": track})
    session.result("control.undo")
    recorder.check("undo of source removal restores the bounced, muted source",
                   session.result("track.get_state", {"track": track}).get("muted") is True, "track=%r" % track)
    session.result("control.undo")
    recorder.check("bounce undo unmutes a rebuilt source and removes the landed track",
                   session.result("track.get_state", {"track": track}).get("muted") is False
                   and "error" in session.result("track.get_state", {"track": landed}), "track=%r landed=%r" % (track, landed))
    redone = session.result("control.redo")
    recorder.check("bounce redo mutes the rebuilt source again",
                   redone.get("redone") is True
                   and session.result("track.get_state", {"track": track}).get("muted") is True,
                   "reply=%r" % redone)
    session.result("control.undo")
    recorder.check("a second bounce undo restores the source's original mute",
                   session.result("track.get_state", {"track": track}).get("muted") is False, "track=%r" % track)


def check_window_shell(session, recorder, outdir):
    """M3.2/M3.4/M3.7: the main window's own verbs, on the harness's offscreen GUI instance.

    The VCA strip is exercised here too: a group with two members makes the mixer's VCA area
    appear (MixerView::syncWithMixer picks up socket-made channels and groups within 500 ms),
    and window.screenshot renders it - the image's size is asserted; its pixels were reviewed
    by hand when the strip landed (2026-09-29)."""
    first = session.result("mixer.add_channel").get("channel")
    second = session.result("mixer.add_channel").get("channel")
    group = session.result("vca.create", {"name": "Shell"}).get("group")
    for channel in (first, second):
        session.result("vca.assign", {"group": group, "channel": channel})
    session.result("window.toggle", {"editor": "mixer"})
    time.sleep(1.2)
    shot = os.path.join(outdir, "mixer.png")
    rendered = session.result("window.screenshot", {"path": shot})
    recorder.check("window.screenshot renders the main window to a PNG",
                   os.path.exists(shot) and (rendered.get("width") or 0) > 100, "reply=%r" % rendered)
    for command in ("window.detach_all", "window.attach_all", "window.settings", "app.about",
                    "window.command_palette"):
        reply = session.result(command)
        recorder.check("%s answers without holding the surface" % command, "error" not in reply,
                       "reply=%r" % reply)
    active = session.result("window.screenshot", {"path": os.path.join(outdir, "active.png"),
                                                  "window": "active"})
    recorder.check("window.screenshot renders the ACTIVE window too", "error" not in active,
                   "reply=%r" % active)
    full = session.result("window.fullscreen", {"enabled": True})
    recorder.check("window.fullscreen enters fullscreen", full.get("fullscreen") is True, "reply=%r" % full)
    session.result("window.fullscreen", {"enabled": False})


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
        check_arrangement_record_land(session, recorder)
        check_audible_diff(session, recorder, instance.tmp)
        check_project_lifecycle(session, recorder, instance.tmp)
        check_subprojects(session, recorder, instance.tmp)
        check_track_flatten(session, recorder)
        check_range_bounce(session, recorder, instance.tmp)
        check_bounce_source_undo(session, recorder, instance.tmp)
        check_window_shell(session, recorder, instance.tmp)
        session.call("control.quit")
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("socket-side success paths")
        return 1
    H.ok("socket-side success paths (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
