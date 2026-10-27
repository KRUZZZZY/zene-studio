#!/usr/bin/env python3
"""ControlMidiTakeRecord - Q050-054.1: MIDI recorded into an armed instrument track by transport.record.

A real control-socket transcript against the real `zene` binary. A known MIDI phrase is PLAYED into the
running engine by the host's ALSA sequencer (`aplaymidi`, an external process, exactly like a keyboard)
straight into the instrument track's own sequencer port, while transport.record records an audio clip
alongside. Each check is a measured effect read back over the socket, not a success report:

  * ARM - track.set_midi_arm arms the instrument track (mode state) and refuses an audio track;
  * RECORD ALONGSIDE AUDIO - transport.record names the armed track in 'midi_tracks' while the armed
    sample clip records; Stop leaves ONE MIDI clip, which record.midi_capture_status reports and
    note.list reads back: the phrase's keys, its velocities (as the track hears them), and its timing -
    the gaps between the notes and their lengths, in song ticks, to within the jitter of a real-time
    sequencer (positions are stamped per audio period);
  * ONE UNDO STEP - control.undo takes the MIDI take off (undone_command record.midi_capture_commit),
    control.redo puts it back with the same notes;
  * LOOP PASSES - with the loop on, a note in each of two passes becomes two take clips over the loop,
    on two take lanes of one take group, and comp.get_state reports the NEWER pass as the active take.

It reports ctest *Skipped* (exit 77) - never *Passed* - when the host cannot supply real MIDI input (no
`aconnect`/`aplaymidi`, the engine came up on another MIDI client, or the track opened no sequencer
port), the ControlRetroCapture precedent: a test that cannot make its measurement must not say it did.

Usage: QT_QPA_PLATFORM=offscreen python3 control-midi-take-record.py <zene-binary>
"""

import os
import re
import struct
import subprocess
import sys
import time

from control_socket_harness import (Instance, Problems, Transcript, connect,
                                    finish, ok_result, wait_ready)

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "data", "agent-control-fixture.mmp")
SEQUENCER = "ALSA-Sequencer"
TRACK_NAME = "Control Target"
# The phrase: (file tick, key, velocity, length) at 480 ppq and 120 bpm = 960 file ticks a second. The
# song runs at 120 bpm too, 48 song ticks a beat: a file beat (480) is 48 song ticks.
PHRASE = ((240, 60, 100, 240), (720, 64, 80, 240), (1200, 67, 127, 480))
SONG_TICKS_PER_FILE_TICK = 48.0 / 480.0
# Song ticks: ~60 ms of sequencer and period jitter at 120 bpm. Measured: exact on an idle box; on one run of
# seven at load average 7 every note-on was stamped 3-4 ticks early (lengths 29/28/49 for 24/24/48).
TOLERANCE = 6
# The loop is the fixture's one bar [0, 192) = 2 s at 120 bpm: one note early in each of two passes.
LOOP_PHRASE = ((480, 72, 100, 240), (2400, 74, 100, 240))
LOOP_SECONDS = 3.2
BASE_VELOCITY = 127  # the fixture's <midiport basevelocity>: a note plays at velocity * 100 / 127


def vlq(value):
    out = bytearray([value & 0x7F])
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


def write_smf(path, notes):
    """A format-0 file at 120 bpm whose notes sit at the absolute file ticks given."""
    body = bytearray(b"\x00\xff\x51\x03" + bytes((0x07, 0xA1, 0x20)))
    cursor = 0
    for tick, key, velocity, length in notes:
        body += vlq(tick - cursor) + bytes((0x90, key, velocity))
        body += vlq(length) + bytes((0x80, key, 0))
        cursor = tick + length
    body += b"\x00\xff\x2f\x00"
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480)
    with open(path, "wb") as handle:
        handle.write(header + b"MTrk" + struct.pack(">I", len(body)) + bytes(body))


def tool(name):
    found = subprocess.run(["sh", "-c", "command -v %s" % name], capture_output=True, text=True)
    return found.stdout.strip() or None


def engine_client(aconnect, pid):
    """The ALSA client number the engine (by pid) opened, or None."""
    listing = subprocess.run([aconnect, "-l"], capture_output=True, text=True)
    for line in listing.stdout.splitlines():
        head = re.match(r"^client\s+(\d+):\s+'.*'\s+\[type=user(?:,pid=(\d+))?\]", line)
        if head and head.group(2) == str(pid):
            return int(head.group(1))
    return None


def track_port(aplaymidi, client, name):
    """The (client, port) of the engine's WRITABLE port named after the track, or None."""
    listing = subprocess.run([aplaymidi, "-l"], capture_output=True, text=True)
    for line in listing.stdout.splitlines():
        row = re.match(r"^\s*(\d+):(\d+)\s+.*?\s{2,}(.+?)\s*$", line)
        if row and int(row.group(1)) == client and row.group(3) == name:
            return int(row.group(1)), int(row.group(2))
    return None


def skipped(reason, instance=None):
    if instance is not None:
        instance.close()
    print("SKIPPED: %s" % reason)
    sys.exit(77)


class Session:
    """A control client that numbers its own requests and keeps a transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last = 0

    def call(self, cmd, args=None):
        self.last += 1
        return self.client.call(self.last, cmd, args or {}, transcript=self.transcript)

    def ok(self, cmd, args=None):
        return ok_result(self.call(cmd, args), self.last)


def project(instance, name, loop):
    """The fixture at 120 bpm, its MIDI ports readable (so the track opens a sequencer input port),
    the loop [0, 192) on or off."""
    with open(FIXTURE) as handle:
        text = handle.read()
    text = text.replace('readable="0"', 'readable="1"').replace('bpm="140"', 'bpm="120"')
    text = text.replace('lpstate="0"', 'lpstate="%d"' % (1 if loop else 0))
    path = os.path.join(instance.tmp, name)
    with open(path, "w") as handle:
        handle.write(text)
    return path


def open_project(context, name, loop):
    session = context["session"]
    opened = session.ok("project.open", {"path": project(context["instance"], name, loop)})
    if opened.get("loaded_with_errors"):
        skipped("the fixture did not load cleanly", context["instance"])
    tracks = session.ok("track.list").get("tracks") or []
    ids = [t["id"] for t in tracks if t.get("name") == TRACK_NAME]
    if not ids:
        skipped("the fixture has no %r track" % TRACK_NAME, context["instance"])
    context["track"] = ids[0]
    port = None
    deadline = time.time() + 5
    while port is None and time.time() < deadline:
        # The engine's ALSA client is listed once it has a port - the readable track's.
        client = engine_client(context["aconnect"], context["instance"].process.pid)
        port = track_port(context["aplaymidi"], client, TRACK_NAME) if client is not None else None
        time.sleep(0.1)
    if port is None:
        skipped("the engine opened no sequencer port named %r" % TRACK_NAME, context["instance"])
    context["port"] = port


def play(context, notes, name, problems):
    path = os.path.join(context["instance"].tmp, name)
    write_smf(path, notes)
    done = subprocess.run([context["aplaymidi"], "-p", "%d:%d" % context["port"], path],
                          capture_output=True, text=True, timeout=60)
    problems.require(done.returncode == 0, "aplaymidi exited %d: %s" % (done.returncode, done.stderr.strip()))


def clip_ids(session):
    return set(c["id"] for c in session.ok("arrangement.get_state")["clips"])


def note_rows(session, clip):
    rows = session.ok("note.list", {"clip": clip}).get("notes") or []
    return sorted((int(r["position"]), int(r["length"]), int(r["key"]), int(r["velocity"])) for r in rows)


# ---------------------------------------------------------------------------
# the measurements
# ---------------------------------------------------------------------------


def check_arm(context, problems):
    session = context["session"]
    armed = session.ok("track.set_midi_arm", {"track": context["track"], "armed": True})
    problems.require(armed["armed"] is True and armed["changed"] is True, "arming replied %r" % armed)
    audio = session.ok("track.add", {"type": "sample", "name": "Audio alongside"})["track"]
    context["audio_track"] = audio
    refused = session.call("track.set_midi_arm", {"track": audio, "armed": True})
    problems.require((refused.get("error") or {}).get("kind") == "refused",
                     "arming an audio track for MIDI was not refused: %r" % refused)
    status = session.ok("record.midi_capture_status")
    problems.require([t["track"] for t in status["armed_tracks"]] == [context["track"]],
                     "record.midi_capture_status lists %r armed" % status["armed_tracks"])


def record_phrase(context, problems):
    """Arms an audio clip, records the phrase alongside it and returns the commit's report."""
    session = context["session"]
    audio_clip = session.ok("clip.add", {"track": context["audio_track"], "position": 0})["clip"]
    context["audio_clip"] = audio_clip
    session.ok("clip.set_record", {"clip": audio_clip, "record": True})
    session.ok("transport.seek", {"ticks": 0})
    context["before"] = clip_ids(session)
    started = session.ok("transport.record")
    problems.require(started["recording"] is True, "transport.record replied %r" % started)
    problems.require(started.get("midi_tracks") == [context["track"]],
                     "transport.record names %r as recording MIDI" % started.get("midi_tracks"))
    play(context, PHRASE, "phrase.mid", problems)
    time.sleep(0.5)
    session.ok("transport.stop")
    return session.ok("record.midi_capture_status")["last_commit"]


def check_phrase(rows, problems):
    """The keys, velocities, gaps and lengths read back match the phrase played."""
    problems.require([r[2] for r in rows] == [n[1] for n in PHRASE], "keys %r, played %r" % (rows, PHRASE))
    problems.require([r[3] for r in rows] == [n[2] * 100 // BASE_VELOCITY for n in PHRASE],
                     "velocities %r for the played %r" % ([r[3] for r in rows], [n[2] for n in PHRASE]))
    if len(rows) != len(PHRASE):
        return
    for i in range(1, len(PHRASE)):
        gap = rows[i][0] - rows[i - 1][0]
        expected = (PHRASE[i][0] - PHRASE[i - 1][0]) * SONG_TICKS_PER_FILE_TICK
        problems.require(abs(gap - expected) <= TOLERANCE, "gap %d before note %d, played %g" % (gap, i, expected))
    for row, note in zip(rows, PHRASE):
        expected = note[3] * SONG_TICKS_PER_FILE_TICK
        problems.require(abs(row[1] - expected) <= TOLERANCE, "length %d, played %g" % (row[1], expected))


def audio_clip_armed(session, clip):
    return any(c.get("record") for c in session.ok("arrangement.get_state")["clips"] if c["id"] == clip)


def check_audio_alongside(context, problems):
    """The armed sample clip is still there and recorded the same pass."""
    session = context["session"]
    audio = [c for c in session.ok("arrangement.get_state")["clips"] if c["id"] == context["audio_clip"]]
    problems.require(len(audio) == 1, "the audio clip recorded alongside is gone: %r" % audio)
    # The armed sample clip recorded the same pass: installing its take disarms it (SampleClip::acceptRecordedTake).
    deadline = time.time() + 3
    while audio_clip_armed(session, context["audio_clip"]) and time.time() < deadline:
        time.sleep(0.1)
    disarm = session.ok("clip.set_record", {"clip": context["audio_clip"], "record": False})
    print("  the audio clip after the pass: %r" % disarm)
    problems.require(disarm.get("record_before") is False,
                     "the audio clip is still armed after the pass: its take was not installed (%r)" % disarm)


def check_record(context, problems):
    session = context["session"]
    commit = record_phrase(context, problems)
    takes = commit.get("takes") or []
    problems.require(len(takes) == 1 and takes[0]["track"] == context["track"],
                     "the commit reports %r" % commit)
    if not takes:
        return
    clip = takes[0]["clip"]
    context["midi_clip"] = clip
    after = clip_ids(session)
    problems.require(after - context["before"] == {clip},
                     "the arrangement gained %r, the commit names %r" % (sorted(after - context["before"]), clip))
    problems.require(commit.get("loop_recording") is False and takes[0]["lane"] == -1,
                     "a plain pass was reported as %r" % takes[0])
    rows = note_rows(session, clip)
    context["rows"] = rows
    print("  the take %s holds %r (position, length, key, velocity)" % (clip, rows))
    check_phrase(rows, problems)
    check_audio_alongside(context, problems)


def check_undo(context, problems):
    session = context["session"]
    clip = context.get("midi_clip")
    if clip is None:
        problems.add("no MIDI take to undo")
        return
    undone = session.ok("control.undo")
    problems.require(undone.get("undone_command") == "record.midi_capture_commit",
                     "control.undo unwound %r" % undone.get("undone_command"))
    problems.require(clip not in clip_ids(session), "the take %s is still there after one undo" % clip)
    redone = session.ok("control.redo")
    problems.require(redone.get("redone") is not False, "control.redo replied %r" % redone)
    problems.require(clip in clip_ids(session), "control.redo did not bring %s back" % clip)
    problems.require(note_rows(session, clip) == context["rows"], "the redone take holds %r" % note_rows(session, clip))


def check_loop(context, problems):
    session = context["session"]
    open_project(context, "loop.mmp", True)
    session.ok("track.set_midi_arm", {"track": context["track"], "armed": True})
    session.ok("transport.seek", {"ticks": 0})
    session.ok("transport.record")
    started = time.time()
    play(context, LOOP_PHRASE, "loop.mid", problems)
    time.sleep(max(0.0, LOOP_SECONDS - (time.time() - started)))
    session.ok("transport.stop")
    commit = session.ok("record.midi_capture_status")["last_commit"]
    takes = commit.get("takes") or []
    print("  the loop recording left %r" % takes)
    problems.require(commit.get("loop_recording") is True, "not reported as a loop recording: %r" % commit)
    problems.require(len(takes) == 2, "two passes left %d takes" % len(takes))
    if len(takes) != 2:
        return
    problems.require(takes[0]["group"] == takes[1]["group"] and takes[0]["group"] >= 0,
                     "the passes are not one take group: %r" % takes)
    problems.require(takes[0]["lane"] != takes[1]["lane"], "the passes share a lane: %r" % takes)
    problems.require(all(t["position"] == 0 and t["length"] == 192 for t in takes), "the takes do not span the loop: %r" % takes)
    keys = [note_rows(session, t["clip"])[0][2] for t in sorted(takes, key=lambda t: t["pass"])]
    problems.require(keys == [72, 74], "the passes hold %r" % keys)
    comp = session.ok("comp.get_state", {"track": context["track"]})
    group = (comp.get("take_groups") or [{}])[0]
    newest = max(takes, key=lambda t: t["pass"])
    problems.require(group.get("active_lane") == newest["lane"] and group.get("user_chosen") is False,
                     "the active take is lane %r, the newest pass is on lane %r" % (group.get("active_lane"), newest["lane"]))


def step(name, function, context):
    problems = Problems()
    try:
        function(context, problems)
    except AssertionError as error:
        problems.add("a command failed: %s" % error)
    print("%-58s %s" % (name, "ok" if not problems else "FAILED (%d)" % len(problems.items)))
    for item in problems.items:
        print("      %s" % item)
    return (name, not problems, problems.items)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    aconnect, aplaymidi = tool("aconnect"), tool("aplaymidi")
    if aconnect is None or aplaymidi is None:
        skipped("no ALSA-sequencer tooling on this host (aconnect=%r aplaymidi=%r)" % (aconnect, aplaymidi))
    instance = Instance(sys.argv[1])
    transcript = Transcript()
    instance.spawn()
    results = []
    try:
        client = connect(instance)
        wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        running = session.ok("midi.retro_capture_status")
        if not str(running["client"]).startswith(SEQUENCER):
            skipped("the engine runs the %r MIDI client, not the ALSA sequencer" % running["client"], instance)
        context = {"session": session, "instance": instance, "aplaymidi": aplaymidi, "aconnect": aconnect}
        open_project(context, "plain.mmp", False)
        print("source   : aplaymidi -> %d:%d (%s)" % (context["port"][0], context["port"][1], TRACK_NAME))
        results.append(step("track.set_midi_arm arms the instrument track", check_arm, context))
        results.append(step("recorded alongside audio, the take and its notes read back", check_record, context))
        results.append(step("one control.undo / control.redo covers the take", check_undo, context))
        results.append(step("loop passes become take lanes, the newest active", check_loop, context))
    finally:
        instance.close()
    transcript.dump()
    return finish(results)


if __name__ == "__main__":
    sys.exit(main())
