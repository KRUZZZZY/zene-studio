**Verdict: FIXED**
new-p14-41: FIXED
new-p14-72: FIXED

# Cluster new-p14-41 + new-p14-72: a note edit that changes nothing took an undo step

**Verdict: FIXED.** Both findings are real and they are ONE defect: the note/scale edit
handlers took the MidiClip journal checkpoint BEFORE they knew whether the edit changed
anything, so an edit that moved nothing still pushed a step onto the undo stack, dirtied
the project and claimed an inverse it did not have. The fix is the one rule the codebase
already follows everywhere else - the command answers "does this edit change anything?"
first, and takes the checkpoint only when the answer is yes - now in ONE shared helper
(`control::checkpointNoteEdit`, `include/ControlNoteShared.h:112`,
`src/core/ControlNoteShared.cpp:70`) that all four call sites go through.

## The shared root cause

The house rule, stated by the code itself:

- `src/core/ControlCommandsNoteTransform.cpp:234` (`note.transpose`): `if (changes) { clip->addJournalCheckPoint(); }`
  - the same shape at `:283` (`note.velocity_offset`) and `:322` (`note.velocity_scale`).
- `src/core/ControlCommandsNoteSlide.cpp:179` (`note.slide_clear`, Luna cluster 3904):
  "the checkpoint came before the flags were known, so a scope with no slide note in it
  still pushed a step onto the undo stack while publishing no edit at all. Take it only
  when at least one flag in scope is going to change."
- `src/core/ControlCommandsNoteAcross.cpp:129` (`note.across`, Luna cluster 2857): a
  handler that succeeded while changing nothing "left no journal checkpoint and described
  no transaction ... Counting it as an edit would report clips as edited and return a
  control.undo transaction that no undo step backs".

The four commands the findings name were the ones that had no such gate:

| command | before the fix | after the fix |
| --- | --- | --- |
| `note.scale` | `src/core/ControlCommandsNoteShape.cpp:101` `clip->addJournalCheckPoint();` - taken for factors of 1, which move nothing | `src/core/ControlCommandsNoteShape.cpp:131` `control::checkpointNoteEdit(clip, shape.changes);` |
| `note.transform_where` | `src/core/ControlCommandsNoteShape.cpp:478` `clip->addJournalCheckPoint();` - taken for an action that changes nothing (a transpose of 0, a velocity the notes already have, a mute flag they already carry) | `src/core/ControlCommandsNoteShape.cpp:556-557` |
| `note.slide_set` | `src/core/ControlCommandsNoteSlide.cpp:107` `clip->addJournalCheckPoint();` - taken for the flag the note already carries | `src/core/ControlCommandsNoteSlide.cpp:112` |
| `scale.snap_notes` | `src/core/ControlCommandsScaleEdit.cpp:103` `clip->addJournalCheckPoint();` - taken for a snap that moves nothing | `src/core/ControlCommandsScaleEdit.cpp:108-109` |

The fix has four parts, and they are ONE fix:

1. **The shared gate** - `control::checkpointNoteEdit(MidiClip*, bool changes)`
   (`include/ControlNoteShared.h:112`, `src/core/ControlNoteShared.cpp:70`): the checkpoint
   is taken only when the edit really changes something. All four call sites go through it.
2. **The question asked of the engine** - the engine itself now answers "would this move
   anything?" without touching a note: `NoteTransform::countTransposed`
   (`include/NoteTransform.h:131`, `src/core/NoteTransform.cpp:186`),
   `NoteTransform::countOffsetVelocity` (`:135`, `:216`) and `NoteTransform::countOffScale`
   (`:139`, `:379`). Each count is the SAME rule the matching edit applies - the transpose
   and the velocity offset now share one `transposedKey()` / `offsetVolume()` helper with
   the edits themselves, and the snap count asks the very `nearestInScaleKey()` the snap
   uses - so the answer and the edit that follows it cannot drift apart.
3. **The reply** - an edit that changed nothing has no step behind it, so it must not
   claim an inverse and must not dirty the project. `finish()`
   (`src/core/ControlCommandsNoteShared.h:67`) takes a `changed` flag: it skips
   `Song::setModified()` and the `__transaction` payload when nothing changed. The same
   rule is applied by hand in `note.slide_set` (`src/core/ControlCommandsNoteSlide.cpp:112`)
   and in `scale.snap_notes` (`src/core/ControlCommandsScaleEdit.cpp:139-147`), which build
   their own replies. A no-op edit now records no transaction, so the control surface does
   not claim a change either (`ControlRegistry.cpp:344` `changedNothing()`).

## Evidence

### new-p14-41 - no-op note edits took an undo checkpoint

Baseline (HEAD `b61e5f965`, unmodified tree):

- `src/core/ControlCommandsNoteShape.cpp:101` - `clip->addJournalCheckPoint();` before the
  scale loop, with no test of whether the factors change anything. `note.scale` with the
  DEFAULT factors (1.0 for both) puts every note back where it was and still takes a step.
- `src/core/ControlCommandsNoteShape.cpp:478` - the same call in `transformWhere`, taken
  whenever the filter matched at least one note, whether or not the action changed it.
- `src/core/ControlCommandsNoteSlide.cpp:107` - the same call in `slideSet`, taken before
  the flag was compared with the one the note carries.
- `src/core/ControlCommandsNoteShared.h:64` (before the fix) - `finish()` called
  `Engine::getSong()->setModified()` and wrote the `__transaction` payload unconditionally,
  so a no-op marked the project modified and advertised an inverse it did not have.

RED (the new test, on the unmodified product code): `test-1-red.log`, ctest EXIT 8 -

```
FAIL!  : NoteNoOpEditUndoStepTest::anIdentityScaleTakesNoUndoStep() Compared values are not the same
   Actual   (project->undoDepth()): 1
   Expected (depthBefore)         : 0
FAIL!  : NoteNoOpEditUndoStepTest::anExplicitUnitScaleTakesNoUndoStep() ...
FAIL!  : NoteNoOpEditUndoStepTest::aTransposeOfZeroTakesNoUndoStep() ...
FAIL!  : NoteNoOpEditUndoStepTest::mutingAnAlreadyMutedNoteTakesNoUndoStep() ...
FAIL!  : NoteNoOpEditUndoStepTest::settingTheSlideFlagItAlreadyHasTakesNoUndoStep() ...
Totals: 4 passed, 5 failed, 0 skipped, 0 blacklisted
```

(the two negative controls - a real change still takes exactly one step and one undo puts
the notes back - passed in the RED run as well: the test is not failing for an unrelated
reason.)

GREEN with the fix: `test-2-green.log` / `test-10-family-final.log`, ctest EXIT 0.

### new-p14-72 - a snap of a clip already in scale took an undo step

Baseline:

- `src/core/ControlCommandsScaleEdit.cpp:103` - `clip->addJournalCheckPoint();` before
  `NoteTransform::snapToScale()`, whose return value (the number of notes that moved) was
  only used for the reply - never to decide whether the step was needed.
- The old `tests/src/core/ControlNoteScaleVerbsTest.cpp:842-848` documented the defect as
  expected behaviour: "Two undos: the second snap is its own step (a no-op edit still
  records one, and undoing it leaves the first snap's result), then the first snap's step
  restores the keys."

RED: `test-1-red.log`, ctest EXIT 8 -

```
FAIL!  : ControlNoteScaleVerbsTest::aSecondSnapOfAClipAlreadyInScaleTakesNoUndoStep() Compared values are not the same
   Actual   (journal->undoDepth()): 72
   Expected (depthAfterTheSnap)   : 71
FAIL!  : ControlNoteScaleVerbsTest::snapMovesOnlyTheOutOfScaleNotesAndTheCheckpointTakesThemBack() ...
```

The W9 test was rewritten to the CORRECT expectation (one undo takes the keys back, because
the no-op snap left no step of its own) - it encoded the defect, so it could not stay.

GREEN: `test-2-green.log` / `test-10-family-final.log`, ctest EXIT 0.

### The regression tests

- `tests/src/core/NoteNoOpEditUndoStepTest.cpp` (new, registered in `tests/CMakeLists.txt`
  and in both source manifests):
  - `anIdentityScaleTakesNoUndoStep()` (`:148`) - `note.scale` with the default factors:
    no undo step, no transaction, the project not modified, the notes untouched.
  - `anExplicitUnitScaleTakesNoUndoStep()` (`:171`) - the same with explicit 1.0 factors.
  - `aTransposeOfZeroTakesNoUndoStep()` (`:192`) - `note.transform_where`, transpose 0.
  - `mutingAnAlreadyMutedNoteTakesNoUndoStep()` (`:213`) - `note.transform_where`, mute of
    an already muted note.
  - `settingTheSlideFlagItAlreadyHasTakesNoUndoStep()` (`:237`) - `note.slide_set` with the
    flag the note already carries, in both directions.
  - `aRealChangeStillTakesExactlyOneUndoStep()` (`:268`) and
    `aRealTransformAndSlideStillTakeOneUndoStepEach()` (`:289`) - the NEGATIVE controls: a
    real change still takes exactly one step and one undo puts the notes back.
- `tests/src/core/ControlNoteScaleVerbsTest.cpp`:
  - `aSecondSnapOfAClipAlreadyInScaleTakesNoUndoStep()` (`:855`) - the undo depth is
    unchanged by the no-op snap, and ONE undo takes the first snap back.
  - `snapMovesOnlyTheOutOfScaleNotesAndTheCheckpointTakesThemBack()` (`:801`) - the W9 test,
    corrected to the one-undo expectation.

The tests are deterministic: they read the journal depth, the recorded transactions, the
song's modified flag and the notes themselves. No timing, no sleep, no thread, no seed.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-41.r2/logs`
(all commands run from the worktree unless noted; `cmake` is the lane's guarded shim, which
allows `--build ... --target <t>` only).

| Step | Command | EXIT |
| --- | --- | --- |
| baseline build | `cmake --build build --target ControlNoteScaleVerbsTest -j20` (`build-0-baseline.log`) | 0 |
| baseline test | `cd build/tests && ctest -R ControlNoteScaleVerbsTest --output-on-failure` (`test-0-baseline.log`) | 0 (`1/1 ... Passed`) |
| RED build | `cmake --build build --target NoteNoOpEditUndoStepTest ControlNoteScaleVerbsTest` (`build-5-red.log`) | 0 |
| **RED** test | `cd build/tests && ctest -R "NoteNoOpEditUndoStepTest\|ControlNoteScaleVerbsTest" --output-on-failure` (`test-1-red.log`) | **8** (5 failures in `NoteNoOpEditUndoStepTest`, 2 in `ControlNoteScaleVerbsTest`) |
| GREEN build | `cmake --build build --target NoteNoOpEditUndoStepTest ControlNoteScaleVerbsTest` (`build-6-green.log`) | 0 |
| **GREEN** test | the same ctest (`test-2-green.log`) | **0** (`100% tests passed out of 2`) |
| family build | `cmake --build build --target NoteEditingCommandsTest NoteShapeValidationTest SlideNotesTest NoteTransformTest NoteToolsTest NoteCurvesAcrossTest ClipLinkTest ControlNoteExpressionCommandsTest NotePlaybackTest NoteRandomTest NoteSelectionIdentityTest NoteEditRangeTest` (`build-7-family.log`) | 0 |
| family test | `ctest -R "NoteEditingCommandsTest\|...\|NoteEditRangeTest\|ControlNoteScaleVerbsTest\|NoteNoOpEditUndoStepTest"` (`test-3-family.log`) | 8 - only `NotePlaybackTest` fails, and it fails for a missing PLUGIN (`monstro`, `nes` are not built in this tree), identically with and without the fix (see the 4b rows) |
| app build | `cmake --build build --target zene` (`build-14-zene.log`) | 0 |
| CCN fix build | `cmake --build build --target NoteNoOpEditUndoStepTest ControlNoteScaleVerbsTest NoteEditingCommandsTest NoteShapeValidationTest` (`build-11-ccn.log`) | 0 |
| CCN fix test | `ctest -R "NoteNoOpEditUndoStepTest\|ControlNoteScaleVerbsTest\|NoteEditingCommandsTest\|NoteShapeValidationTest"` (`test-7-after-ccn.log`) | 0 (`100% tests passed out of 4`) |
| **4b revert** | `git diff -- include/ControlNoteShared.h include/NoteTransform.h src/core/ControlCommandsNoteShape.cpp src/core/ControlCommandsNoteShared.h src/core/ControlCommandsNoteSlide.cpp src/core/ControlCommandsScaleEdit.cpp src/core/ControlNoteShared.cpp src/core/NoteTransform.cpp > $LOGS/fix.patch` then `git apply -R $LOGS/fix.patch` | 0 / 0 |
| **4b revert** build+test | `cmake --build build --target NoteNoOpEditUndoStepTest ControlNoteScaleVerbsTest` (`build-12-4b-revert.log`), `ctest -R "NoteNoOpEditUndoStepTest\|ControlNoteScaleVerbsTest" --output-on-failure` (`test-8-4b-revert.log`) | build 0, **ctest 8** - the same 7 failures as the RED run |
| **4b reapply** | `git apply $LOGS/fix.patch` | 0 |
| **4b reapply** build+test | `cmake --build build --target NoteNoOpEditUndoStepTest ControlNoteScaleVerbsTest` (`build-13-4b-reapply.log`), the same ctest (`test-9-4b-reapply.log`) | build 0, **ctest 0** (`100% tests passed out of 2`) |
| final family test | `ctest -R "ControlArmReversibilityTest\|...\|ControlNoteScaleVerbsTest\|NoteNoOpEditUndoStepTest"` (`test-10-family-final.log`) | **0** (`100% tests passed out of 25`) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gate-2-feature-check.log`) | **0** - `RESULT: PASS` (all eight rows PASS) |

Affected family: 25 test binaries executed in the final family run
(`test-10-family-final.log`), 25 passed, 0 failed. The first family run
(`test-3-family.log`, EXIT 8) also ran `NotePlaybackTest`, whose two failures are
environmental - the `monstro` and `nes` plugins are not built in this tree, so the test
compares the dummy instrument's name with the plugin's. The same two failures appear in
the 4b REVERT run (`test-4-4b-revert.log`), i.e. they are there without the fix as well;
`NotePlaybackTest` is not part of the final family run because of that.

### Gates

`feature_check.py <worktree>` (`gate-2-feature-check.log`), EXIT 0:

| Gate | EXIT |
| --- | --- |
| fork-sources | 0 |
| all-sources-reproduce | 0 |
| no-upstream-regression | 0 |
| check-namespace | 0 |
| no-tautology | 0 |
| doc-figures | 0 |
| complexity (ratchet) | 0 (`no new item in your files`) |
| file-length (ratchet) | 0 (`no new item in your files`) |

The first gate run (`gate-1-feature-check.log`, EXIT 1) reported
`NEW: new function over target: lmms::scaleNotes@81-126@src/core/ControlCommandsNoteShape.cpp (CCN 11)`.
The scaling computation was moved into `scaledShape()`
(`src/core/ControlCommandsNoteShape.cpp:95`), which is what the gate asks for; the gate was
not re-anchored and no limit was changed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** RED
  `test-1-red.log` ctest EXIT 8; GREEN `test-2-green.log` ctest EXIT 0; 4b revert
  `test-8-4b-revert.log` ctest EXIT 8 (the same seven assertions); 4b reapply
  `test-9-4b-reapply.log` ctest EXIT 0. `fix.patch` holds only the product hunks (the
  eight product files above); the test files stay in place while the fix is reverted, which
  is what makes the reverted run a proof.
- **Deterministic.** The tests read the journal depth, the recorded transaction list, the
  song's modified flag and the notes' own values. No wall-clock timing, no sleep, no race,
  no seed, no thread. The project is made "unmodified" through the public
  `Song::noteProjectSaved()`, which only clears the flag.
- **The fix is at the cause and complete.** The cause is the missing "does this edit change
  anything?" gate in front of the checkpoint, and the fix puts that gate in ONE place all
  four call sites go through (`control::checkpointNoteEdit`). Siblings checked: every
  `addJournalCheckPoint()` in the note/scale command layer was read - the ones that already
  had the gate (`note.transpose`, `note.velocity_offset`, `note.velocity_scale`,
  `note.slide_clear`) are untouched; the four that had none are the four this fix changes.
  The rule is also applied where the reply is built by hand (`note.slide_set`,
  `scale.snap_notes`) and in the shared `finish()` helper, so no caller is left claiming an
  inverse it does not have. The engine counts share their rule with the edits
  (`transposedKey()`, `offsetVolume()`, `nearestInScaleKey()`), so the answer and the edit
  cannot drift.
- **Each edit is ONE undo step, and the reversibility class stays honest.** A real edit
  still takes exactly one MidiClip checkpoint and one `control.undo` puts it back
  (`aRealChangeStillTakesExactlyOneUndoStep()`,
  `aRealTransformAndSlideStillTakeOneUndoStepEach()`, and the corrected W9 test). A no-op
  takes no step and records no transaction, so the control surface does not claim a
  change it cannot reverse (`ControlRegistry.cpp:344` `changedNothing()`; the same rule
  `note.across` already follows at `src/core/ControlCommandsNoteAcross.cpp:129`). No command
  was added, so the command-snapshot canary is untouched.
- **Nothing on the audio thread.** The changed code is the control-surface command layer
  (`src/core/ControlCommands*`), the control-surface shared helpers and the engine's
  note-transform helpers. None of it is reached from `processImpl`/`renderPeriod`/the
  per-period path: the new counts are called by the command handlers only, and they
  allocate nothing but the `std::vector` of pitch classes `snapToScale()` already built.
  No allocation, no lock, no wait, no logging was added to any audio path.
- **Old projects and saved data load exactly as before.** No file format, save path or
  serialised attribute was touched. `Note::setPos`/`setLength`/`setKey`/`setVolume` are
  unchanged; `NoteTransform::transpose()` and `offsetVelocity()` keep their exact formulas
  (the expressions moved into `transposedKey()`/`offsetVolume()`), so every saved note and
  every undo step recorded before this change restores to the same state.
- **Portability (Qt 5.15 / MSVC / macOS):** the new code uses `std::clamp`, `std::lround`,
  `std::any_of` and `QVector` - all already used in these files. No Qt-6-only API, no
  `windows.h` macro, no `std::filesystem`.
- **Verdict REAL (not ALREADY-FIXED, not FALSE-POSITIVE).** The baseline code is quoted
  above with its file and line: the unconditional `clip->addJournalCheckPoint()` calls in
  `src/core/ControlCommandsNoteShape.cpp:101` and `:478`,
  `src/core/ControlCommandsNoteSlide.cpp:107` and `src/core/ControlCommandsScaleEdit.cpp:103`,
  and the unconditional `setModified()` / `__transaction` in `finish()`
  (`src/core/ControlCommandsNoteShared.h:64`). The RED run measured the extra step on the
  unmodified code (`undoDepth` 1 instead of 0; 72 instead of 71).

## Files

Committed in two commits, one per bug, on `bugs/qwen-new-p14-41`:

1. `fix(cluster new-p14-41): a no-op note edit takes no undo step` -
   `include/ControlNoteShared.h`, `src/core/ControlNoteShared.cpp`,
   `include/NoteTransform.h`, `src/core/NoteTransform.cpp`,
   `src/core/ControlCommandsNoteShape.cpp`, `src/core/ControlCommandsNoteShared.h`,
   `src/core/ControlCommandsNoteSlide.cpp`,
   `tests/src/core/NoteNoOpEditUndoStepTest.cpp` (new), `tests/CMakeLists.txt`,
   `tests/fork-sources.txt`, `tests/all-sources.txt`.
2. `fix(cluster new-p14-72): a snap that moves nothing takes no undo step` -
   `src/core/ControlCommandsScaleEdit.cpp`, `include/NoteTransform.h`,
   `src/core/NoteTransform.cpp`, `tests/src/core/ControlNoteScaleVerbsTest.cpp`,
   `docs/qwen-fixes-2026-10/cluster-new-p14-41.md` (this report).

## Notes for the loop

- `cmake` in this sandbox is the lane's guarded shim: it refuses a bare configure
  ("configuring is the parent's job"). The build tree re-configures itself through
  `make`'s `cmake_check_build_system` rule, which is how the new test target appeared -
  building an EXISTING target once (`build-2-reconfig.log`) was enough.
- The new test file had to be registered in three places: `tests/CMakeLists.txt` (the
  target), `tests/fork-sources.txt` (the fork's own source manifest) and
  `tests/all-sources.txt` (re-derived with the manifest's own recipe - `REPRODUCES`).
