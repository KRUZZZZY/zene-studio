#!/usr/bin/env python3
"""END-TO-END proof that the groove pool and quantise are drivable by an agent.

THE CLAIM UNDER TEST: an agent connected to a real `zene` instance over its
control socket can capture the timing and velocity FEEL of one clip into a named
groove, apply that groove to another clip, quantise a clip with a strength and a
humanise amount, and reverse all of it - and the effect on the notes is real and
measurable, not merely a command that answered ok.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/
GrooveTemplateTest.cpp and ControlGrooveCommandsTest.cpp already hold the
arithmetic and the surface to account IN PROCESS. What they cannot prove is the
release contract's section 3.1: that the feature is reachable THROUGH THE SOCKET
by an agent, with the schema validation, the wire numbers and the undo
behaviour an external client actually gets. So this test starts the REAL binary
(the `ControlSocketIntegration` mould: `$<TARGET_FILE:zene>`,
`QT_QPA_PLATFORM=offscreen`, the shared `control_socket_harness`) and reads every
number back off the wire.

WHAT IT ASSERTS, in numbers - a note's tick position and velocity before and
after each operation, read from `roll.get_state`, and the groove pool read from
`groove.list`:

  * extract: a clip whose notes sit 3 / +2 / -2 / +3 ticks off a 12-tick grid,
    with velocities 120 / 80 / 100 / 100, must read back as the steps
    (+3, 100) (-3, 120) (+2, 80) (-2, 100) - the mean deviation per slot, and
    that slot's mean velocity;
  * apply: the same groove written onto a clip that sits ON the grid must move
    its four notes to ticks 3 / 9 / 26 / 34 with velocities 100 / 120 / 80 / 100,
    and report 4 positions and 2 velocities moved;
  * quantise: at strength 0.5 a note at tick 5 lands on 2 (5 + round(-2.5)), and
    at strength 1.0 on 0; a humanised quantise stays within the amount asked for,
    reproduces exactly under the same seed, and differs under another;
  * undo: control.undo restores the pre-command note positions, and takes an
    extracted, renamed or removed groove back out of the pool;
  * persistence: the groove survives a project.save + project.open round trip,
    because the pool is project state.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-groove-commands.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The grid the whole proof uses: one sixteenth note at the engine's own
#: 192-tick 4/4 bar (DefaultTicksPerBar / 16).
GRID = 12
#: The template cycle: four slots, so the feel repeats every beat.
LENGTH = 48
#: (key, tick, velocity) of the clip whose feel is captured. The notes are
#: 3 / +2 / -2 / +3 ticks off the grid; the velocities average 100.
FEEL_NOTES = ((60, 9, 120), (62, 26, 80), (64, 34, 100), (65, 51, 100))
#: ... and what the extraction must read back out of it: each slot's mean
#: deviation from the grid, and that slot's mean velocity.
FEEL_STEPS = ((3, 100), (-3, 120), (2, 80), (-2, 100))


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


def take_of(session, clip):
    """(position, velocity) of every note of a clip, in the clip's own order."""
    state = session.result("roll.get_state", {"clip": clip})
    return tuple((int(note.get("position", -1)), int(note.get("velocity", -1)))
                 for note in state.get("notes") or [])


def steps_of(session, name):
    """The timing and velocity offset of every slot of a groove."""
    listed = session.result("groove.list", {"name": name})
    steps = (listed.get("groove") or {}).get("steps") or []
    return tuple((int(step.get("timing", 0)), int(step.get("velocity", 0))) for step in steps)


def names_in_pool(session):
    return tuple(entry.get("name") for entry in
                 session.result("groove.list").get("templates") or [])


def make_clip(session, instance, transcript, name="Groove Target"):
    """A fresh instrument track and one empty clip on it, through the commands."""
    track = session.result("track.add", {"type": "instrument", "name": name})
    clip = session.result("clip.add", {"track": track.get("track"), "position": 0})
    if not clip.get("clip"):
        H.fail("clip.add returned no clip id (%r)" % clip, instance, transcript)
    return clip.get("clip")


def add_notes(session, clip, notes):
    for key, position, velocity in notes:
        session.result("note.add", {"clip": clip, "key": key, "position": position,
                                    "length": GRID, "velocity": velocity})


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_extract(session, clip, recorder):
    """The feel of the clip is captured as the exact numbers the rule implies."""
    extracted = session.result("groove.extract", {"clip": clip, "name": "feel",
                                                  "grid": GRID, "length": LENGTH})
    recorder.check("groove.extract captures the clip's feel",
                   extracted.get("notes_read") == 4 and extracted.get("replaced") is False
                   and steps_of(session, "feel") == FEEL_STEPS,
                   "notes_read=%r replaced=%r steps=%s"
                   % (extracted.get("notes_read"), extracted.get("replaced"),
                      steps_of(session, "feel")))
    again = session.result("groove.extract", {"clip": clip, "name": "feel",
                                              "grid": GRID, "length": GRID * 2})
    recorder.check("extracting over a name REPLACES it",
                   again.get("replaced") is True and names_in_pool(session) == ("feel",),
                   "replaced=%r pool=%s" % (again.get("replaced"), names_in_pool(session)))
    # Put the four-slot feel back for the apply below.
    session.result("groove.extract", {"clip": clip, "name": "feel", "grid": GRID,
                                      "length": LENGTH})


def check_apply(session, instance, transcript, recorder):
    """The groove really moves notes: tick positions and velocities before/after."""
    clip = make_clip(session, instance, transcript, "Apply Target")
    add_notes(session, clip, ((60, 0, 100), (62, 12, 100), (64, 24, 100), (65, 36, 100)))
    before = take_of(session, clip)
    recorder.check("the apply fixture starts ON the grid",
                   before == ((0, 100), (12, 100), (24, 100), (36, 100)),
                   "before=%s" % (before,))

    applied = session.result("groove.apply", {"clip": clip, "name": "feel"})
    after = take_of(session, clip)
    recorder.check("groove.apply moves the notes to the groove's own ticks",
                   after == ((3, 100), (9, 120), (26, 80), (34, 100)),
                   "after=%s" % (after,))
    recorder.check("groove.apply reports what really changed",
                   applied.get("positions_moved") == 4 and applied.get("velocities_moved") == 2
                   and applied.get("notes_moved") == 4,
                   "positions_moved=%r velocities_moved=%r notes_moved=%r"
                   % (applied.get("positions_moved"), applied.get("velocities_moved"),
                      applied.get("notes_moved")))
    # The A16 record, read BEFORE the undo: the class comes from the contract
    # table and the mechanism names the clip's own checkpoint.
    records = [record for record in session.result("control.transactions").get("transactions") or []
               if record.get("command") == "groove.apply"]
    recorder.check("the A16 record classes groove.apply true_inverse on the clip checkpoint",
                   bool(records) and records[-1].get("class") == "true_inverse"
                   and records[-1].get("reversible") is True
                   and "MidiClip checkpoint" in str(records[-1].get("mechanism")),
                   "records=%s" % (records[-1:] or [],))

    undone = session.result("control.undo")
    recorder.check("control.undo takes the groove off the clip's notes",
                   undone.get("undone") is True and take_of(session, clip) == before,
                   "undone=%r after undo=%s" % (undone.get("undone"), take_of(session, clip)))

    # Idempotence, asserted AFTER the undo so the undo has one step to take:
    # applying the same groove twice leaves the second application nothing to do,
    # because both targets (the slot's tick and the slot's velocity) are absolute.
    session.result("groove.apply", {"clip": clip, "name": "feel"})
    twice = session.result("groove.apply", {"clip": clip, "name": "feel"})
    recorder.check("a second apply has nothing left to do",
                   twice.get("notes_moved") == 0 and take_of(session, clip) == after,
                   "notes_moved=%r after=%s" % (twice.get("notes_moved"), take_of(session, clip)))
    session.result("control.undo")
    session.result("control.undo")
    return clip


def check_quantize(session, instance, transcript, recorder):
    """Strength is how far a note travels; humanise is a bounded seeded jitter."""
    clip = make_clip(session, instance, transcript, "Quantise Target")
    add_notes(session, clip, ((60, 5, 90), (62, 17, 130), (64, 29, 110), (65, 41, 70)))

    half = session.result("groove.quantize", {"clip": clip, "grid": GRID, "strength": 0.5})
    after_half = take_of(session, clip)
    recorder.check("strength 0.5 travels half the way to the grid",
                   after_half == ((2, 90), (14, 130), (26, 110), (38, 70))
                   and half.get("positions_moved") == 4
                   and half.get("velocities_moved") == 0,
                   "after=%s positions_moved=%r" % (after_half, half.get("positions_moved")))

    session.result("groove.quantize", {"clip": clip, "grid": GRID, "strength": 1.0})
    exact = take_of(session, clip)
    recorder.check("strength 1.0 lands the notes on the grid",
                   exact == ((0, 90), (12, 130), (24, 110), (36, 70)),
                   "after=%s" % (exact,))

    # THE HUMANISE IS A JITTER: bounded by the amount asked for, reproducible
    # from the SAME state, and a fixed point in POSITION (the timing draw is
    # pinned to the slot) while its velocity draw rolls again.
    reset = {"clip": clip, "grid": GRID, "strength": 1.0}
    take = dict(reset, humanise_ticks=3, humanise_velocity=5, seed=7)
    session.result("groove.quantize", take)
    humanised = take_of(session, clip)
    bounded = all(abs(h[0] - e[0]) <= 3 and abs(h[1] - e[1]) <= 5
                  for h, e in zip(humanised, exact))
    recorder.check("the humanise is bounded by the amount asked for",
                   humanised != exact and bounded,
                   "exact=%s humanised=%s" % (exact, humanised))

    session.result("groove.quantize", take)
    repeated = take_of(session, clip)
    recorder.check("the timing jitter is a fixed point, the velocity jitter rolls again",
                   tuple(h[0] for h in repeated) == tuple(h[0] for h in humanised)
                   and repeated != humanised,
                   "first=%s repeated=%s" % (humanised, repeated))

    session.result("groove.quantize", reset)
    recorder.check("quantising again with no humanise returns the clip to the grid",
                   take_of(session, clip) == exact,
                   "after reset=%s" % (take_of(session, clip),))
    session.result("groove.quantize", take)
    recorder.check("the same seed on the same notes reproduces the same take",
                   take_of(session, clip) == humanised,
                   "again=%s" % (take_of(session, clip),))
    session.result("groove.quantize", reset)
    session.result("groove.quantize", dict(take, seed=8))
    recorder.check("another seed is another take",
                   take_of(session, clip) != humanised,
                   "seed8=%s" % (take_of(session, clip),))

    undone = session.result("control.undo")
    recorder.check("control.undo takes a quantise back",
                   undone.get("undone") is True,
                   "undone=%r after undo=%s" % (undone.get("undone"), take_of(session, clip)))


def check_pool_edits_and_persistence(session, instance, transcript, recorder):
    """Rename, remove, both reversible; and the pool survives save + open."""
    renamed = session.result("groove.rename", {"name": "feel", "to": "shuffle"})
    recorder.check("groove.rename renames in place",
                   renamed.get("renamed_to") == "shuffle" and names_in_pool(session) == ("shuffle",),
                   "renamed_to=%r pool=%s" % (renamed.get("renamed_to"), names_in_pool(session)))
    session.result("control.undo")
    recorder.check("control.undo takes the rename back",
                   names_in_pool(session) == ("feel",),
                   "pool=%s" % (names_in_pool(session),))

    session.result("groove.remove", {"name": "feel"})
    session.result("control.undo")
    recorder.check("control.undo brings a removed groove back with its steps",
                   names_in_pool(session) == ("feel",)
                   and steps_of(session, "feel") == FEEL_STEPS,
                   "pool=%s steps=%s" % (names_in_pool(session), steps_of(session, "feel")))

    project = os.path.join(instance.tmp, "workspace", "groove-proof.mmp")
    saved = session.result("project.save", {"path": project})
    opened = session.result("project.open", {"path": project})
    recorder.check("the pool is project state: it survives save and open",
                   saved.get("saved") is True and bool(opened.get("file"))
                   and opened.get("error_count") == 0
                   and names_in_pool(session) == ("feel",)
                   and steps_of(session, "feel") == FEEL_STEPS,
                   "saved=%r opened=%r errors=%r pool=%s" % (saved.get("saved"), opened.get("file"),
                                                   opened.get("error_count"),
                                                   names_in_pool(session)))
    return project


def check_refusals(session, instance, transcript, recorder):
    """Every refusal is typed, and changes nothing."""
    clip = make_clip(session, instance, transcript, "Refusal Target")
    add_notes(session, clip, ((60, 5, 100),))
    unknowns = [
        ("groove.list", {"name": "nope"}),
        ("groove.remove", {"name": "nope"}),
        ("groove.rename", {"name": "nope", "to": "other"}),
        ("groove.apply", {"clip": clip, "name": "nope"}),
        ("groove.set", {"name": "bad", "length_ticks": 12, "step_ticks": 12,
                        "steps": [{"slot": 0, "timing": 9, "velocity": 0}]}),
        ("groove.quantize", {"clip": clip, "grid": 12, "strength": 5}),
    ]
    failures = []
    for command, args in unknowns:
        error = session.typed_error(command, args)
        if not error.get("kind") or not error.get("message"):
            failures.append("%s -> %r" % (command, error))
    recorder.check("every refusal is typed and carries a message",
                   not failures, "; ".join(failures))
    recorder.check("a refused call changed nothing",
                   take_of(session, clip) == ((5, 100),)
                   and names_in_pool(session) == ("feel",)
                   and "bad" not in names_in_pool(session),
                   "take=%s pool=%s" % (take_of(session, clip), names_in_pool(session)))


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
        print("FAIL: the groove.* socket proof has %d failed check(s)" % len(problems))
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
        recorder.check("the pool starts empty", names_in_pool(session) == (),
                       "pool=%s" % (names_in_pool(session),))
        feel_clip = make_clip(session, instance, transcript, "Feel Source")
        add_notes(session, feel_clip, FEEL_NOTES)
        check_extract(session, feel_clip, recorder)
        check_apply(session, instance, transcript, recorder)
        check_quantize(session, instance, transcript, recorder)
        check_pool_edits_and_persistence(session, instance, transcript, recorder)
        check_refusals(session, instance, transcript, recorder)
        check_quit(session, instance, recorder)

    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("groove.* socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
