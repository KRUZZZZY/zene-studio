ALREADY-FIXED

# Cluster new-p14-39-port: note transforms did not mark the project modified

new-p14-39-port: ALREADY-FIXED

## Verdict: ALREADY-FIXED behaviour, and the ported coverage is now in the tree

The finding names `note.transpose`, `note.velocity_offset` and `note.velocity_scale`
(`src/core/ControlCommandsNoteTransform.cpp`): they changed notes, checkpointed the clip and
emitted `dataChanged()`, but never called `Song::setModified()`, so a project edited through one
of them was announced clean and the edit could be lost without a prompt.

The accepted fix for cluster `new-p14-39` is `396b7a5046` ("Mark project modified after note
transforms change notes", branch `bugs/qwen-new-p14-39`, not an ancestor of this branch). Two
halves of it exist here in another form:

- The **product behaviour** is already on this branch from a later, wider fix, `b663f21b0c`
  ("fix(note): every note edit that changes state marks the project modified", new-p17-14), an
  ancestor of HEAD (`git merge-base --is-ancestor b663f21b0c HEAD` **EXIT=0**). It replaced the
  per-file helper with one shared finalizer, `control::markProjectEdited`, called by every
  note-edit path with its own "did this change anything?" answer - the same conditional the
  accepted fix used (`markModifiedIfChanged`), so a no-op still leaves the project clean.
- The **second test the accepted commit added** (a no-op transform leaves the project clean) was
  genuinely missing here: the existing `noOpTransposeAdvertisesNoUndo` /
  `noOpVelocityOffsetAdvertisesNoUndo` / `noOpVelocityScaleAdvertisesNoUndo` slots
  (`tests/src/core/ControlNoteNoOpUndoTest.cpp:246`, `:259`, `:272`) count transactions only and
  never read `Song::isModified()`. This round adds that coverage as one slot,
  `noteTransformsTrackProjectDirtyState` (`tests/src/core/ControlNoteNoOpUndoTest.cpp:289`),
  which asserts both halves for all three verbs. No product file, command, registration list or
  build file changed.

### Code evidence (read this session)

| what | where |
|---|---|
| `note.transpose` dirties after the edit | `src/core/ControlCommandsNoteTransform.cpp:250` `markProjectEdited(changes);  // a moved key is project state` |
| `note.velocity_offset` dirties | `src/core/ControlCommandsNoteTransform.cpp:294` `markProjectEdited(changes);  // a moved velocity is project state` |
| `note.velocity_scale` dirties | `src/core/ControlCommandsNoteTransform.cpp:336` (same call, third handler) |
| the shared finalizer's contract ("every verb that changes note state has to say so ... cluster new-p17-14") | `include/ControlNoteShared.h:112-130`; declaration `void markProjectEdited(bool changed = true);` at `:130` |
| the implementation: a changed edit calls `Song::setModified()`, a no-op returns early | `src/core/ControlNoteShared.cpp:80-88`, `setModified()` at `:86` |
| the sibling note commands that end in the shared `notes::finish()` dirty too; `finish(..., changed=false)` skips it | `src/core/ControlCommandsNoteShared.h:77` |
| `note.randomize` (the GUI "Randomize velocities..." / "Humanize timing..." items) dirties | `src/core/ControlCommandsNoteRandom.cpp:385` `if (changed) { song->setModified(); }` |
| **the new test, both halves** | `tests/src/core/ControlNoteNoOpUndoTest.cpp:289` `noteTransformsTrackProjectDirtyState()` |
| the pre-existing dirty test for the same three verbs | `tests/src/core/ControlNoteNoOpUndoTest.cpp:501` `directNoteEditsMarkProjectDirty()` |

GUI path: the piano roll's note tools (`src/gui/NoteTransformActions.cpp:191`
`makeNoteTransformActions`, added to the toolbar at `src/gui/editors/PianoRoll.cpp:5471`) run
`note.randomize` / `note.velocity_scale` through `ControlRegistry::invoke`, i.e. the same
handlers above, so the user-facing path is covered by the same marking.

### What the new test asserts

`noteTransformsTrackProjectDirtyState` builds one note (key 60, velocity 100) and then:

- changed half - `checkProjectDirtyAfter` (which marks the project clean first) for
  `note.transpose {semitones: 2}`, `note.velocity_offset {delta: -10}` and
  `note.velocity_scale {factor: 0.5}`; each must leave `Song::isModified()` true;
- no-op half - `markProjectClean()`, then `{semitones: 0}`, `{delta: 0}`, `{factor: 1.0}`; each
  must report `notes_changed: 0` and leave `Song::isModified()` false.

Values are injected arguments, the dirty flag is reset through the product's own
`Song::noteProjectSaved()` seam (`:117`); no timing, sleeps or threads.

## Step 4b proof: RED without the marking, GREEN with it

The product change under test is the three `markProjectEdited(changes)` calls already in the
tree. `logs/fix.patch` is the diff that REMOVES them (3 deletions, one file):

```
git diff -- src/core/ControlCommandsNoteTransform.cpp > logs/fix.patch   # PATCH_EXIT=0, 28 lines
```

Commands and their EXIT codes (logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-39-port.r3/logs`):

1. `cmake --build build --target ControlNoteNoOpUndoTest -j4` + `cd build/tests && ctest -R
   ControlNoteNoOpUndoTest --output-on-failure` - build-1.log / test-1.log - **BUILD_EXIT=0
   TEST_EXIT=0**, 1/1 passed (the tree as it stands, with the new slot)
2. `./ControlNoteNoOpUndoTest noteTransformsTrackProjectDirtyState` - test-slot-1.log -
   **EXIT=0**, `PASS : ControlNoteNoOpUndoTest::noteTransformsTrackProjectDirtyState()`,
   `Totals: 3 passed, 0 failed`
3. removed the three calls, recorded `logs/fix.patch`; `md5sum` of the product file became
   `d61ae05a9fd212151bb3a1b49cf4f409`; `cmake --build build --target ControlNoteNoOpUndoTest
   -j4` + `ctest -R ControlNoteNoOpUndoTest` - build-red.log / test-red.log - **BUILD_EXIT=0
   TEST_EXIT=8**:
   ```
   FAIL!  : ControlNoteNoOpUndoTest::noteTransformsTrackProjectDirtyState() 'failures.isEmpty()' returned FALSE. (note.transpose left the project clean
   FAIL!  : ControlNoteNoOpUndoTest::directNoteEditsMarkProjectDirty() 'failures.isEmpty()' returned FALSE. (note.transpose left the project clean
   Totals: 19 passed, 2 failed, 0 skipped, 0 blacklisted, 583ms
   0% tests passed, 1 tests failed out of 1
   ```
4. `git apply -R logs/fix.patch` - **APPLY_REVERSE_EXIT=0**; `md5sum` back to
   `209f1dded6257b12ed77ad7d49a98ab8`; `git status --short` = `M tests/src/core/ControlNoteNoOpUndoTest.cpp`
   only
5. `cmake --build build --target ControlNoteNoOpUndoTest NoteTransformTest
   NoteNoOpEditUndoStepTest NoteEditingCommandsTest ControlNoteScaleVerbsTest
   NoteTransformActionsTest -j4` + `ctest -R ControlNoteNoOpUndoTest` +
   `ctest -R 'ControlNoteNoOpUndo|NoteTransform|NoteNoOpEditUndoStep|NoteEditingCommands|ControlNoteScaleVerbs|NoteTransformActions'`
   - build-green.log / test-green.log / test-family.log - **BUILD_EXIT=0 TEST_EXIT=0
   FAMILY_EXIT=0**, `100% tests passed out of 6` (ControlNoteScaleVerbsTest, NoteTransformTest,
   NoteEditingCommandsTest, NoteNoOpEditUndoStepTest, NoteTransformActionsTest,
   ControlNoteNoOpUndoTest)
6. `cmake --build build --target zene -j4` - build-zene.log - **ZENE_BUILD_EXIT=0**
7. `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -
   gates.log - **GATES_EXIT=0**:

```
feature-check kads-new-p14-39-port (base 3436a808b, 2 changed files; logs in .../build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

**ASK: The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
RED: with `fix.patch` applied (the three `markProjectEdited(changes)` calls removed), rebuild
BUILD_EXIT=0, `ctest -R ControlNoteNoOpUndoTest` **TEST_EXIT=8**, and the failure is the new
slot: `FAIL! : ControlNoteNoOpUndoTest::noteTransformsTrackProjectDirtyState() 'failures.isEmpty()'
returned FALSE. (note.transpose left the project clean ...)` (test-red.log). GREEN after
`git apply -R fix.patch` (APPLY_REVERSE_EXIT=0): rebuild BUILD_EXIT=0, **TEST_EXIT=0**, 1/1
passed (test-green.log), and the slot alone passes (`PASS`, EXIT=0, test-slot-1.log). Family:
FAMILY_EXIT=0, 6/6.

**ASK: The test is deterministic - no wall-clock timing, sleeps or races.**
The new slot (`tests/src/core/ControlNoteNoOpUndoTest.cpp:289`) uses no timing at all. The
dirty-state seam is the product's own: `markProjectClean()` (`:117`) calls
`Song::noteProjectSaved(song->projectFileName())`, the assertion reads
`Engine::getSong()->isModified()`. Every input is an injected argument (`semitones`, `delta`,
`factor`), the fixture is one note built by `makeOneNoteFixture` (key 60, velocity 100), and
each assertion is `QVERIFY`/`QCOMPARE` on that state. No thread, no event loop, no sleep.

**ASK: The fix is at the cause and complete - every caller and sibling checked and consistent.**
The cause (the three transform handlers not saying "this changed") and its siblings were walked
in the command family that owns note edits:
`note.transpose` / `note.velocity_offset` / `note.velocity_scale` - `markProjectEdited(changes)`
(`src/core/ControlCommandsNoteTransform.cpp:250`, `:294`, `:336`);
`note.add`, `note.remove`, `note.edit`, `note.slide_set`, `note.slide_clear` -
`markProjectEdited(...)` (`src/core/ControlCommandsNotes.cpp:144`, `:206`, `:377`, `:445`,
`src/core/ControlCommandsNoteEdit.cpp:465`, `src/core/ControlCommandsNoteSlide.cpp:91`);
`note.probability` - `src/core/ControlCommandsNoteProbability.cpp:156`;
`note.randomize` - `src/core/ControlCommandsNoteRandom.cpp:385`;
`note.expression_stream_set` - `src/core/ControlCommandsNoteExpression.cpp:342`, `:401`, `:504`;
`note.generate_melody` - `src/core/ControlCommandsNoteMelody.cpp:175`, `:178`, `:220`;
`note.scale`, `note.ratchet`, `note.add_many`, `note.restore_timing`, `note.transform_where`,
`note.copy/cut/paste/duplicate/slice/remove_many` - the shared `notes::finish()`
(`src/core/ControlCommandsNoteShape.cpp:137`, `:175`, `:238`, `:264`, `:572`;
`src/core/ControlCommandsNoteClipboard.cpp:111`, `:160`, `:224`, `:279`, `:309`;
`src/core/ControlCommandsNoteShared.h:77`). So every note edit that changes state dirties the
project, through one of two paths that agree on the same conditional ("no change, no step, no
dirty"), and the new test pins both halves for the three verbs the finding names.

**ASK: No collateral damage - old projects and saved data load as before, no new limit.**
No product code was changed this round (`git diff` on the commit touches only
`tests/src/core/ControlNoteNoOpUndoTest.cpp` and this report), so loading, saving, undo depth and
every command's behaviour are what the branch already shipped. The new slot only drives the
registry and reads the song's dirty flag; it adds no command, no argument schema and no limit.
Nothing new runs on the audio thread - the marking it exercises lives in a command handler on
the control/UI thread - and no control command was added, so the ControlCommandsSnapshot canary
does not move.

**ASK: ALREADY-FIXED - the code (file:line) or the intended contract that shows it.**
`src/core/ControlCommandsNoteTransform.cpp:250`, `:294`, `:336` call
`control::markProjectEdited(changes)`; that helper is declared with its rule at
`include/ControlNoteShared.h:112-130` and defined at `src/core/ControlNoteShared.cpp:80-88`
(`Song::setModified()` at `:86`). The commit that put it there, `b663f21b0c` (new-p17-14), is an
ancestor of HEAD (`git merge-base --is-ancestor b663f21b0c HEAD` EXIT=0), and step 4b above
shows the behaviour depends on exactly those three calls.

## Other findings

- `note.scale`, `note.ratchet`, `note.add_many` and `note.restore_timing` take a
  `clip->addJournalCheckPoint()` unconditionally (`src/core/ControlCommandsNoteShape.cpp:157`,
  `:235`, `:255`) and rely on their command's own refusal for a no-op; only `note.scale` asks
  first (`control::checkpointNoteEdit(clip, shape.changes)` at `:131`). Out of scope for this
  cluster and already covered by clusters new-p14-41 / new-p14-72; noted, not changed.
- `build/tests` has no CMake target `ControlNoteEditor`; the note editor is exercised by the
  Python control test `tests/control-note-editor.py`. Cost me one failed build invocation in the
  previous round, not a defect.
