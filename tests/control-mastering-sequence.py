#!/usr/bin/env python3
"""Q050-317.1: a mastering release sequence created, edited and reopened over the control socket
of the REAL binary.

THE CLAIM UNDER TEST, in one sentence: an agent connected to a real `zene` instance can create a
mastering document, add two audio-file songs, edit their order, processing, gap and fades through
the registered `mastering.song_*` commands, save the project and open it again - and every source
reference, the order, the processing, the gap and both fades read back exactly as they were.

WHY A REGISTERED CTEST AND NOT ONLY THE QtTest. `tests/src/core/MasteringSequenceTest.cpp` holds
the state machine to account in process. What it cannot prove is the release contract's agent
half: that the commands are REACHABLE THROUGH THE SOCKET, with schema validation, the wire field
names an external client actually gets, and the A16 transaction/undo behaviour the registry
records. So this starts the real binary (the ControlSocketIntegration mould: `$<TARGET_FILE:zene>`,
QT_QPA_PLATFORM=offscreen, the shared control_socket_harness with its own HOME/XDG world), opens a
mastering document through project.new_document, and drives the whole acceptance through the
socket.

WHAT IT ASSERTS, against the wire and the saved file:

  * the group is registered: control.commands_list carries mastering.sequence_list and the four
    mastering.song_* verbs, each with an args schema, the reads declaring mutating:false and the
    edits mutating:true, and every one carrying its A16 class in control.transactions after a call;
  * project.new_document {type: "mastering"} opens a mastering document whose release is empty;
  * two audio-file songs are added by absolute path, the second one after the first, and the read
    back names both sources in release order with their ids;
  * order, processing, gap and fades are changed through song_set / song_move, and each change is
    ONE undo step that control.undo reverses exactly (a reorder and a field edit, each measured);
  * a refused edit is typed and leaves the list untouched;
  * the release survives project.save then project.open: the <mastering-sequence> element is in the
    saved XML (parsed, not grepped), and every field the acceptance names is unchanged - the order
    included, and the two sources resolved to the files this test wrote;
  * the same is true of a project that never had a release: it saves with no element and opening it
    clears a release the session had.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-mastering-sequence.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import math
import os
import struct
import sys
import wave
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402


class Session:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 500

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args or {}, transcript=self.transcript)

    def ok(self, command, args=None):
        return H.ok_result(self.call(command, args), self.last_id)

    def typed(self, command, args=None):
        reply = self.call(command, args)
        return None if reply.get("ok") else reply.get("error", {}).get("kind")

    def command_entry(self, command):
        """One command's entry out of control.commands_list."""
        for entry in self.ok("control.commands_list").get("commands") or []:
            if entry.get("id") == command:
                return entry
        return {}


class Recorder:
    """Names each check and its evidence, so a failure says WHAT was checked and WHAT was seen."""

    def __init__(self, problems):
        self.problems = problems
        self.checks = 0

    def require(self, name, passed, evidence):
        self.checks += 1
        if not passed:
            self.problems.add("%s -- %s" % (name, evidence))


def plugin_env(binary):
    plugin_dir = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return {"LMMS_PLUGIN_DIR": plugin_dir} if os.path.isdir(plugin_dir) else None


def write_tone(path, frequency):
    """A real 16-bit mono WAV: the songs must be files the build's decoder accepts."""
    rate = 44100
    frames = rate  # one second is plenty for a reference
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        data = bytearray()
        for i in range(frames):
            value = int(0.4 * 32767.0 * math.sin(2.0 * math.pi * frequency * i / rate))
            data += struct.pack("<h", value)
        handle.writeframes(bytes(data))
    return path


def row_of(reply):
    return {song.get("source"): song for song in reply.get("songs") or []}


def release_state(session):
    """The acceptance's fields, in release order, as one comparable list of tuples."""
    listed = session.ok("mastering.sequence_list")
    state = []
    for song in listed.get("songs") or []:
        state.append((song.get("song"), os.path.realpath(song.get("source") or ""),
                      song.get("processing"), song.get("gap_ms"), song.get("fade_in_ms"),
                      song.get("fade_out_ms")))
    return state


def check_registration(session, recorder):
    listed = {entry.get("id") for entry in session.ok("control.commands_list").get("commands") or []}
    wanted = {"mastering.sequence_list", "mastering.song_add", "mastering.song_set",
              "mastering.song_move", "mastering.song_remove"}
    recorder.require("control.commands_list carries the five release-sequence ids",
                   wanted <= listed, "missing=%s" % sorted(wanted - listed))
    reads = session.command_entry("mastering.sequence_list")
    recorder.require("mastering.sequence_list is a read with a result schema",
                   reads.get("mutating") is False and reads.get("result_schema"),
                   "mutating=%r schema=%r" % (reads.get("mutating"), bool(reads.get("result_schema"))))
    for command in ("mastering.song_add", "mastering.song_set", "mastering.song_move", "mastering.song_remove"):
        entry = session.command_entry(command)
        schema = (entry.get("args_schema") or {}).get("required") or []
        recorder.require("%s declares itself an edit with a schema" % command,
                       entry.get("mutating") is True and bool(entry.get("args_schema")) and bool(schema),
                       "mutating=%r required=%r" % (entry.get("mutating"), schema))


def check_empty_document(session, recorder):
    session.ok("project.new_document", {"type": "mastering"})
    listed = session.ok("mastering.sequence_list")
    recorder.require("a new mastering document starts an empty release",
                   listed.get("songs") == [] and listed.get("count") == 0
                   and listed.get("document_type") == "mastering",
                   "listed=%r" % listed)
    names = listed.get("processing_names") or []
    recorder.require("the page names the engine's own processing choices",
                   names and names[0] == "none" and "streaming-14" in names and "ebu-r128" in names,
                   "names=%r" % (names,))


def add_two_songs(session, first, second):
    session.ok("mastering.song_add", {"source": first, "title": "Opening"})
    session.ok("mastering.song_add", {"source": second, "title": "Closing"})
    songs = session.ok("mastering.sequence_list").get("songs") or []
    return [song.get("song") for song in songs]


def real_sources(session):
    return [os.path.realpath(song.get("source") or "")
            for song in session.ok("mastering.sequence_list").get("songs") or []]


def row_of(session, song_id):
    for row in release_state(session):
        if row[0] == song_id:
            return row
    return None


def song_ids(session):
    return [row[0] for row in release_state(session)]


def check_two_songs(session, first, second, recorder):
    songs = session.ok("mastering.sequence_list").get("songs") or []
    recorder.require("two audio files are songs of the release, in the order added",
                   len(songs) == 2 and real_sources(session)
                   == [os.path.realpath(first), os.path.realpath(second)],
                   "sources=%r" % (real_sources(session),))
    recorder.require("the songs carry stable ids and report their file exists",
                   len(songs) == 2 and all(song.get("song") for song in songs)
                   and all(song.get("exists") for song in songs),
                   "songs=%r" % songs)
    recorder.require("the last song's gap is reported as not heard between songs",
                   [song.get("gap_applies") for song in songs] == [True, False],
                   "songs=%r" % songs)


def check_adds_are_recorded(session, recorder):
    """The A16 record of the last add is the group's declared class, and it is reversible."""
    records = session.ok("control.transactions").get("transactions") or []
    last = records[-1] if records else {}
    recorder.require("the add recorded one reversible true_inverse step",
                   last.get("command") == "mastering.song_add" and last.get("class") == "true_inverse"
                   and last.get("reversible") is True,
                   "record=%r" % last)


def set_field(session, song, **fields):
    args = {"song": song}
    args.update(fields)
    session.ok("mastering.song_set", args)


def check_both_edits_landed(session, first_id, second_id, recorder):
    first_row = row_of(session, first_id)
    second_row = row_of(session, second_id)
    recorder.require("processing, gap and fades are set per song",
                   first_row is not None and first_row[2:] == ("streaming-14", 3000, 1500, 2500)
                   and second_row is not None and second_row[2:] == ("ebu-r128", 0, 500, 4000),
                   "first=%r second=%r" % (first_row, second_row))


def check_edit_order(session, first_id, second_id, recorder):
    set_field(session, first_id, processing="streaming-14", fade_in_ms=1500, fade_out_ms=2500, gap_ms=3000)
    set_field(session, second_id, processing="ebu-r128", fade_in_ms=500, fade_out_ms=4000)
    check_both_edits_landed(session, first_id, second_id, recorder)
    moved = session.ok("mastering.song_move", {"song": second_id, "to": 0})
    recorder.require("the order changes through song_move, ids travelling with their songs",
                   song_ids(session) == [second_id, first_id],
                   "reply=%r ids=%r" % (moved, song_ids(session)))


def check_reorder_undo(session, recorder, second_id, first_id):
    session.ok("control.undo")
    recorder.require("one undo takes the reorder back",
                   song_ids(session) == [first_id, second_id], "ids=%r" % (song_ids(session),))


def check_field_undo(session, recorder, first_id, second_id):
    session.ok("control.undo")  # the second song's own field edit: one step for its four fields
    second_row = row_of(session, second_id)
    recorder.require("the second song's processing and fades are taken back as ONE step",
                   second_row is not None and second_row[2:] == ("none", 0, 0, 0),
                   "state=%r (the first song keeps its own edit)" % (row_of(session, second_id),))
    session.ok("control.undo")  # the first song's too
    first_row = row_of(session, first_id)
    recorder.require("the first song's four edited fields are taken back as ONE step",
                   first_row is not None and first_row[2:] == ("none", 0, 0, 0),
                   "state=%r" % (row_of(session, first_id),))


def check_edits_are_undoable(session, first, first_id, recorder):
    second_id = song_ids(session)[1]
    check_edit_order(session, first_id, second_id, recorder)
    check_reorder_undo(session, recorder, second_id, first_id)
    check_field_undo(session, recorder, first_id, second_id)
    check_edit_refusals(session, recorder, first, first_id)
    return second_id


def check_edit_refusals(session, recorder, first, first_id):
    for name, call, expected in (
            ("an unknown song is a typed not_found and changes nothing",
             lambda: session.typed("mastering.song_set", {"song": "song-999", "gap_ms": 10}), "not_found"),
            ("an unknown processing name is invalid_args",
             lambda: session.typed("mastering.song_set", {"song": first_id, "processing": "louder-please"}),
             "invalid_args"),
            ("a missing audio file is a typed not_found",
             lambda: session.typed("mastering.song_add",
                                   {"source": os.path.join(os.path.dirname(first), "gone.wav")}), "not_found")):
        refused = call()
        recorder.require(name, refused == expected, "kind=%r" % (refused,))


def set_release_for_saving(session, first, second_id):
    songs = session.ok("mastering.sequence_list").get("songs") or []
    first_id = [song.get("song") for song in songs
                if os.path.realpath(song.get("source")) == os.path.realpath(first)][0]
    session.ok("mastering.song_set", {"song": first_id, "processing": "streaming-16-dynamics",
                                      "fade_in_ms": 1200, "fade_out_ms": 3400, "gap_ms": 2500})
    session.ok("mastering.song_set", {"song": second_id, "processing": "streaming-14-ceiling-2",
                                      "fade_in_ms": 700, "fade_out_ms": 2100, "gap_ms": 900})
    # Put the second song first, so the saved ORDER is deliberately not the order of the adds.
    session.ok("mastering.song_move", {"song": second_id, "to": 0})


def saved_sequence(path):
    """The release the file states, in the same shape release_state() reports."""
    tree = ET.parse(path)
    element = tree.getroot().find(".//mastering-sequence")
    if element is None:
        return None
    return [("song-" + (child.get("id") or ""), os.path.realpath(child.get("source") or ""),
             child.get("processing") or "none", int(child.get("gap-ms") or 0),
             int(child.get("fade-in-ms") or 0), int(child.get("fade-out-ms") or 0))
            for child in list(element)]


def check_save_and_reopen(session, instance, first, second, second_id, recorder):
    """Put the release back the way the acceptance wants it, save, reopen, compare."""
    set_release_for_saving(session, first, second_id)
    before = release_state(session)
    path = os.path.join(instance.tmp, "release.mmp")
    session.ok("project.save", {"path": path})
    saved = saved_sequence(path)
    recorder.require("the saved project carries a <mastering-sequence> element at all",
                   saved is not None, "path=%s" % path)
    recorder.require("the saved XML states the same order, sources, processing, gap and fades",
                   saved == before, "saved=%r before=%r" % (saved, before))
    check_reopen(session, path, before, first, second, recorder)


def check_reopen(session, path, before, first, second, recorder):
    # Replace the session, then open the file: a real load, not a left-over.
    session.ok("project.new_document", {"type": "production"})
    recorder.require("a production document has no release",
                   (session.ok("mastering.sequence_list").get("songs") or []) == [],
                   "songs=%r" % (session.ok("mastering.sequence_list").get("songs"),))
    session.ok("project.open", {"path": path})
    after = release_state(session)
    recorder.require("project.open restores sources, order, processing, gap and fades unchanged",
                   after == before, "after=%r before=%r" % (after, before))
    listed = session.ok("mastering.sequence_list")
    recorder.require("the reopened document is a mastering document",
                   listed.get("document_type") == "mastering", "listed=%r" % (listed,))
    sources = [os.path.realpath(song.get("source")) for song in listed.get("songs") or []]
    recorder.require("both source references resolve to the files this test wrote",
                   sources == [os.path.realpath(second), os.path.realpath(first)], "sources=%r" % (sources,))


def check_a_project_without_a_release(session, instance, recorder):
    """Absent element: a project that never had one saves none, and opening it clears the list."""
    plain = os.path.join(instance.tmp, "plain.mmp")
    session.ok("project.new_document", {"type": "mastering"})
    session.ok("project.save", {"path": plain})
    text = open(plain, encoding="utf-8", errors="replace").read()
    recorder.require("an empty release writes no element (a project that never used one re-saves as before)",
                   "<mastering-sequence" not in text, "plain=%s" % plain)
    session.ok("project.open", {"path": plain})
    recorder.require("opening the empty-release project leaves an empty release",
                   (session.ok("mastering.sequence_list").get("songs") or []) == [],
                   "songs=%r" % (session.ok("mastering.sequence_list").get("songs"),))


def check_the_instance(binary, problems):
    transcript = H.Transcript()
    first = None
    second = None
    with H.start_instance(binary, workingdir=None, extra_env=plugin_env(binary)) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        print("instance: %s" % binary)
        session = Session(client, transcript)
        session.ok("control.version")
        first = write_tone(os.path.join(instance.tmp, "opening.wav"), 220.0)
        second = write_tone(os.path.join(instance.tmp, "closing.wav"), 660.0)
        recorder = Recorder(problems)
        check_registration(session, recorder)
        check_empty_document(session, recorder)
        first_id = add_two_songs(session, first, second)[0]
        check_adds_are_recorded(session, recorder)
        check_two_songs(session, first, second, recorder)
        second_id = check_edits_are_undoable(session, first, first_id, recorder)
        check_save_and_reopen(session, instance, first, second, second_id, recorder)
        check_a_project_without_a_release(session, instance, recorder)
    transcript.dump()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = H.Problems()
    check_the_instance(argv[1], problems)
    return H.finish([("the mastering release sequence over the control socket", not problems, problems.items)])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
