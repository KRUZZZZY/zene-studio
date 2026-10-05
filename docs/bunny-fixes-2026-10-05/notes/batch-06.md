# notes lane, batch 06 — six findings

All six findings were re-read in the current tree before any change: five were REAL and are
fixed, each with a regression test confirmed RED before the product change and GREEN after;
one (cluster 1152) is a false positive — the code the report did not cite already re-sorts the
note list.

### Cluster 4806 — `finish` advertises unusable inverse commands
Verdict: FIXED
Evidence: `finish()` at `src/core/ControlCommandsNoteShared.h:72` built its transaction with
`transactionPayload({{clip}}, command, {}, ...)` — the command itself as its own inverse with
an EMPTY args object. Every command that routes through this helper takes at least `clip`, and
most take more, so the advertised inverse pair could never be replayed: a reader following
`control.transactions` and re-issuing `note.add_many {}` gets a validation failure on the
missing `clip`/`notes`. The helper now advertises `control.undo`, which takes no arguments at
all (`cmd.argsSchema = objectSchema()`, `src/core/ControlCommandsControl.cpp:436`) — the same
inverse `note.slide_clear` already advertises at `src/core/ControlCommandsNoteSlide.cpp:184`.
Only the private `__transaction` payload changed; no command's args or result schema, and no
canary entry, was touched.
Commit: b36deeeea
Test: NoteToolsTest::anAdvertisedInverseOfANoteEditCanActuallyBeReplayed
Red/green: red `cmake --build build --target NoteToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=8 (`FAIL!` — the
recorded inverse op was `note.add_many`, not `control.undo`); green
`cmake --build build --target zene NoteToolsTest` EXIT=0 and
`ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=0 (13/13). Both gates EXIT=0.

### Cluster 3904 — Clearing slides records an undo step when nothing changed
Verdict: FIXED
Evidence: `slideClear()` called `clip->addJournalCheckPoint()` unconditionally at
`src/core/ControlCommandsNoteSlide.cpp:156`, before it knew whether any flag in scope would
change, while `publishSlideEdit(clip)` eight lines later was already conditional on
`cleared > 0`. A clear over a scope holding no slide note therefore pushed a no-op step onto
the undo stack and answered `slides_cleared:0`. The checkpoint now follows the same condition
as the publish. `slideClear()` sat at its CCN 10 target, so the two flag walks moved into
`countSlides()` / `clearSlides()` helpers instead of growing the function.
Commit: 01c82dbbe
Test: ControlNoteScaleVerbsTest::clearingSlidesOnAClipWithoutAnyRecordsNoUndoStep
Red/green: red `cmake --build build --target ControlNoteScaleVerbsTest` EXIT=0 and
`cd build/tests && ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure` EXIT=8
(`FAIL!` — `Engine::projectJournal()->undoDepth()` grew by 1 across a no-op clear); green
`cmake --build build --target zene ControlNoteScaleVerbsTest` EXIT=0 and
`ctest -R '^(SlideNotesTest|ControlNoteScaleVerbsTest)$' --output-on-failure` EXIT=0 (2/2).
Both gates EXIT=0.

### Cluster 1152 — Key edits can leave simultaneous notes out of order
Verdict: FALSE-POSITIVE
Evidence: both transforms end in `finish(ref, clip, ...)`
(`src/core/ControlCommandsNoteTools.cpp:78` for `invert`, `:214` for `transpose_in_key`), and
`finish()` calls `clip->rearrangeAllNotes()` at `src/core/ControlCommandsNoteShared.h:66`,
which re-sorts the whole list with `Note::lessThan` (`src/tracks/MidiClip.cpp:264`,
`include/Note.h:263`: position ascending, key descending as the tie-break). The stored order
is therefore always the clip's ordering after a key edit — the report's premise (that
`note.transpose` needs an explicit call the note tools lack) does not hold, because these two
commands get theirs from the shared helper. The report is also wrong about the ordering
itself: simultaneous notes sort by key DESCENDING, not ascending.
Commit: none
Test: none
Red/green: none

### Cluster 5691 — Arpeggiation drops distinct voices at the same pitch
Verdict: FIXED
Evidence: `arpeggiate()` collected the voices in a `std::map<int, Note>` keyed by pitch and
filled it with `voices.emplace(note->key(), *note)` at
`src/core/ControlCommandsNoteTools.cpp:106-111`. `emplace` keeps the FIRST note of two
same-pitch chord members while `keys` kept both, so `voices.at(...)` handed the same note
twice and the second voice's velocity, pan and probability died with the removed chord.
`runOrder()` now returns INDICES into a vector of voices (one per note, stably sorted by key),
so `up`/`down`/`updown` play every chord member with its own state.
Commit: 6de0a6292
Test: NoteToolsTest::arpeggiatingAChordKeepsBothVoicesOfTheSamePitch
Red/green: red `cmake --build build --target NoteToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=8 (`FAIL!` —
`'notes()[0]->getVolume() != notes()[1]->getVolume()' returned FALSE`); green
`cmake --build build --target zene NoteToolsTest` EXIT=0 and
`ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=0 (14/14). Both gates EXIT=0.

### Cluster 5694 — Flam velocity silently clamps out-of-range input
Verdict: FIXED
Evidence: `src/core/ControlCommandsNoteTools.cpp:135` read `velocity` and wrapped it in
`std::clamp(..., 0.05, 1.0)` and then answered success, so `velocity: 2` created grace notes
at full velocity — a value the caller never asked for. The schema is a bare
`numberProperty()` (`src/core/ControlCommandsNoteTools.cpp:333`) and appears verbatim in
`tools/mcp-zene-control/zene_control/commands_snapshot.json`, so it may not change; the range
is refused in the handler with `InvalidArgs`, the handler-side refusal of `4e288ecb5`. The
0.6 default and every accepted value are unchanged.
Commit: cf5504f80
Test: NoteToolsTest::aFlamVelocityTheHandlerCannotHonourIsRefused
Red/green: red `cmake --build build --target NoteToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=8 (`FAIL!` —
`velocity: 2` was accepted); green
`cmake --build build --target zene NoteToolsTest` EXIT=0 and
`ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=0 (15/15). Both gates EXIT=0.

### Cluster 2883 — Repeated notes can exceed the supported song range
Verdict: FIXED
Evidence: `repeatNotes()` formed each copy at
`note->pos().getTicks() + i * interval` (`src/core/ControlCommandsNoteTools.cpp:207` before
the fix) with no range check, and `MidiClip::addNote` validates nothing
(`src/tracks/MidiClip.cpp:182`), so a note near the right edge produced notes ending past
`MaxSongLength` (`include/Song.h:77`) while the reply still counted them in `added`. The
farthest copy (the last one) is now checked in `qint64` before any mutation and the call is
refused with `InvalidArgs`, mirroring `checkNewEndPosition()`
(`src/core/ControlCommandsNoteShape.cpp:153`). The report's signed-overflow claim is FALSE:
`interval` is capped at 3072 and `count` at 64, so `i * interval` cannot leave `tick_t`'s
range — the real defect was the unchecked song range, which is what the fix addresses.
Commit: d5d633209
Test: NoteToolsTest::repeatingANoteAtTheSongsEdgeIsRefusedNotWrittenPastIt
Red/green: red `cmake --build build --target NoteToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=8 (`FAIL!` — the copy
past the limit had been written); green
`cmake --build build --target zene NoteToolsTest` EXIT=0 and
`ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=0 (16/16). Both gates EXIT=0.

Leftovers: nothing half-done. Cluster 2883's overflow wording stays false and was not
"fixed"; cluster 1152 needs no change. `note.slide_clear` and `note.slide_set` still advertise
an inverse op of `control.undo`/`note.slide_set` with `{clip}` args, which replay against
`control.undo`'s empty schema — the same shape cluster 4806 had; only the shared `finish()`
helper was in this lane's findings, so the two slide commands' own payload was left alone and
is worth a follow-up finding.