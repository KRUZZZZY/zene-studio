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
     lets the default template be written without touching the user's.

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
