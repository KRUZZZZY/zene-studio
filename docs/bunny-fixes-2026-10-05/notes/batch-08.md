# notes lane, batch 08 — six findings

All six findings were re-read in the current tree before any change: four are REAL and are fixed,
each with a regression test confirmed RED for the stated reason before the product change and GREEN
after; one is a FALSE-POSITIVE (the linked-clip mirroring it asks for already happens, lower down in
`MidiClip`); one is SKIPPED-SCOPE (the recalculation it asks for is already called, and the behaviour
it actually wants is the auto-resize design's grow-only rule).

### Cluster 5686 — `note.add` and `note.remove` leave linked clips out of sync
Verdict: FALSE-POSITIVE
Evidence: both handlers reach the model, which mirrors for them:
`src/tracks/MidiClip.cpp:197-210` (`addNote`) and `src/tracks/MidiClip.cpp:225-244`
(`removeNote(Note*)`) each call `updateLength()`, emit `dataChanged()` and then
`ClipLinks::mirrorContent(this)` when `linkId() > 0`. The handlers at
`src/core/ControlCommandsNotes.cpp:113-156` and `:171-206` therefore need no mirroring of their
own; the explicit calls in `note.move`/`note.resize`/`note.velocity_set` are belt-and-braces, not a
behaviour the other two lack. `ClipLinkTest` passes.
Commit: none
Test: none
Red/green: none

### Cluster 4814 — `note.remove`'s advertised inverse loses note state and identity
Verdict: FIXED
Evidence: `src/core/ControlCommandsNotes.cpp:183-191` (before the fix) built the inverse from key,
position, length and velocity only and advertised `note.add`, which carries no pan, probability or
expression and cannot restore the removed note's id. The handler now advertises `control.undo`
with empty args (`:203`), which is the inverse the clip checkpoint taken at `:194` actually
supports; the full serialized state still rides along in the before-payload, and the result's
`removed` id still comes from the note read before the delete (`:192`).
Commit: 94e73d7b9
Test: NoteToolsTest::theAdvertisedInverseOfRemovingANoteRestoresTheWholeNote
Red/green: red `cmake --build build --target zene NoteToolsTest RangeEditsTest` EXIT=0 and
`cd build/tests && ./NoteToolsTest` EXIT=2 (2 failed; `Actual (inverse op): "note.add"`,
`Expected: "control.undo"`); green `cmake --build build --target zene NoteToolsTest` EXIT=0 and
`./NoteToolsTest` EXIT=0 (22 passed, 0 failed), plus
`ctest -R '^(NoteEditingCommandsTest|ClipLinkTest|ControlNoteScaleVerbsTest|NoteShapeValidationTest|NoteCurvesAcrossTest|RecordToolsTest|UndoHonestyTest|ControlRegistryTest|ControlResultSchemaTest|ControlEditCommandsTest|ControlGrooveCommandsTest|ArrangementModesTest|NoteToolsTest|RangeEditsTest)$'`
EXIT=0 (14/14). Both gates EXIT=0.

### Cluster 4815 — `note.remove` leaves auto-resizing clip length too long
Verdict: SKIPPED-SCOPE
Evidence: the reported cause is wrong — `MidiClip::removeNote(Note*)` already calls
`updateLength()` at `src/tracks/MidiClip.cpp:229`, exactly as `addNote` does at `:186`. What
`updateLength()` does at `src/tracks/MidiClip.cpp:129-157` is deliberately grow-only: it takes the
maximum of the note end positions and rounds it UP to full bars, so a clip never shrinks. Making
removal shrink an auto-resizing clip would change the auto-resize semantics the GUI also relies on
— a design change, not a minimal behaviour fix.
Commit: none
Test: none
Red/green: none

### Cluster 4812 — `note.select` can report the wrong previous selection after notes move
Verdict: FIXED
Evidence: the handler read the previous selection from the position store
(`src/core/ControlCommandsNotes.cpp:457`, before the fix) and resolved those positions against the
clip's CURRENT note list (`:494-500`). A reorder — `note.move` calls `rearrangeAllNotes()` at
`:240` — leaves the stored positions stale, so the resolution named a different note than the one
selected. The previous selection is now read from the notes' own selection flags
(`selectedNoteIds`, `:411-422`), which travel with the note objects through a re-sort; the store is
still written for `roll.get_state` and the selection-scope verbs, exactly as
`note.select_where` writes both.
Commit: 94e73d7b9
Test: NoteToolsTest::selectingAfterAMoveReportsTheNoteThatWasActuallySelected
Red/green: red `cmake --build build --target zene NoteToolsTest RangeEditsTest` EXIT=0 and
`./NoteToolsTest` EXIT=2 (2 failed; `previous.first()` was `note-8`, the note that had NOT been
selected, where `note-6` was expected); green `cmake --build build --target zene NoteToolsTest`
EXIT=0 and `./NoteToolsTest` EXIT=0 (22 passed, 0 failed) — including the cluster 3898 case
`NoteShapeValidationTest::select_whereWritesTheSelectionStoreToo`. Both gates EXIT=0.

### Cluster 4890 — Range edits advertise themselves as their own inverse
Verdict: FIXED
Evidence: `finishEdit` at `src/core/ControlCommandsRange.cpp:125-132` (before the fix) passed the
SAME command with empty args as the transaction's inverse, so replaying `range.delete`'s inverse
deleted again instead of restoring the clips — and the same held for the other four checkpointed
range edits that call it (`:157`, `:216`, `:254`, `:265`, `:349`). Each of those goes through
`beginEdit` (`:105-123`), which checkpoints every covered track as ONE undo step, so the
transaction now advertises `control.undo`, whose empty args are then truthful.
Commit: 4ee321c74
Test: RangeEditsTest::aRangeEditAdvertisesControlUndoAsItsInverse
Red/green: red `cmake --build build --target zene NoteToolsTest RangeEditsTest` EXIT=0 and
`cd build/tests && ./RangeEditsTest` EXIT=2 (2 failed; `Actual (inverse op): "range.delete"`,
`Expected: "control.undo"`); green the same build EXIT=0 and `./RangeEditsTest` EXIT=0 (13 passed,
0 failed), plus the 14-test ctest selection above EXIT=0. Both gates EXIT=0.

### Cluster 4897 — `range.fade` counts clips that received no fade
Verdict: FIXED
Evidence: `rangeFades` at `src/core/ControlCommandsRange.cpp:164-170` returned `true`
unconditionally after the wholly-inside early-out, so a clip spanning the WHOLE range — its start
before `from` and its end after `to`, satisfying neither edge condition at `:167` nor `:168` —
reached the `faded +=` at `:213` with both fade lengths untouched. It now reports whether it
actually set a fade, so a spanning clip is no longer counted.
Commit: 4ee321c74
Test: RangeEditsTest::fadeOverAClipSpanningTheWholeRangeIsNotCounted
Red/green: red `cmake --build build --target zene NoteToolsTest RangeEditsTest` EXIT=0 and
`./RangeEditsTest` EXIT=2 (2 failed; `faded` was 1 where 0 was expected); green the same build
EXIT=0 and `./RangeEditsTest` EXIT=0 (13 passed, 0 failed). Both gates EXIT=0.

Leftovers: clusters 4814 and 4812 share commit 94e73d7b9 and clusters 4890 and 4897 share
4ee321c74 — each pair touched one source file and one test file, so splitting them further would
have meant staging partial hunks of the same file; nothing is half-done. The `note.select`
handler still resolves the `selected_notes` it reports back from the position store it has just
written, which is consistent by construction (the store was written from the current list moments
earlier) — out of scope for 4812, which is about `previous_notes`. `MidiClip::updateLength()`
remains grow-only (see 4815); if shrinking auto-resizing clips is wanted, that is its own change
with its own design decision.