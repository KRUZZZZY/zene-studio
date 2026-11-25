ALREADY-FIXED

# Cluster new-p14-39-port: note transforms did not mark the project modified

new-p14-39-port: ALREADY-FIXED

## Verdict: ALREADY-FIXED on this branch

The finding names `note.transpose`, `note.velocity_offset` and `note.velocity_scale`
(`src/core/ControlCommandsNoteTransform.cpp`): they changed notes, checkpointed the clip and
emitted `dataChanged()`, but never called `Song::setModified()`, so a project edited through one
of them was announced clean and the edit could be lost without a prompt.

The accepted fix for cluster `new-p14-39` is `396b7a5046` ("Mark project modified after note
transforms change notes"), on branch `bugs/qwen-new-p14-39` (not an ancestor of this branch).
The *content* of that fix is already on this branch from a later, wider fix: `b663f21b0c`
("fix(note): every note edit that changes state marks the project modified (new-p17-14)"),
which is an ancestor of HEAD (`git merge-base --is-ancestor b663f21b0c HEAD` EXIT=0). That
commit replaced the per-file helper with one shared finalizer, `control::markProjectEdited`,
and every note-edit path on this branch calls it with its own "did this change anything?"
answer - the same conditional the ported fix used (`markModifiedIfChanged`), so a no-op still
leaves the project clean.

There is therefore nothing left to port: the behaviour the accepted fix produced is present,
and a test on this branch fails when that marking is removed and passes when it is there
(step 4b proof below).

### Code evidence (read this session)

| what | where |
|---|---|
| `note.transpose` dirties after the edit | `src/core/ControlCommandsNoteTransform.cpp:250` `markProjectEdited(changes);  // a moved key is project state` |
| `note.velocity_offset` dirties | `src/core/ControlCommandsNoteTransform.cpp:294` `markProjectEdited(changes);  // a moved velocity is project state` |
| `note.velocity_scale` dirties | `src/core/ControlCommandsNoteTransform.cpp:336` (same call, third handler) |
| the shared finalizer's contract ("every verb that changes note state has to say so ... cluster new-p17-14") | `include/ControlNoteShared.h:112-130`; declaration `void markProjectEdited(bool changed = true);` at `:130` |
| the implementation: a changed edit calls `Song::setModified()`, a no-op returns early | `src/core/ControlNoteShared.cpp:80-88`, `setModified()` at `:86` |
| the sibling note commands that end in the shared `notes::finish()` dirties too, and `finish(..., changed=false)` skips it | `src/core/ControlCommandsNoteShared.h:77` (`markProjectEdited();` at `:77`) |
| `note.randomize` (the GUI "Randomize velocities..." / "Humanize timing..." items) dirties | `src/core/ControlCommandsNoteRandom.cpp:385` `if (changed) { song->setModified(); }` |
| the regression test that covers the three transforms | `tests/src/core/ControlNoteNoOpUndoTest.cpp:459` `directNoteEditsMarkProjectDirty()`, the three calls at `:466`, `:468`, `:470` |

GUI path: the piano roll's note tools (`src/gui/NoteTransformActions.cpp:191`
`makeNoteTransformActions`, added to the toolbar at `src/gui/editors/PianoRoll.cpp:5471`) run
`note.randomize` / `note.velocity_scale` through `ControlRegistry::invoke`, i.e. the same
handlers above - so the user-facing path is covered by the same marking. No product file was
changed by this job, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are
untouched.

## Step 4b proof: the existing test catches exactly this bug

The product change to revert is the marking already in the tree (there is no change of mine to
revert). I removed the three `markProjectEdited(...)` lines from
`src/core/ControlCommandsNoteTransform.cpp`, built, ran the test, then restored them with
`git apply -R` of the recorded patch and re-ran.

Commands and their EXIT codes (logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-39-port.r2/logs`):

1. `cmake --build build --target ControlNoteNoOpUndoTest -j4` - build-1.log - **EXIT=0**
2. `cd build/tests && ctest -R ControlNoteNoOpUndoTest --output-on-failure` - test-1.log -
   **EXIT=0** (1/1 passed) - the tree as it stands
3. temporarily removed the three `markProjectEdited(...)` calls;
   `git diff -- src/core/ControlCommandsNoteTransform.cpp > logs/red-revert.patch` -
   **EXIT=0**, `1 file changed, 3 deletions(-)`
4. `cmake --build build --target ControlNoteNoOpUndoTest -j4` - build-red.log - **EXIT=0**
5. `cd build/tests && ctest -R ControlNoteNoOpUndoTest --output-on-failure` - test-red.log -
   **EXIT=8**, `0% tests passed, 1 tests failed out of 1`, and the failure names exactly the
   three transforms:
   ```
   FAIL!  : ControlNoteNoOpUndoTest::directNoteEditsMarkProjectDirty() 'failures.isEmpty()' returned FALSE. (note.transpose left the project clean
   note.velocity_offset left the project clean
   note.velocity_scale left the project clean)
      Loc: [../../tests/src/core/ControlNoteNoOpUndoTest.cpp(502)]
   Totals: 19 passed, 1 failed, 0 skipped, 0 blacklisted, 641ms
   ```
6. `git apply -R logs/red-revert.patch` - **EXIT=0**; `git status --short` empty and
   `md5sum src/core/ControlCommandsNoteTransform.cpp` =
   `209f1dded6257b12ed77ad7d49a98ab8`, the same digest as before step 3
7. `cmake --build build --target ControlNoteNoOpUndoTest -j4` - build-green.log - **EXIT=0**
8. `cd build/tests && ctest -R ControlNoteNoOpUndoTest --output-on-failure` - test-green.log -
   **EXIT=0** (1/1 passed, 100%)
9. `cmake --build build --target ControlNoteNoOpUndoTest NoteTransformTest
   NoteNoOpEditUndoStepTest NoteEditingCommandsTest ControlNoteScaleVerbsTest
   NoteTransformActionsTest -j4` - build-family2.log - **EXIT=0**
10. `cd build/tests && ctest -R
    'ControlNoteNoOpUndo|NoteTransform|NoteNoOpEditUndoStep|NoteEditingCommands|ControlNoteScaleVerbs|NoteTransformActions'
    --output-on-failure` - test-family.log - **EXIT=0**, `100% tests passed out of 6`
11. `cmake --build build --target zene -j4` - build-zene.log - **EXIT=0** (the object the
    temporary revert touched is rebuilt from the restored source)

One failed step, recorded for honesty: an earlier family build (`build-family.log`) exited
**EXIT=2** with `make: *** No rule to make target 'ControlNoteEditor'` - `ControlNoteEditor`
is the Python control test `tests/control-note-editor.py`, not a CMake target. The rerun
without it is step 9 above.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` - gates.log -
**EXIT=0**:

```
feature-check kads-new-p14-39-port (base 3436a808b, 0 changed files; logs in .../build/feature-check)
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
There is no product change of mine; I reverted the marking that is in the tree. RED: test-red.log,
`ctest -R ControlNoteNoOpUndoTest` **EXIT=8**, `directNoteEditsMarkProjectDirty` failing with
"note.transpose / note.velocity_offset / note.velocity_scale left the project clean". GREEN after
restoring (`git apply -R` EXIT=0): test-green.log **EXIT=0**, 1/1 passed. Family: test-family.log
**EXIT=0**, 6/6.

**ASK: The test is deterministic - no wall-clock timing, sleeps or races.**
`directNoteEditsMarkProjectDirty` (`tests/src/core/ControlNoteNoOpUndoTest.cpp:459`) uses no
timing at all: `markProjectClean()` (`:117`) calls `Song::noteProjectSaved(song->projectFileName())`,
then the command runs and the assertion reads `Engine::getSong()->isModified()` (`:132`). All
values are injected args (`semitones`, `delta`, `factor`), the fixture is one note built by
`makeOneNoteFixture`, and every assertion is a Qt Test `QVERIFY`/`QCOMPARE` on that state.

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
dirty").

**ASK: No collateral damage - old projects and saved data load as before, no new limit.**
No product code was changed by this job (`git diff` empty; the only commit adds this report), so
loading, saving, undo depth and every command's behaviour are byte-for-byte what the branch
already shipped. Nothing was added to the audio thread; the marking is in command handlers on
the control/UI thread, and no control command was added (nothing moved in the
ControlCommandsSnapshot canary).

**ASK: ALREADY-FIXED - the code (file:line) or the intended contract that shows it.**
`src/core/ControlCommandsNoteTransform.cpp:250`, `:294`, `:336` call
`control::markProjectEdited(changes)`; that helper is declared with its rule at
`include/ControlNoteShared.h:112-130` and defined at `src/core/ControlNoteShared.cpp:80-88`
(`Song::setModified()` at `:86`). The commit that put it there, `b663f21b0c` (new-p17-14), is an
ancestor of HEAD (`git merge-base --is-ancestor b663f21b0c HEAD` EXIT=0). The step-4b run above
shows the behaviour depends on exactly those three calls.

## Other findings

- `note.scale` refuses a no-op with a failed transaction but takes a checkpoint before asking
  (`control::checkpointNoteEdit(clip, shape.changes)` at `src/core/ControlCommandsNoteShape.cpp:131`
  - the conditional is there, `addJournalCheckPoint()` at `:157`, `:235`, `:255` is unconditional).
  Out of scope for this cluster and already covered by clusters new-p14-41 / new-p14-72; noted,
  not changed.
- `build/tests` has no CMake target `ControlNoteEditor`; the note editor is exercised by the
  Python control test `tests/control-note-editor.py`. Cost me one failed build invocation, not a
  defect.
