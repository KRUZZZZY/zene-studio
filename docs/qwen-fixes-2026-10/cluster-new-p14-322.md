FIXED

# Cluster new-p14-322: undoing a v2 project silently changes its save format

## Finding

Undo restores a snapshot through `Song::restoreUndoSnapshot`, which runs a full `loadProject()` on a
plain-XML temporary `.mmp`. An accepted load sets the session's container flag from the document it read:

- `src/core/Song.cpp:2053` - `m_containerV2Project = dataFile.loadedFromContainer();` (false for a snapshot)
- `src/core/Song.cpp:1688` - `clearProject()` also resets it to false before the load
- `src/core/Song.cpp:2404` - `plannedSaveFormat()` decides a bare `.mmpz` save from that flag
  (`m_containerV2Project || globalContainerV2()`), so a restore that loses the flag makes the next save legacy.

Before the fix, `restoreUndoSnapshot` held the rest of the per-session state across its load but not this flag.
Reproduced by the regression test (RED below): open a v2 container, undo, and `isContainerV2Project()` is false.

## Design

A restore takes the flag of the session it puts back, and that flag is captured with the snapshot:

- **Ordinary undo of the same project** (`UndoHistory` structural steps, `src/core/UndoHistory.cpp:122`, `:125`):
  `restoreUndoSnapshot` keeps the session's current format (`src/core/SongUndoSnapshot.cpp:121`, `:131`). These
  snapshots are persisted with the project, so their bytes are not changed.
- **Checkpoint restores** (`project.open` and `script.run` undo, redo and rollback): the format is captured with the
  checkpoint and set after a restore that took effect. The new `SessionCheckpoint` fields are
  `containerV2Before` / `containerV2After` (`include/SessionCheckpoint.h:72-73`), set at `src/core/SessionCheckpoint.cpp:191`
  (pre-action) and `:138`, `:251` (post-action). The helper `restoreCheckpointSession` (`src/core/SessionCheckpoint.cpp:53-60`)
  calls `restoreUndoSnapshot`, then `setContainerV2Project` (the existing public setter, precedent
  `src/core/ControlCommandsProjectFiles.cpp:279`) only when the restore returned true.
- **Checkpoint restore call sites** (all now carry the format): undo and redo in `pushRestoreStep`
  (`src/core/SessionCheckpoint.cpp:73`, `:81`); the `project.open` rollbacks (`:131`, `:141`); the `script.run` rollbacks
  (`:240`, `:255`).
- **Document-command rollback** `restoreSession` in `src/core/ControlCommandsDocuments.cpp:175-183` (the session
  replaced by a built-in recipe): the format is captured at `:215` and set on a successful restore.
- `project.open` itself (`src/core/SessionCheckpoint.cpp:220`, the non-undo `loadProject` caller) and `main.cpp` keep taking the format from the file they open.
- `Song.h` is untouched: `setContainerV2Project` already exists, so no header besides `SessionCheckpoint.h` changed.

## Tests

All in `tests/src/core/ProjectContainerSaveTest.cpp` (already registered in `tests/CMakeLists.txt:414`):
- `undoingAV2ProjectKeepsItsContainerFormat` - ordinary undo of a v2 project (round-1 regression).
- `undoingALegacyProjectKeepsItsLegacyFormat` - guard: a legacy session stays legacy across an undo.
- `aRefusedUndoRestoreKeepsTheContainerFormat` - guard: a snapshot the loader refuses changes nothing.
- `undoingAProjectOpenRestoresTheDisplacedContainerFormat` (round 2, the cross-format regression). A legacy session with
  unsaved work is displaced by `project.open` of a v2 file; `control.undo` must restore it as legacy (the state matches and a
  bare `.mmpz` save is `LegacyCompressed`); `control.redo` must restore the opened v2 project (the state matches and a bare save is
  `ZipContainer`).
- `aRefusedProjectOpenRollsBackToTheDisplacedContainerFormat` (round 2, checkpoint rollback). The undo budget is set to the
  displaced snapshot plus 64 bytes (`BudgetGuard`), so the open loads the v2 project, cannot keep its redo state, and rolls back.
  The refusal must name "its redo state" (so the rollback path is the one tested) and the displaced legacy format must return.

No new source files, so the registration files need no change. No new control commands; no audio-thread code; no new undo step.

## Commands and exit codes

Build environment: the first build failed with `ccache: ... Permission denied` on ccache's temp directory, which is outside the
write-jail. Every build sets `CCACHE_TEMPDIR` to an on-disk directory under `$TMPDIR`; nothing else in the build environment was changed.

Round 2 logs are in `runs/new-p14-322.r2/logs`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| RED build (round-1 product code, final tests) | `git apply -R logs/fix-r2-final.patch`, then `cmake --build build --target zene ProjectContainerSaveTest -j4` | 0 | build-4-red-final.log |
| RED run | `cd build/tests && ctest -V -R "^ProjectContainerSaveTest$" --output-on-failure` | 8 | test-4-red-final.log |
| GREEN build | `git apply logs/fix-r2-final.patch`, then `cmake --build build --target zene ProjectContainerSaveTest ... -j4` | 0 | build-5-green-final.log |
| GREEN family run | `cd build/tests && ctest -V -R "^(ProjectContainerSaveTest\|SessionCheckpointTest\|SessionCheckpointBudgetTest\|UndoBoundsTest\|UndoHistoryOptionsTest\|ControlProjectTemplateTest\|ProjectContainerTest\|ProjectContainerEntriesTest\|UndoHonestyTest\|UndoHistoryPersistenceTest)$" --output-on-failure` | 8 (one env failure, below) | test-5-green-final-family.log |

Round-2 patch captured with `git diff -- include/SessionCheckpoint.h src/core/SessionCheckpoint.cpp src/core/ControlCommandsDocuments.cpp src/core/SongUndoSnapshot.cpp > logs/fix-r2-final.patch` (product files only).

### RED (round-1 product code with the final tests)

```
FAIL!  : ProjectContainerSaveTest::undoingAProjectOpenRestoresTheDisplacedContainerFormat() '!song->isContainerV2Project()' returned FALSE. (undo must restore the displaced legacy format)
   Loc: [../../tests/src/core/ProjectContainerSaveTest.cpp(635)]
FAIL!  : ProjectContainerSaveTest::aRefusedProjectOpenRollsBackToTheDisplacedContainerFormat() '!song->isContainerV2Project()' returned FALSE. (the rollback must put the displaced legacy format back)
   Loc: [../../tests/src/core/ProjectContainerSaveTest.cpp(672)]
Totals: 18 passed, 2 failed
```

The round-1 ordinary-undo tests and the two guards pass on that code, as they should.

### GREEN (final)

```
Totals: 20 passed, 0 failed    (ProjectContainerSaveTest, from test-5-green-final-family.log; includes init/cleanup entries)
```

Executed count for `^ProjectContainerSaveTest$`: 20 entries, 20 passed, including all five cases above and the two init/cleanup slots.

### Family (final)

| Test | Result |
| --- | --- |
| ProjectContainerSaveTest | passed (20/20 entries) |
| ProjectContainerTest | passed (12) |
| ProjectContainerEntriesTest | passed (15) |
| UndoBoundsTest | passed (12) |
| UndoHistoryOptionsTest | passed (5) |
| SessionCheckpointTest | passed (13) |
| SessionCheckpointBudgetTest | passed (4) |
| ControlProjectTemplateTest | passed (5) |
| UndoHonestyTest | passed (13, 4 skipped) |
| UndoHistoryPersistenceTest | 1 failed, 10 passed |

`UndoHistoryPersistenceTest::aSavedHistoryUndoesToEveryPreEditState` fails at its fixture
(`UndoHistoryPersistenceTest.cpp:210`: "the baseline needs a real instrument (tripleoscillator), a parameter and a sample clip").
The fixture `buildBaseline()` (`tests/src/core/UndoHistoryTestSupport.h:168`) loads the `tripleoscillator` instrument, the
plugin scan in the log reports 0 native plugins, and the fixture never calls a restore. The same signature appeared in round 1.
This is the plugin environment of this worktree, not the change; I have not run that test on an unmodified base in this session.

Not run: `ControlUndoStructuralTranscript`, `RevisionTimelineTest`, `ControlContainerV2`, `ProjectContainerSaveAsGuiTest` are registered
but not built in this worktree.

## Coverage not in a test

- `script.run` rollback (`src/core/SessionCheckpoint.cpp:240`, `:255`) and the `newFromRecipe` rollback
  (`src/core/ControlCommandsDocuments.cpp:183`, `:240`) use the same restore and format logic, but no deterministic test drives them.
  A script that saves as v2 must write inside the script project directory (`src/core/ScriptEngine.cpp:654`, `:766`), which I did
  not set up in the test. A recipe rollback needs a failure injected inside `project.new` or the template write, and there is no seam
  for that. These are changed by reading, not by a test.
- The `project.open` redo-capture refusal (`src/core/SessionCheckpoint.cpp:131`, "could not be captured for redo") is the same helper
  call but is not triggered by any test.

## Gates

`python3 .../feature_check.py <worktree>` could not complete in this write-jail: it writes its output to
`<worktree>/../<name>-feature-check/` (outside the write-jail) and its ratchet baseline needs a `git worktree add` in the shared main repo
(`gates-1-feature-check.log`, EXIT 1, PermissionError). I ran every gate command it calls from the worktree, with logs in the LOG FOLDER:

| Gate | Command | EXIT | Note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | |
| check-namespace | `tests/scripted/check-namespace` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| complexity | `bash tests/complexity-gate.sh --check` | 1 | red on the train; no REGRESSION line names a changed file |
| file-length | `bash tests/file-length-gate.sh --check` | 1 | red on the train; no REGRESSION line names a changed file (final run) |

Round 2 first ran file-length and reported `tests/src/core/ProjectContainerSaveTest.cpp (514)` as a new file over 500 code lines (the
gate counts code lines, not blanks or comments; that file had 388 before this work). I trimmed my own added lines (no test or
assertion removed, one redundant `QVERIFY(budget.ok)` dropped because a failed budget already fails the refusal assertion). The file is now
exactly 500 code lines, the limit, and the final run reports nothing for it.

## Self-check

1. **RED/GREEN proof.** Yes, for both round-2 regressions: with the round-2 product hunk reversed, the two cross-format tests fail
   (`ProjectContainerSaveTest.cpp:635` and `:672`, ctest EXIT 8, 18 passed / 2 failed); with it restored, all 20 entries pass (EXIT 0). The
   ordinary-undo regression proof from round 1 is still valid: the same test passes on the current tree and failed without its product hunk.
2. **Deterministic.** No timers, sleeps or threads. Snapshots are captured in-process; tempos and track names are fixed; the refused
   open uses a budget derived from the displaced session's own snapshot size plus a fixed 64-byte margin (the size of two captures
   of the same session can differ in compressed bytes only by a few bytes). Files live in a `QTemporaryDir`. The budget is restored by
   `BudgetGuard`'s destructor however the test leaves.
3. **At the cause, complete.** The cause is the single container-flag assignment in the load path (`Song.cpp:2053`, `:1688`) and the
   restores that did not carry the format of the session they put back. Every checkpoint restore now carries its captured format
   (see Design). The ordinary same-project undo keeps the session's format on purpose. The `project.open` and `main.cpp` loads keep
   their file-derived format.
4. **No collateral damage.** Guards cover a legacy session across an undo and a refused undo restore. `loadProject`, `Song.h` and the
   persisted snapshot bytes are unchanged. No limit or check was added; the budget is set only inside the new test. Old projects and
   saved data load exactly as before. `UndoHistoryPersistenceTest` fails at its fixture as described above.
5. **Not ALREADY-FIXED or FALSE-POSITIVE.** Verdict is FIXED; no "already fixed" claim is made.

## Gate status (not a claim of pass)

- `feature_check.py` could not run in this write-jail; the six simple gates pass; the two ratchet gates have no regression item in
  the changed files, but the feature_check table itself was not produced.
