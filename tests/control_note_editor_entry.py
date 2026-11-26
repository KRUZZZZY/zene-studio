"""The note editor's entry modes over the control socket (Q050-211, docs/NOTE-EDITOR.md 3a/3b).

Split out of tests/control-note-editor.py so neither file grows past its code-line target ('tests/tool-files-gate.sh'). The
driver imports check_step_input and check_recording and passes its own new_clip, so both phases still run inside
one driver run, in order, against one instance.

Step input: a step lands at the clip's cursor, the cursor advances by the grid step, the step's notes are the
selection, ONE control.undo takes the step back, and a transport stop under a step writes nothing.
Recording: note.record_on / note.record_off write the notes a keyboard plays, ONE control.undo takes the whole
take back, and the transport's stop ends it.
"""

def step_notes(session, clip):
    """The clip's notes as note.list reports them, by id (note.list names the id `note`)."""
    return {n.get("note"): n for n in session.result("note.list", {"clip": clip}).get("notes") or []}


def step_input_state(session):
    return session.result("window.get_state").get("note_editor") or {}


REFUSED_STEPS = (
    {"key": 60, "keys": [64]},
    {},
    {"key": 128},
    {"key": 60, "keys": [60, 60]},
    {"key": 60, "position": 1919800, "length": 24},
)


def check_step_input_armed(session, recorder, clip):
    """The switch and the clip's cursor, through the command the toolbar button runs."""
    armed = session.result("window.note_editor_set",
                           {"clip": clip, "grid": 24, "new_length": 30, "step_input": True, "step_cursor": 0})
    state = step_input_state(session)
    recorder.check("window.note_editor_set arms Step input and reports the clip's cursor",
                   armed.get("step_input") is True and armed.get("step_cursor") == 0
                   and state.get("step_input") is True and state.get("step_cursor") == 0,
                   "reply=%r state=%r" % (armed, state))


def check_first_step(session, recorder, clip):
    """A bare step takes its defaults - a sixteenth at the cursor - and the cursor moves on by it."""
    first = session.result("note.step_input", {"clip": clip, "key": 60})
    notes = step_notes(session, clip)
    placed = notes.get(first.get("notes", [None])[0]) or {}
    recorder.check("note.step_input puts the key at the cursor with a sixteenth's length",
                   first.get("position") == 0 and first.get("length") == 12 and first.get("cursor") == 12
                   and placed.get("position") == 0 and placed.get("key") == 60 and placed.get("length") == 12,
                   "reply=%r note=%r" % (first, placed))
    recorder.check("the editor's cursor is the one the command advanced",
                   step_input_state(session).get("step_cursor") == 12,
                   "state=%r" % step_input_state(session))
    return first


def check_chord_placement(session, recorder, clip):
    """A chord on the grid the header shows, at the cursor the last step left."""
    chord = session.result("note.step_input", {"clip": clip, "keys": [64, 67], "step": 24, "length": 30})
    notes = step_notes(session, clip)
    chord_notes = [notes[i] for i in chord.get("notes") or []]
    recorder.check("a chord lands at the cursor, one note per pitch, on the editor's step and length",
                   chord.get("position") == 12 and chord.get("cursor") == 36 and len(chord_notes) == 2
                   and all(n.get("position") == 12 and n.get("length") == 30 for n in chord_notes),
                   "reply=%r notes=%r" % (chord, chord_notes))
    recorder.check("the clip holds both steps", len(notes) == 3, "notes=%r" % notes)
    return chord, notes


def check_chord_selection(session, recorder, first, notes, chord_notes):
    """The step's notes are the selection, and the earlier step's is not."""
    recorder.check("the step's notes are the selection, and the earlier step's is not",
                   all(n.get("selected") is True for n in chord_notes)
                   and (notes.get(first.get("notes", [None])[0]) or {}).get("selected") is False,
                   "notes=%r" % notes)


def check_step_undo(session, recorder, clip):
    """ONE undo per step; the cursor is edit state and undo does not move it."""
    session.result("control.undo")
    notes = step_notes(session, clip)
    recorder.check("ONE control.undo takes the chord back and leaves the cursor where it was",
                   len(notes) == 1 and step_input_state(session).get("step_cursor") == 36,
                   "notes=%r state=%r" % (notes, step_input_state(session)))


def check_step_under_transport(session, recorder, clip):
    """The transport runs under the step and stops after it: nothing more is written, the cursor holds,
    the mode survives, and the next step continues from the cursor."""
    session.result("transport.play")
    during = session.result("note.step_input", {"clip": clip, "key": 62, "length": 24, "step": 24})
    session.result("transport.stop")
    stopped = session.result("transport.get_state")
    after_stop = step_notes(session, clip)
    recorder.check("a step entered while the transport runs lands at the cursor, and the stop writes nothing",
                   during.get("position") == 36 and len(after_stop) == 2
                   and stopped.get("playing") is False and step_input_state(session).get("step_cursor") == 60,
                   "during=%r after=%r transport=%r" % (during, after_stop, stopped))
    recorder.check("Step input survives the transport stop",
                   step_input_state(session).get("step_input") is True, "state=%r" % step_input_state(session))
    next_step = session.result("note.step_input", {"clip": clip, "key": 64, "length": 24, "step": 24})
    recorder.check("the step after a stop continues from the cursor the stop left",
                   next_step.get("position") == 60 and len(step_notes(session, clip)) == 3,
                   "reply=%r notes=%r" % (next_step, step_notes(session, clip)))


def check_step_refusals(session, recorder, clip):
    """A step refuses what it cannot enter, typed, and changes nothing; then the switch turns it off."""
    before = step_notes(session, clip)
    for args in REFUSED_STEPS:
        args = dict(args, clip=clip)
        error = session.typed_error("note.step_input", args)
        recorder.check("a step that cannot be entered is refused, typed: %r" % (args,),
                       error.get("kind") == "invalid_args", "error=%r" % error)
    recorder.check("and none of the refused steps changed the clip", step_notes(session, clip) == before,
                   "notes=%r" % step_notes(session, clip))
    disarmed = session.result("window.note_editor_set", {"step_input": False})
    recorder.check("window.note_editor_set disarms Step input", disarmed.get("step_input") is False,
                   "reply=%r" % disarmed)


def check_step_input(session, recorder, new_clip):
    """Q050-211: step entry over the socket - a step lands at the clip's cursor, the cursor advances by
    the grid step, the step's notes are the selection, ONE control.undo takes the step back, and a
    transport stop under a recorded step writes nothing and leaves the cursor where it was."""
    _, clip = new_clip(session, "Step input")
    check_step_input_armed(session, recorder, clip)
    first = check_first_step(session, recorder, clip)
    chord, notes = check_chord_placement(session, recorder, clip)
    check_chord_selection(session, recorder, first, notes, [notes[i] for i in chord.get("notes") or []])
    check_step_undo(session, recorder, clip)
    check_step_under_transport(session, recorder, clip)
    check_step_refusals(session, recorder, clip)

def record_notes(session, clip):
    """A clip's notes as sorted key@position+length strings, so a take reads at a glance."""
    rows = session.result("note.list", {"clip": clip}).get("notes") or []
    return sorted("%d@%d+%d" % (n["key"], n["position"], n["length"]) for n in rows)


def check_record_armed(session, recorder, clip):
    """The switch, through the command the toolbar's Record button runs."""
    armed = session.result("window.note_editor_set", {"clip": clip, "record": True, "step_input": False})
    editor = step_input_state(session)
    recorder.check("window.note_editor_set arms Record and reports it",
                   armed.get("record") is True and editor.get("record") is True,
                   "reply=%r state=%r" % (armed, editor))


def check_record_phrase(session, recorder, clip):
    """Two notes of ONE take: each edge at a named position, and one undo takes the take back."""
    first = session.result("note.record_on", {"clip": clip, "key": 60, "position": 0, "step": 24})
    session.result("note.record_off", {"clip": clip, "key": 60, "position": 48, "step": 24})
    second = session.result("note.record_on", {"clip": clip, "key": 64, "position": 48, "step": 24})
    session.result("note.record_off", {"clip": clip, "key": 64, "position": 60, "step": 24})
    notes = record_notes(session, clip)
    recorder.check("note.record_on opens a note and note.record_off closes it where the key came up",
                   notes == ["60@0+48", "64@48+12"] and first.get("take_started") is True
                   and second.get("take_started") is False,
                   "notes=%r first=%r second=%r" % (notes, first, second))
    session.result("control.undo")
    recorder.check("ONE control.undo takes the whole take back", record_notes(session, clip) == [],
                   "notes=%r" % record_notes(session, clip))


def check_record_under_transport(session, recorder, clip):
    """A key held while the transport runs, and the stop that ends the take under it."""
    session.result("transport.play")
    during = session.result("note.record_on", {"clip": clip, "key": 67, "step": 24})
    session.result("transport.stop")
    notes = record_notes(session, clip)
    ended = session.result("note.record_end", {"clip": clip})
    late = session.result("note.record_off", {"clip": clip, "key": 67, "step": 24})
    recorder.check("a note recorded while the transport runs stays one step long after the stop",
                   during.get("position") is not None and notes == ["67@%d+24" % during.get("position")],
                   "during=%r notes=%r" % (during, notes))
    recorder.check("the stop ended the take: record_end closes nothing and the late key-up writes nothing",
                   ended.get("count") == 0 and late.get("closed") is False,
                   "ended=%r late=%r" % (ended, late))


def check_record_disarm(session, recorder, clip):
    """Record goes off through the same command, and a take that never started writes nothing."""
    off = session.result("window.note_editor_set", {"record": False})
    stray = session.result("note.record_off", {"clip": clip, "key": 70})
    recorder.check("window.note_editor_set disarms Record, and a key-up with nothing open writes nothing",
                   off.get("record") is False and stray.get("closed") is False,
                   "reply=%r stray=%r" % (off, stray))


def check_recording(session, recorder, new_clip):
    """Q050-211: live recording over the socket - the two MIDI edges write the notes a keyboard plays,
    ONE undo step covers the take, and the transport's stop ends it."""
    _, clip = new_clip(session, "Record")
    check_record_armed(session, recorder, clip)
    check_record_phrase(session, recorder, clip)
    check_record_under_transport(session, recorder, clip)
    check_record_disarm(session, recorder, clip)
