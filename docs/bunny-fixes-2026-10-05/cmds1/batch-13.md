# cmds1 batch-13 — control commands ControlCommands[A-M]*

Worktree `zene-bunny-cmds1`, branch `bugs/bunny-cmds1-2026-10-05`. Six findings:
5 FIXED, 1 REAL-UNTESTED (cluster 5651 — see below).

### Cluster 4770 — Import and paste retain source note IDs

Verdict: FIXED

Evidence: `src/core/ControlCommandsImportTracks.cpp:82-90` — `detach()` stripped the
stable id of `track`, `effect` and the three clip kinds, but `note` was absent from
`withIds`. `Note::saveState` always writes the note id (`src/core/Note.cpp:354`) and
`ProjectIds::isDocumentElement` returns true for a pasted/imported `<track>` payload
(`src/core/ProjectIds.cpp:250-263`), so a carried id stays live in this project.

Commit: 06e1050a3

Test: ProjectToolsTest::pastedNotesAreGivenFreshIds

Red/green:
- `cmake --build build --target zene ProjectToolsTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
- `cd build/tests && ctest -R '^ProjectToolsTest$' --output-on-failure > <abs>/build/bunny-t.log 2>&1; echo EXIT=$?`
  -> EXIT=8, `Totals: 10 passed, 2 failed`; `FAIL! pastedNotesAreGivenFreshIds() Compared values are not the same`
- after both import fixes: EXIT=0, `100% tests passed, 0 tests failed out of 1`

### Cluster 5647 — Two unsaved projects are treated as the same project when pasting tracks

Verdict: FIXED

Evidence: `src/core/ControlCommandsImportTracks.cpp:282` — `pasteTracks` compared the
clipboard document's `project` attribute with `Engine::getSong()->projectFileName()`, and
an unsaved project has no file name, so `"" == ""` made every pair of unsaved projects look
like one project and `settleFolder` (`:123-131`) kept the `folder` id.

Commit: 8cb1bc45a

Test: ProjectToolsTest::aPastedTrackFromAnUnsavedProjectDoesNotJoinAForeignFolder

Red/green:
- `cmake --build build --target zene ProjectToolsTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
- `cd build/tests && ctest -R '^ProjectToolsTest$' --output-on-failure > <abs>/build/bunny-t.log 2>&1; echo EXIT=$?`
  -> EXIT=8; `FAIL! aPastedTrackFromAnUnsavedProjectDoesNotJoinAForeignFolder() 'copy->parentFolder() != folder' returned FALSE.`
- after the fix: EXIT=0, `100% tests passed, 0 tests failed out of 1`

### Cluster 5643 — Partial input-transform updates can be reported as successful when rejected

Verdict: FIXED

Evidence: `src/core/ControlCommandsInputTransform.cpp:113` — the probe was a fresh
`MidiInputTransform` (the identity) while `MidiInputTransform::fromJson` compares a named
`key_low`/`key_high` against the object's CURRENT values
(`src/core/MidiInputTransform.cpp:192-197`); line 117 then discarded the real result.

Commit: 84fd96826

Test: MidiInputTransformTest::aPartialUpdateThatConflictsWithTheCurrentTransformIsRefused

Red/green:
- `cmake --build build --target zene MidiInputTransformTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
- `cd build/tests && ctest -R '^MidiInputTransformTest$' --output-on-failure > <abs>/build/bunny-t.log 2>&1; echo EXIT=$?`
  -> EXIT=8, `Totals: 12 passed, 1 failed`; `FAIL! aPartialUpdateThatConflictsWithTheCurrentTransformIsRefused() Compared values are not the same`
- after the fix: EXIT=0, `100% tests passed, 0 tests failed out of 1`

### Cluster 1096 — Key-name transaction omits the captured before-state

Verdict: FIXED

Evidence: `src/core/ControlCommandsKeyNames.cpp:72-76` — `storeNames` passed `{}` as the
transaction `before` payload while the undo closure captured the old names, so
`control.transactions` showed a reversible step with nothing to reverse to.

Commit: 2e9d422c9

Test: UndoHonestyTest::theKeyNameTransactionCarriesTheNamesItRestores

Red/green:
- `cmake --build build --target zene NoteEditingCommandsTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
- `cd build/tests && ctest -R '^NoteEditingCommandsTest$' --output-on-failure > <abs>/build/bunny-t.log 2>&1; echo EXIT=$?`
  -> EXIT=8; `FAIL! theKeyNameTransactionCarriesTheNamesItRestores() '!first->before.value("names").toArray().isEmpty()' returned FALSE.`
- after the fix: EXIT=0, `Totals: 22 passed, 0 failed`

The case was first written in `NoteEditingCommandsTest.cpp`, which sits at the 500-line
gate, and was moved to `UndoHonestyTest.cpp` (the transaction-honesty home). Re-verified
there: `cmake --build build --target zene UndoHonestyTest NoteEditingCommandsTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0,
`ctest -R '^(UndoHonestyTest|NoteEditingCommandsTest)$'` -> EXIT=0, 2/2 passed.

### Cluster 4836 — Omitting a key name silently deletes the existing name

Verdict: FIXED

Evidence: `src/core/ControlCommandsKeyNames.cpp:85-92` — `name` is not required by the
schema, and `args.value("name").toString()` turned an absent value into `""`, which is the
documented way to remove a label. The fix rejects the ABSENT case in the handler rather
than changing the schema, which would move the ControlCommandsSnapshot canary (rule 8); an
explicit `""` still removes.

Commit: 0cd40becd

Test: UndoHonestyTest::aKeyNameSetWithoutANameChangesNothing

Red/green:
- `cmake --build build --target zene NoteEditingCommandsTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
- `cd build/tests && ctest -R '^NoteEditingCommandsTest$' --output-on-failure > <abs>/build/bunny-t.log 2>&1; echo EXIT=$?`
  -> EXIT=8; `FAIL! aKeyNameSetWithoutANameChangesNothing() Compared values are not the same`
- after the fix: EXIT=0, `Totals: 22 passed, 0 failed`; re-verified at the new home with
  `ctest -R '^(UndoHonestyTest|NoteEditingCommandsTest)$'` -> EXIT=0, 2/2 passed.

### Cluster 5651 — MIDI clip export writes invalid long delta times

Verdict: REAL-UNTESTED

Evidence: `src/core/ControlCommandsLibraryClips.cpp:70-84` — `putVarLen` fills
`quint8 groups[5]` with `while (value != 0 && count < 5)`, so a value above `0x0fffffff`
(268,435,455 ticks) does emit five bytes, against the SMF four-byte limit. The encoder is
genuinely unbounded at five. What I could NOT establish headlessly is a reachable path that
produces such a delta: `smfOf` writes deltas between note ticks, note and clip positions
are bounded by `MaxSongLength` (9999 * 192 = 1,919,808 ticks, `include/Song.h:77`) on every
command path I traced, and every project-file position read I found (`SampleClip.cpp:598`,
`PatternClip.cpp:77`, `Clip.cpp:697`) is a plain `toInt()` of a stored value rather than a
validated one, so an unbounded tick could only come from a hand-authored or externally
generated file, which no in-tree command can produce. Rather than change product code
without a deterministic failing test (procedure step 2), the finding is recorded with this
evidence. The fix, if the owner's call, is a four-byte cap in `putVarLen` plus a rejection
of a delta above `0x0fffffff` in `smfOf`.

Commit: none

Test: none

Red/green: none

## Verification

Gates run before every one of the five commits, each EXIT=0:
`bash tests/complexity-gate.sh --check > build/bunny-cx.log 2>&1; echo EXIT=$?` and
`bash tests/file-length-gate.sh --check > build/bunny-fl.log 2>&1; echo EXIT=$?`.

Full re-run of every suite touching a changed file:
`cmake --build build --target zene NoteEditingCommandsTest UndoHonestyTest MidiInputTransformTest ProjectToolsTest LibraryMenusTest NoteEditorTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0;
`ctest -R '^(NoteEditingCommandsTest|UndoHonestyTest|MidiInputTransformTest|ProjectToolsTest|LibraryMenusTest|NoteEditorTest)$' --output-on-failure` -> EXIT=0, 6/6 passed.

Leftovers: cluster 5651 is left unfixed — the five-byte encoder is real but no deterministic
headless test reaches a delta above the SMF limit, so it needs either an owner decision to
cap the encoder without a test, or a test fixture that loads an out-of-range tick from a
hand-written project file.