# notes lane, batch 03 — six findings

### Cluster 3914 — Note edits can create notes extending past the song limit
Verdict: FIXED
Evidence: `resolveEdits()` validated position and length as independent fields
(`src/core/ControlCommandsNoteEdit.cpp`), so a note positioned near `MaxSongLength`
(`include/Song.h:77`, `9999 * DefaultTicksPerBar`) with a positive length ended past the supported
song range. A new `checkEndPosition()` helper sums the resulting position and length in `qint64` and
returns `InvalidArgs` when the end exceeds the limit; `checkRecurPattern()` was extracted from
`resolveEdits` so the new branch did not push it past the CCN 10 ratchet.
Commit: 72e123645
Test: NoteEditRangeTest::aNoteMayNotEndPastTheSongLimit
Red/green: red `cmake --build build --target zene NoteEditRangeTest NoteEditingCommandsTest` EXIT=0
and `cd build/tests && ctest -R '^NoteEditRangeTest$' --output-on-failure` EXIT=8
(`result.errorKind` 0, expected `ControlErrorKind::InvalidArgs` 3 — proven by temporarily disabling
the new check, which was restored before the commit); green the same build EXIT=0 and
`ctest -R '^(NoteEditRangeTest|NoteEditingCommandsTest)$'` EXIT=0 (2/2), plus
`ctest -R '^(NotePlaybackTest|ClipLinkTest|NoteToolsTest|NoteCurvesAcrossTest|NoteGridTest|NoteEditorFlTest)$'`
EXIT=0 (6/6). Both gates EXIT=0.
Leftovers: the case lives in a new `tests/src/core/NoteEditRangeTest.cpp` rather than in
`NoteEditingCommandsTest.cpp`, which was already at the 500-code-line file-length limit; the new file
is registered in `tests/CMakeLists.txt` (LMMS_TESTS and the target-name condition), `tests/fork-sources.txt`
and `tests/all-sources.txt`.

### Cluster 4807 — `note.edit` silently ignores misspelled fields
Verdict: FIXED
Evidence: `checkEditValues()` (`src/core/ControlCommandsNoteEdit.cpp`) validated the values of known
fields only, never the entry keys, so an edit carrying `velocity: 90` plus the typo `velcoity: 10`
succeeded, wrote 90 and dropped the typo silently. `checkEditKeys()` now rejects any key that is not
`note` or one of the `kFields` set, returning `InvalidArgs` before anything is written.
Commit: ba9f77eed
Test: NoteEditingCommandsTest::noteEditRefusesAMisspelledField
Red/green: red `cmake --build build --target zene NoteEditingCommandsTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteEditingCommandsTest$' --output-on-failure` EXIT=8
(`result.errorKind` 0 vs `InvalidArgs` 3); green EXIT=0 / EXIT=0, plus `NotePlaybackTest`,
`NoteGridTest`, `NoteEditorTest`, `ItemListPanelTest`, `NoteEditorFlTest` 5/5 passed. Both gates EXIT=0.
Leftovers: none.

### Cluster 5676 — Expression edits do not propagate to linked clips
Verdict: FIXED
Evidence: the set and clear handlers in `src/core/ControlCommandsNoteExpression.cpp` changed note data
and emitted `dataChanged()` but never called `ClipLinks::mirrorContent`, unlike every other note-edit
path in the slice (`ControlCommandsNoteEdit.cpp`). Both handlers now mirror the clip to its link group
after `dataChanged()`.
Commit: aa72a85fe
Test: ClipLinkTest::anExpressionEditIsSeenByTheOtherMember
Red/green: red `cmake --build build --target zene ClipLinkTest` EXIT=0 and
`cd build/tests && ctest -R '^ClipLinkTest$' --output-on-failure` EXIT=8
(`expressionCount(registry, b)` 0, expected 1); green EXIT=0 / EXIT=0, plus
`ControlNoteExpressionCommandsTest` (2/2) and `ClipLinkPersistenceTest`, `MpeNoteStorageTest` (2/2).
Both gates EXIT=0.
Leftovers: none.

### Cluster 5677 — The expression inverse cannot restore an absent expression
Verdict: FALSE-POSITIVE
Evidence: `transactionPayload()` (`src/core/ControlEditSupport.cpp:481-493`) never sets an `applies`
field, so `undoByCommand()` (`src/core/ControlCommandsControl.cpp:330-346`) never dispatches the
recorded `note.expression_set` inverse and falls back to the journal replay path at `:412-417`. The
journal undo restores the clip's checkpointed before-state, which for a note that had no expression
is the absent-expression state — so `has_expression` is correctly false again after undo, and no
`note.expression_clear` inverse is needed.
Commit: none
Test: none
Red/green: none.
Leftovers: none.

### Cluster 2026 — Chord following looks up the wrong timeline position for left-trimmed clips
Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsNoteHarmony.cpp:84` computes
`startPosition + startTimeOffset + pos`, which is the same additive trim-offset convention used
throughout the engine: `src/tracks/InstrumentTrack.cpp:876`, `:900` and `:915`,
`src/gui/clips/MidiClipView.cpp:809`, and `src/core/ControlCommandsClipTrim.cpp:134`. The finding's
proposed subtraction contradicts all of them; the additive form is authoritative.
Commit: none
Test: none
Red/green: none.
Leftovers: none.

### Cluster 1145 — Chord-follow edits do not notify or mirror the changed clip
Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsNoteHarmony.cpp:95` returns through the shared `finish()` helper at
`src/core/ControlCommandsNoteShared.h:64-75`, which re-sorts the notes, calls `updateLength()`, emits
`dataChanged()` and calls `ClipLinks::mirrorContent`. The notify-and-mirror the finding says is
missing is already on that path.
Commit: none
Test: none
Red/green: none.
Leftovers: none.

Leftovers: three of the six findings are false positives against the current tree; nothing in this batch
is half-done and the working tree is clean.