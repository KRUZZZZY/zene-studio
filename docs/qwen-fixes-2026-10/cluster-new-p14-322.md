FIXED

# Cluster new-p14-322: undoing a v2 project silently changes its save format

## Finding

Undo restores a snapshot through `Song::restoreUndoSnapshot`, which runs a full `loadProject()` on a
plain-XML temporary `.mmp`. An accepted load sets the session's container flag from the document it read:

- `src/core/Song.cpp:2053` - `m_containerV2Project = dataFile.loadedFromContainer();` (false for the snapshot)
- `src/core/Song.cpp:1688` - `clearProject()` also resets it to false before the load
- `src/core/Song.cpp:2404` - `plannedSaveFormat()` decides a bare `.mmpz` save from that flag
  (`m_containerV2Project || globalContainerV2()`), so after an undo the next save is written as legacy.

Before the fix `restoreUndoSnapshot` held the rest of the per-session state across its load but not this flag.
Reproduced by the regression test (RED below): open a v2 container, undo, and `isContainerV2Project()` is false.

## Fix

`src/core/SongUndoSnapshot.cpp:117-129`: the flag is captured before the load (`:119`) and put back after it
(`:129`), next to the file-name restore. The file's header comment (`:44-47`) lists it among the held state.

Callers and siblings checked:
- Both `restoreUndoSnapshot` overloads (`include/Song.h:502-505`) run the same body, so both are covered.
- Restore call sites, all of which now keep the format: `src/core/UndoHistory.cpp:122,125`,
  `src/core/SessionCheckpoint.cpp:61,70,120,128,225,238`, `src/core/ControlCommandsDocuments.cpp:177`.
- The non-undo `loadProject` callers are unchanged on purpose: `project.open` (`src/core/SessionCheckpoint.cpp:205`)
  and `main.cpp` still take the format from the file they open.
- `Song.h` and every header are untouched, so no other target needs rebuilding.

## Tests

Added to the existing `tests/src/core/ProjectContainerSaveTest.cpp` (already registered in `tests/CMakeLists.txt:414`):
- `undoingAV2ProjectKeepsItsContainerFormat` - the regression. v2 open, capture, change tempo, restore; asserts the
  flag is still true, the state matches, and a bare save to `.mmpz` is a `ZipContainer`.
- `undoingALegacyProjectKeepsItsLegacyFormat` - guard: a legacy session stays legacy across an undo (passes before and after).
- `aRefusedUndoRestoreKeepsTheContainerFormat` - guard: a snapshot the loader refuses leaves the flag and state alone (passes before and after).

No new source files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no change.
No new control commands; no `ControlCommandsSnapshot` change. No audio-thread code and no new undo step.

## Commands and exit codes

Build environment note: the first build failed with `ccache: error: failed to create temporary file ... Permission denied`
(`build-1-red-test.log`, EXIT=2) from ccache's temp directory, which is outside the write-jail. Every build below sets
`CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-322` (on-disk `$TMPDIR`); that is the only change to the build environment.

| Step | Command | EXIT | Log (LOG FOLDER) |
| --- | --- | --- | --- |
| RED build | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-322 cmake --build build --target ProjectContainerSaveTest -j4` | 0 | build-2-red-test.log |
| RED run | `cd build/tests && ctest -R "^ProjectContainerSaveTest$" --output-on-failure` | 8 | test-1-red.log |
| GREEN build | `cmake --build build --target zene ProjectContainerSaveTest -j4` | 0 | build-3-green.log |
| GREEN run | `cd build/tests && ctest -R "^ProjectContainerSaveTest$" --output-on-failure` | 0 | test-2-green.log |
| 4b: save product diff | `git diff -- src/core/SongUndoSnapshot.cpp > logs/fix.patch` | 0 | fix.patch |
| 4b: revert fix | `git apply -R logs/fix.patch` | 0 | - |
| 4b: rebuild reverted | `cmake --build build --target zene ProjectContainerSaveTest -j4` | 0 | build-4-reverted.log |
| 4b: run reverted (must FAIL) | `cd build/tests && ctest -V -R "^ProjectContainerSaveTest$" --output-on-failure` | 8 | test-3-reverted-red.log |
| 4b: re-apply fix | `git apply logs/fix.patch` | 0 | - |
| 4b: rebuild fixed | `cmake --build build --target zene ProjectContainerSaveTest -j4` | 0 | build-5-reapplied.log |
| 4b: run fixed (must PASS) | `cd build/tests && ctest -V -R "^ProjectContainerSaveTest$" --output-on-failure` | 0 | test-4-green-final.log |
| Family build | `cmake --build build --target ProjectContainerTest ProjectContainerEntriesTest UndoHistoryPersistenceTest SessionCheckpointTest UndoBoundsTest -j4` | 0 | build-6-family.log |
| Family run | `cd build/tests && ctest -V -R "^(ProjectContainerTest\|ProjectContainerEntriesTest\|ProjectContainerSaveTest\|UndoHistoryPersistenceTest\|SessionCheckpointTest\|UndoBoundsTest)$"` | 8 | test-5-family.log |

### RED (without the fix)

```
FAIL!  : ProjectContainerSaveTest::undoingAV2ProjectKeepsItsContainerFormat() 'song->isContainerV2Project()' returned FALSE. (undo must keep the v2 container format)
   Loc: [../../tests/src/core/ProjectContainerSaveTest.cpp(550)]
Totals: 17 passed, 1 failed, 0 skipped, 0 blacklisted
```

The two guard tests pass without the fix, as they should.

### GREEN (with the fix)

```
PASS   : ProjectContainerSaveTest::undoingAV2ProjectKeepsItsContainerFormat()
PASS   : ProjectContainerSaveTest::undoingALegacyProjectKeepsItsLegacyFormat()
PASS   : ProjectContainerSaveTest::aRefusedUndoRestoreKeepsTheContainerFormat()
Totals: 18 passed, 0 failed, 0 skipped, 0 blacklisted
```

Executed count for the `^ProjectContainerSaveTest$` family: 18 test functions, all run.

### Related family (with the fix)

`ProjectContainerTest`, `ProjectContainerEntriesTest`, `ProjectContainerSaveTest`, `UndoBoundsTest` and
`SessionCheckpointTest` pass. `UndoHistoryPersistenceTest` fails at its fixture (see Self-check, item 4).
The other undo/checkpoint binaries (`ControlUndoStructuralTranscript`, `RevisionTimelineTest`,
`SessionCheckpointBudgetTest`, `ControlContainerV2`, ...) are registered but not built in this worktree and were not run.

## Gates

`python3 .../feature_check.py <worktree>` could not complete in this session: it writes its output to
`<worktree>/../<name>-feature-check/` (the projects folder, outside the write-jail) and its ratchet baseline
needs a `git worktree add` in the shared main repo. I ran every gate command it calls, from the worktree, with logs in
the LOG FOLDER:

| Gate | Command | EXIT | Note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | |
| check-namespace | `tests/scripted/check-namespace` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| complexity | `bash tests/complexity-gate.sh --check` | 1 | red on the train; no REGRESSION line names `SongUndoSnapshot.cpp` or `ProjectContainerSaveTest.cpp` |
| file-length | `bash tests/file-length-gate.sh --check` | 1 | red on the train; no REGRESSION line names either changed file |

`feature_check.py` itself: EXIT=1 with `PermissionError` creating its sibling log folder (`gates-1-feature-check.log`).
So the feature_check table is not produced here. By its own rule (fail only on items NEW in changed files) the two
ratchet rows would pass; the six simple rows pass.

## Self-check

1. **RED/GREEN proof.** Yes. The regression fails without the fix (`ProjectContainerSaveTest.cpp:550`, EXIT 8, 17 passed / 1 failed)
   and passes with it (EXIT 0, 18 passed), via the 4b revert and re-apply recorded above. The patch was the product diff only
   (`src/core/SongUndoSnapshot.cpp`).
2. **Deterministic.** No timers, sleeps or threads. The snapshot is captured in-process with `captureUndoSnapshot()`; the
   tempo change is a fixed value (90); the refused case uses a fixed byte string (`<<not a project`) compressed with `qCompress`;
   files live in a `QTemporaryDir`.
3. **At the cause, complete.** The cause is the single container-flag assignment in the load path (`Song.cpp:2053`, `:1688`),
   which the restore did not hold. The fix holds it in the one function all undo, redo and checkpoint restores share. The
   `project.open` and `main.cpp` load paths keep their own file-derived format and are deliberately untouched (see Fix).
4. **No collateral damage.** The two guard tests cover a legacy session and a refused restore. Opening v2 and legacy files
   is unchanged (the `loadProject` body is not edited). No limit or check was added. The one failing test in the related family,
   `UndoHistoryPersistenceTest`, fails at `UndoHistoryPersistenceTest.cpp:210` ("the baseline needs a real instrument
   (tripleoscillator)"): its fixture `buildBaseline()` (`tests/src/core/UndoHistoryTestSupport.h:168`) loads the
   `tripleoscillator` instrument, the plugin scan in the log reports 0 native plugins, and the fixture never calls a restore. My change
   runs only inside `restoreUndoSnapshot`, so it cannot have caused that line to fail. This is an environment gap (native
   plugin not found in this worktree build). I did not run that test on the unfixed base in this session, so that claim is by
   reading the code and the log, not by a before/after run.
5. **Not ALREADY-FIXED or FALSE-POSITIVE.** Verdict is FIXED; no "already fixed" claim is made.

## Gate status (not a claim of pass)

- `feature_check.py` could not run in this write-jail (its output folder is outside it); the six simple gates pass and the two ratchet
  gates have no regression item in the changed files, but the feature_check table itself was not produced.
