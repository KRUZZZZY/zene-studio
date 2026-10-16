#!/usr/bin/env python3
"""END-TO-END proof that the chord progression workflow is drivable by an agent
(Q050-019: the progression panel's own read and write, through the socket).

THE CLAIM UNDER TEST: an agent connected to a real `zene` instance over its
control socket can ask what the catalogue would WRITE in a chosen scale
(chord.progression_list with `scale`, the "suggestions" the panel shows), SELECT
one progression, write its notes into a clip (chord.progression_generate - the
SAME registered command the panel's Write button dispatches), read the notes back
off the wire, and take it all back with ONE control.undo.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/gui/
ChordProgressionPanelTest.cpp holds the panel and tests/src/core/
ControlChordWriteTest.cpp holds the surface, both in process. What neither can
show is the release contract's section 3.1 path: a real client, over the socket,
with the schema validation, the wire numbers and the undo behaviour an external
driver actually gets. So this starts the REAL binary (the `ControlGrooveCommands`
mould: `$<TARGET_FILE:zene>`, `QT_QPA_PLATFORM=offscreen`, the shared
`control_socket_harness`) and reads every number off the wire.

WHAT IT ASSERTS, in numbers:

  * suggestions: chord.progression_list {scale: Major, root: 0, octave: 4} reports
    the catalogue SPELLED OUT in C - "I-V-vi-IV" is four chords named Major,
    Major, minor, Major, its first chord rooted at C (key 60); and at root 2
    (D) the same walk's ii-V-I chord i is rooted at E (pitch class 4);
  * write: chord.progression_generate into a clip that already held one note
    reports notes_written 12 and notes_replaced 1, leaves exactly 12 notes, and
    the lowest key of the first chord is the chord the listing named for it;
  * undo: one control.undo restores the clip to exactly the one note it held;
  * the A16 record classes chord.progression_generate true_inverse on the clip's
    own checkpoint;
  * refusals: a progression, a clip and a scale this engine does not have are
    each refused, typed, and change nothing.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-chord-progression.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The progression the whole proof walks, and the key it walks in.
PROGRESSION = "I-V-vi-IV"
SCALE = "Major"


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, command, args=None):
        """The reply, or {'error': ...} so a failed call is visible in a check."""
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def typed_error(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is False:
            return reply.get("error") or {}
        return {}


class Recorder:
    """Collects the named checks and their evidence, so a failure names the number."""

    def __init__(self):
        self.results = []

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))

    def problems(self):
        return [(name, evidence) for name, passed, evidence in self.results if not passed]


# ---------------------------------------------------------------------------
# reading the wire
# ---------------------------------------------------------------------------


def notes_of(session, clip):
    """(key, position, length, velocity) of every note of a clip."""
    state = session.result("roll.get_state", {"clip": clip})
    return tuple((int(note.get("key", -1)), int(note.get("position", -1)),
                  int(note.get("length", -1)), int(note.get("velocity", -1)))
                 for note in state.get("notes") or [])


def suggestion(session, name, scale=SCALE, root=0, octave=4):
    """The catalogue entry @a name spelt in @a scale, as the wire reports it."""
    listed = session.result("chord.progression_list",
                            {"scale": scale, "root": root, "octave": octave})
    for value in listed.get("suggestions") or []:
        if value.get("name") == name:
            return value
    return {}


def make_clip(session, instance, transcript, name):
    """A fresh instrument track and one empty clip on it, through the commands."""
    track = session.result("track.add", {"type": "instrument", "name": name})
    clip = session.result("clip.add", {"track": track.get("track"), "position": 0})
    if not clip.get("clip"):
        H.fail("clip.add returned no clip id (%r)" % clip, instance, transcript)
    return clip.get("clip")


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_suggestions(session, recorder):
    """The catalogue is spelt out in the selected scale, by the engine's names."""
    four = suggestion(session, PROGRESSION)
    chords = four.get("chords") or []
    recorder.check("the catalogue is spelt in the selected scale",
                   bool(chords) and len(chords) == 4
                   and [c.get("chord") for c in chords] == ["Major", "Major", "minor", "Major"]
                   and chords[0].get("root") == 0 and chords[0].get("key") == 60,
                   "I-V-vi-IV=%r" % (four,))
    # The root transposes the walk: ii-V-I in D starts on E (pitch class 4).
    two_five_one = suggestion(session, "ii-V-I", root=2)
    recorder.check("the root transposes the whole walk",
                   (two_five_one.get("chords") or [{}])[0].get("root") == 4,
                   "ii-V-I in D=%r" % (two_five_one,))
    # With no scale the command is the plain catalogue: no suggestions.
    plain = session.result("chord.progression_list")
    recorder.check("without a scale the command is the plain catalogue",
                   "suggestions" not in plain, "suggestions present=%s"
                   % ("suggestions" in plain))


def check_write_and_undo(session, instance, transcript, recorder):
    """Selecting a progression, writing its notes, and one undo taking them back."""
    clip = make_clip(session, instance, transcript, "Progression Target")
    session.result("note.add", {"clip": clip, "key": 40, "position": 0, "length": 96,
                                "velocity": 60})
    before = notes_of(session, clip)
    recorder.check("the fixture clip holds exactly one note",
                   before == ((40, 0, 96, 60),), "before=%s" % (before,))

    written = session.result("chord.progression_generate",
                             {"clip": clip, "progression": PROGRESSION, "scale": SCALE,
                              "root": 0, "octave": 4, "pattern": "block"})
    after = notes_of(session, clip)
    chords = written.get("chords") or []
    recorder.check("the generator writes the progression's notes",
                   written.get("notes_written") == 12 and written.get("notes_replaced") == 1
                   and written.get("note_count") == 12 and len(after) == 12,
                   "notes_written=%r notes_replaced=%r notes=%s"
                   % (written.get("notes_written"), written.get("notes_replaced"), after))
    recorder.check("the written notes start on the chords the listing named",
                   bool(chords) and chords[0].get("key") == 60
                   and {note[0] for note in after if note[1] == 0} == {60, 64, 67},
                   "chords=%r first-step keys=%s"
                   % (chords[0:1], sorted({note[0] for note in after if note[1] == 0})))

    # The A16 record, read BEFORE the undo: true_inverse on the clip checkpoint.
    records = [record for record in session.result("control.transactions").get("transactions") or []
               if record.get("command") == "chord.progression_generate"]
    recorder.check("the A16 record classes the write true_inverse on the clip checkpoint",
                   bool(records) and records[-1].get("class") == "true_inverse"
                   and records[-1].get("reversible") is True
                   and "checkpoint" in str(records[-1].get("mechanism")),
                   "records=%s" % (records[-1:] or [],))

    undone = session.result("control.undo")
    recorder.check("one control.undo restores the clip exactly",
                   undone.get("undone") is True and notes_of(session, clip) == before,
                   "undone=%r after undo=%s" % (undone.get("undone"), notes_of(session, clip)))
    return clip


def check_refusals(session, instance, transcript, recorder, clip):
    """Every way of naming something the engine does not have is refused, typed."""
    before = notes_of(session, clip)
    unknowns = [
        ("an unknown progression", "chord.progression_generate",
         {"clip": clip, "progression": "nope", "scale": SCALE}),
        ("an unknown scale", "chord.progression_generate",
         {"clip": clip, "progression": PROGRESSION, "scale": "Klingon"}),
        ("an unknown clip", "chord.progression_generate",
         {"clip": "clip-999", "progression": PROGRESSION, "scale": SCALE}),
    ]
    failures = []
    for label, command, args in unknowns:
        error = session.typed_error(command, args)
        if not error.get("kind") or not error.get("message"):
            failures.append("%s -> %r" % (label, error))
    recorder.check("every unknown is refused, typed", not failures, "; ".join(failures))
    recorder.check("a refused call changed nothing",
                   notes_of(session, clip) == before,
                   "take=%s" % (notes_of(session, clip),))


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-58s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    problems = recorder.problems()
    if problems:
        print("")
        print("FAIL: the chord progression socket proof has %d failed check(s)" % len(problems))
        return 1
    print("")
    print("PASS: %d checks, every one a measured number" % len(recorder.results))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]):
        print("cannot run: no binary at %s" % argv[1])
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)

        session.result("control.version")
        check_suggestions(session, recorder)
        clip = check_write_and_undo(session, instance, transcript, recorder)
        check_refusals(session, instance, transcript, recorder, clip)
        check_quit(session, instance, recorder)

    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("chord progression socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
