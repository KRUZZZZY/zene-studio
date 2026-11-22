#!/usr/bin/env python3
"""Q050-273.2 stage 2: a MIDI launcher cell saved into the shared library over the control socket, then
reused in another cell after the project is reopened - no window at any point (docs/050-features/Q050-273.2.md).

WHAT IT PROVES, on the real binary, through --control-socket:

  SAVE    a populated MIDI cell is saved with browser.save_clip {track, scene}: the reply names the .mid in
          <user samples>/Clips, its kind and its byte count, the file exists, and THIS SCRIPT'S OWN SMF
          parser - not the product's reader - finds every note of the pattern the cell references, with
          the same starts, lengths, keys and velocities. A second save without overwrite is REFUSED and
          leaves the file alone; control.undo removes it and redo writes it back (one true-inverse step,
          read back from control.transactions).
  READ    browser.query finds the entry under the samples root with kind midi; browser.tags counts it.
  REOPEN  the project is saved, a new project is made, the saved one is reopened, and the entry is STILL
          found by browser.query; session.import_midi_cell puts it in another cell as a playable pattern
          (the cell references a real pattern, the pattern track is named after the file), and saving THAT
          cell gives the same notes again. Its two edits undo one at a time: the file, then the cell and
          the pattern track.
  FAIL    a missing, empty, damaged, too-large or past-the-song file, a bad instrument, a bad address and a
          relative path are typed refusals; none adds a pattern, writes a file or changes a cell. A MIDI
          cell whose pattern has no notes is refused by browser.save_clip, as is an empty cell.

WHAT IT DOES NOT PROVE: rendering the imported cell through the engine (the live launcher tests own that);
the interface's menu and drag halves (tested in SessionLibraryMidiTest).

Usage: QT_QPA_PLATFORM=offscreen python3 control-session-library-midi.py <zene>
Exit 0 only when every check held.
"""

import os
import struct
import sys
import tempfile

import control_socket_harness as H

REQUEST_IDS = iter(range(1, 3000))

#: The fixture's notes: (start, length, key, velocity) in LMMS ticks (48 per quarter). The velocities are
#: chosen so the save/import volume mapping is exact: 64 -> 102 -> 64, 32 -> 51 -> 32, 100 -> 159 -> 100.
FIXTURE_NOTES = [(0, 48, 60, 64), (48, 24, 64, 32), (96, 96, 67, 100)]
FIXTURE_END = 192


class Session:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript

    def call(self, command, args=None):
        return self.client.call(next(REQUEST_IDS), command, args, transcript=self.transcript)

    def result(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}


class Recorder:
    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def report_results(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-72s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)


def varlen(value):
    groups = [value & 0x7F]
    value >>= 7
    while value:
        groups.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(groups))


def make_smf(notes, bpm=120, division=48):
    """A type-0 SMF written here, independently of the product's writer."""
    events = []
    for start, length, key, velocity in notes:
        events.append((start, 1, 0x90, key, velocity))
        events.append((start + length, 0, 0x80, key, 0))
    events.sort(key=lambda event: (event[0], event[1]))
    track = b"\x00\xff\x51\x03" + struct.pack(">I", 60000000 // bpm)[1:]
    at = 0
    for tick, _, status, key, velocity in events:
        track += varlen(tick - at) + bytes([status, key, velocity])
        at = tick
    track += b"\x00\xff\x2f\x00"
    return b"MThd" + struct.pack(">IHHH", 6, 0, 1, division) + b"MTrk" + struct.pack(">I", len(track)) + track


def read_varlen(data, at):
    value = 0
    for _ in range(4):
        byte = data[at]
        at += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, at
    raise ValueError("a variable-length quantity longer than four bytes")


def parse_smf(data):
    """(notes, end_tick) of a type-0/1 SMF, parsed here rather than by the product's reader.

    notes are (start, length, key, velocity) in ticks; running status, note-on velocity 0 and meta events
    are handled. A note never closed is closed at its track's end, as the reader does."""
    if len(data) < 14 or data[0:4] != b"MThd":
        raise ValueError("not a Standard MIDI File")
    header = struct.unpack(">IHHH", data[4:14])
    if header[0] < 6 or header[3] & 0x8000:
        raise ValueError("bad header")
    division = header[3]
    at = 8 + header[0]
    notes = []
    end_tick = 0
    for _ in range(header[2]):
        if data[at:at + 4] != b"MTrk":
            raise ValueError("no track chunk")
        length = struct.unpack(">I", data[at + 4:at + 8])[0]
        end = at + 8 + length
        at += 8
        tick = 0
        status = 0
        open_notes = {}
        last_note_tick = 0
        while at < end:
            delta, at = read_varlen(data, at)
            tick += delta
            last_note_tick = tick
            byte = data[at]
            if byte == 0xFF:
                kind = data[at + 1]
                size, at = read_varlen(data, at + 2)
                at += size
                if kind == 0x2F:
                    last_note_tick = tick
                continue
            if byte in (0xF0, 0xF7):
                size, at = read_varlen(data, at + 1)
                at += size
                continue
            if byte & 0x80:
                status = byte
                at += 1
            kind = status & 0xF0
            if kind in (0x80, 0x90):
                key = data[at]
                velocity = data[at + 1]
                at += 2
                if kind == 0x80 or velocity == 0:
                    start, note_velocity = open_notes.pop(key, (None, 0))
                    if start is not None:
                        notes.append((start, tick - start, key, note_velocity))
                else:
                    open_notes[key] = (tick, velocity)
            elif kind in (0xA0, 0xB0, 0xE0):
                at += 2
            elif kind in (0xC0, 0xD0):
                at += 1
            else:
                raise ValueError("unsupported status %02x" % status)
        for key, (start, velocity) in open_notes.items():
            notes.append((start, max(1, last_note_tick - start), key, velocity))
        end_tick = max(end_tick, tick)
        at = end
    notes.sort(key=lambda note: (note[0], note[2], note[1], note[3]))
    return notes, end_tick


def slot(session, track, scene):
    for cell in session.result("session.get_state").get("slots", []):
        if int(cell.get("track", -1)) == track and int(cell.get("scene", -1)) == scene:
            return cell
    return {}


def patterns(session):
    return session.result("session.pattern_list").get("patterns", [])


def samples_root(session):
    for root in session.result("browser.roots").get("roots", []):
        if root.get("id") == "samples":
            return root.get("path", "")
    return ""


def files_in(directory):
    try:
        return sorted(os.listdir(directory))
    except OSError:
        return []


def start(binary):
    instance = H.start_instance(binary)
    transcript = H.Transcript()
    H.wait_for_socket(instance)
    client = H.connect(instance)
    H.wait_ready(instance, client, transcript)
    return instance, Session(client, transcript), transcript


def save_notes(session, recorder, track, scene, name, tags=None):
    args = {"track": track, "scene": scene, "name": name}
    if tags is not None:
        args["tags"] = tags
    saved = session.result("browser.save_clip", args)
    path = saved.get("path", "")
    recorder.check("browser.save_clip saves the %s cell" % ("reused" if tags is None else "MIDI"),
                   "error" not in saved and path.endswith(".mid"), "%r" % (saved,))
    recorder.check("the reply says the entry is midi", saved.get("kind") == "midi", "%r" % (saved.get("kind"),))
    recorder.check("the reply's byte count is the file's",
                   int(saved.get("bytes", -1)) == (os.path.getsize(path) if os.path.isfile(path) else -2),
                   "%r" % (saved.get("bytes"),))
    return saved, path


def parse_file(path):
    with open(path, "rb") as handle:
        return parse_smf(handle.read())


def drive_refusals(session, recorder, workdir):
    """Every refusal a MIDI save or import can make without an environment seam."""
    def refused(name, command, args, kind="invalid_args", message=None):
        before_patterns = patterns(session)
        before_slots = session.result("session.get_state").get("slots", [])
        reply = session.call(command, args)
        error = reply.get("error") or {}
        held = reply.get("ok") is False and error.get("kind") == kind
        if message is not None:
            held = held and message in (error.get("message") or "")
        recorder.check(name, held, "%r" % (reply,))
        recorder.check(name + " changes nothing", patterns(session) == before_patterns
                       and session.result("session.get_state").get("slots", []) == before_slots, "")

    refused("an empty cell is refused by browser.save_clip", "browser.save_clip", {"track": 1, "scene": 0},
            kind="refused")
    refused("a MIDI pattern with no notes is refused by browser.save_clip", "browser.save_clip",
            {"track": 0, "scene": 1}, kind="refused", message="no notes")

    missing = os.path.join(workdir, "missing.mid")
    empty = os.path.join(workdir, "empty.mid")
    damaged = os.path.join(workdir, "damaged.mid")
    huge = os.path.join(workdir, "huge.mid")
    no_notes = os.path.join(workdir, "no-notes.mid")
    past = os.path.join(workdir, "past-song.mid")
    open(empty, "wb").close()
    with open(damaged, "wb") as handle:
        handle.write(b"this is not a Standard MIDI File")
    with open(huge, "wb") as handle:
        handle.truncate(16 * 1024 * 1024 + 1)
    with open(no_notes, "wb") as handle:
        handle.write(make_smf([], division=48))
    with open(past, "wb") as handle:
        handle.write(make_smf([(9999 * 192, 48, 60, 64)]))

    base = {"track": 0, "scene": 0}
    refused("a missing MIDI file is not found", "session.import_midi_cell",
            dict(base, path=missing), kind="not_found")
    refused("a relative path is invalid args", "session.import_midi_cell",
            dict(base, path="relative.mid"), kind="invalid_args")
    refused("a directory is not a file", "session.import_midi_cell",
            dict(base, path=workdir), kind="invalid_args", message="not a file")
    refused("an empty file is invalid args", "session.import_midi_cell",
            dict(base, path=empty), kind="invalid_args", message="empty")
    refused("a damaged file is invalid args", "session.import_midi_cell",
            dict(base, path=damaged), kind="invalid_args", message="not imported")
    refused("a file over 16 MiB is invalid args", "session.import_midi_cell",
            dict(base, path=huge), kind="invalid_args", message="16 MiB")
    refused("a file with no notes is invalid args", "session.import_midi_cell",
            dict(base, path=no_notes), kind="invalid_args", message="no notes")
    refused("a file past the song's limit is invalid args", "session.import_midi_cell",
            dict(base, path=past), kind="invalid_args", message="past the song's limit")
    refused("an instrument that does not exist is not found", "session.import_midi_cell",
            dict(base, path=no_notes, instrument=99), kind="not_found")
    refused("an address outside the grid is invalid args", "session.import_midi_cell",
            {"track": 9, "scene": 0, "path": no_notes}, kind="invalid_args")
    refused("a missing path is invalid args", "session.import_midi_cell", base, kind="invalid_args")


def drive_save_and_read(session, recorder, fixture, saved_path):
    """The saved file through this script's own parser, then the entry's browser and undo behaviour."""
    notes, end_tick = parse_file(saved_path)
    recorder.check("the saved MIDI file holds every pattern note, start/length/key/velocity exact",
                   notes == FIXTURE_NOTES, "%r" % (notes,))
    recorder.check("the saved file's end is the pattern's end", end_tick >= FIXTURE_END, "%r" % end_tick)

    reply = session.result("browser.query", {"root": "samples", "text": "agent-groove.mid", "kind": "midi"})
    items = [item for item in reply.get("items", []) if item.get("name") == "agent-groove.mid"]
    recorder.check("browser.query finds the saved entry under the samples root",
                   "error" not in reply and len(items) == 1 and items[0].get("path") == saved_path,
                   "%r" % (reply,))
    tags = session.result("browser.tags").get("tags", [])
    tagged = [entry for entry in tags if entry.get("tag") == "midi-roundtrip"]
    recorder.check("browser.tags counts the saved file under its tag",
                   bool(tagged) and int(tagged[0].get("files", 0)) >= 1, "%r" % (tagged[:1],))

    before = open(saved_path, "rb").read()
    refused = session.call("browser.save_clip", {"track": 0, "scene": 0})
    error = refused.get("error") or {}
    recorder.check("a second save without overwrite is refused", refused.get("ok") is False
                   and error.get("kind") == "refused", "%r" % (refused,))
    recorder.check("the refused save left the file alone",
                   os.path.isfile(saved_path) and open(saved_path, "rb").read() == before, saved_path)

    undone = session.result("control.undo")
    recorder.check("control.undo removes the library file", "error" not in undone and not os.path.exists(saved_path),
                   "undo=%r exists=%r" % (undone, os.path.exists(saved_path)))
    redone = session.result("control.redo")
    recorder.check("control.redo writes it back", "error" not in redone and os.path.isfile(saved_path),
                   "redo=%r" % (redone,))
    notes_again, _ = parse_file(saved_path)
    recorder.check("redo wrote the same notes", notes_again == FIXTURE_NOTES, "%r" % (notes_again,))

    transactions = session.result("control.transactions").get("transactions", [])
    saved_step = next((entry for entry in reversed(transactions)
                       if entry.get("command") == "browser.save_clip"), {})
    imported_step = next((entry for entry in reversed(transactions)
                          if entry.get("command") == "session.import_midi_cell"), {})
    recorder.check("the save is recorded as a true_inverse browser.save_clip step",
                   saved_step.get("class") == "true_inverse" and saved_step.get("reversible") is True,
                   "%r" % (saved_step,))
    recorder.check("the import is recorded as a true_inverse session.import_midi_cell step",
                   imported_step.get("class") == "true_inverse" and imported_step.get("reversible") is True,
                   "%r" % (imported_step,))


def drive_reopen(session, recorder, workdir, saved_path, fixture):
    """Save, reopen, import the entry into another cell, save that cell, compare, then undo twice."""
    project = os.path.join(workdir, "midi-library-cell.mmp")
    recorder.check("the project saves", "error" not in session.result("project.save", {"path": project}), project)
    recorder.check("a new project starts empty", "error" not in session.result("project.new"), "")
    opened = session.result("project.open", {"path": project})
    recorder.check("the project reopens", "error" not in opened, "%r" % opened)
    recorder.check("the reopened cell still references its pattern",
                   slot(session, 0, 0).get("type") == "midi" and int(slot(session, 0, 0).get("pattern", -1)) >= 0,
                   "%r" % slot(session, 0, 0))
    reply = session.result("browser.query", {"root": "samples", "text": "agent-groove.mid", "kind": "midi"})
    items = [item for item in reply.get("items", []) if item.get("name") == "agent-groove.mid"]
    recorder.check("after the reopen the entry is still browsable",
                   "error" not in reply and len(items) == 1 and items[0].get("path") == saved_path,
                   "%r" % (reply,))

    before_patterns = len(patterns(session))
    imported = session.result("session.import_midi_cell", {"track": 1, "scene": 1, "path": saved_path,
                                                           "name": "agent-reuse"})
    recorder.check("the saved entry goes into another cell", "error" not in imported, "%r" % (imported,))
    recorder.check("the import made one pattern and wrote three notes",
                   int(imported.get("notes", -1)) == 3 and len(patterns(session)) == before_patterns + 1,
                   "%r" % (imported,))
    reused = slot(session, 1, 1)
    recorder.check("the other cell references the new pattern by name",
                   reused.get("type") == "midi" and reused.get("name") == "agent-reuse"
                   and int(reused.get("pattern", -1)) >= 0, "%r" % (reused,))
    named = [entry for entry in patterns(session) if int(entry.get("pattern", -1)) == int(reused.get("pattern", -2))]
    recorder.check("the pattern track is named after the imported file",
                   bool(named) and named[0].get("name") == "agent-reuse", "%r" % (named[:1],))

    saved, reused_path = save_notes(session, recorder, 1, 1, "agent-reuse", tags=["midi-roundtrip"])
    notes, _ = parse_file(reused_path)
    recorder.check("saving the reused cell gives the same notes again", notes == fixture, "%r" % (notes,))

    recorder.check("one undo removes the reused cell's library file",
                   "error" not in session.result("control.undo") and not os.path.exists(reused_path), reused_path)
    recorder.check("the next undo clears the cell and takes its pattern away",
                   "error" not in session.result("control.undo") and not slot(session, 1, 1)
                   and len(patterns(session)) == before_patterns, "%r" % (slot(session, 1, 1),))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    with tempfile.TemporaryDirectory(prefix="q050-273-2-") as workdir:
        instance, session, transcript = start(argv[1])
        try:
            recorder.check("the default project is made", "error" not in session.result("project.new"), "")
            recorder.check("the grid is built",
                           "error" not in session.result("session.set_grid", {"tracks": 2, "scenes": 2}), "")
            # Pattern 0 exists and is empty: the two save refusals stage 2 keeps (empty cell, no notes).
            recorder.check("the MIDI control cell references the empty pattern",
                           "error" not in session.result("session.set_slot", {"track": 0, "scene": 1,
                                                                              "type": "midi", "pattern": 0}),
                           "%r" % (slot(session, 0, 1),))
            drive_refusals(session, recorder, workdir)

            fixture = make_smf(FIXTURE_NOTES)
            fixture_path = os.path.join(workdir, "fixture.mid")
            with open(fixture_path, "wb") as handle:
                handle.write(fixture)
            imported = session.result("session.import_midi_cell", {"track": 0, "scene": 0, "path": fixture_path,
                                                                   "name": "agent-groove"})
            recorder.check("a Standard MIDI File imports into a launcher cell",
                           "error" not in imported and int(imported.get("notes", -1)) == 3,
                           "%r" % (imported,))
            cell = slot(session, 0, 0)
            recorder.check("the cell now plays the new pattern, named from the argument",
                           cell.get("type") == "midi" and cell.get("name") == "agent-groove"
                           and int(cell.get("pattern", -1)) >= 0, "%r" % (cell,))
            named = [entry for entry in patterns(session)
                     if int(entry.get("pattern", -1)) == int(cell.get("pattern", -2))]
            recorder.check("the new pattern track carries the same name",
                           bool(named) and named[0].get("name") == "agent-groove", "%r" % (named[:1],))

            saved, saved_path = save_notes(session, recorder, 0, 0, "agent-groove", tags=["midi-roundtrip"])
            drive_save_and_read(session, recorder, fixture, saved_path)
            drive_reopen(session, recorder, workdir, saved_path, FIXTURE_NOTES)
            session.call("control.quit")
        finally:
            instance.close()
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("a MIDI launcher cell in the library over the control socket")
        return 1
    H.ok("a MIDI launcher cell in the library over the control socket (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
