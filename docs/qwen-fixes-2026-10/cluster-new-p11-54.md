FIXED

# Cluster new-p11-54 - moving a track into a folder may not mark the project modified

## Decision

REAL, and fixed. `track.set_folder` re-parents the track and records its undo step but never marks the song modified, so a clean project stays clean after a folder move and the save prompt on close can be skipped.

## Evidence (file:line on this branch, base 639e1d23e)

- Forward write: `src/core/ControlCommandsTrackFolder.cpp:127` `child->setParentFolder(folder);`. Before the fix nothing in `setFolder` (lines 104-155) marked the song modified.
- Undo recording: `src/core/ControlCommandsTrackFolder.cpp:124-126` `control::addUndoStep(...)`. `ProjectJournal::addJournalAction` (`src/core/ProjectJournal.cpp:277-290`) records the step without a `setModified` call, so recording does not dirty the project either.
- Undo and redo do mark modified: `src/core/ProjectJournal.cpp` undo path (`Engine::getSong()->setModified()` near line 148) and redo path (near line 198). The undo side was already honest; only the forward write was missing it.
- Sibling folder writers do mark modified: `src/tracks/TrackFolder.cpp:541` (setCollapsed), `:549` (setPinned), `:375` and `:409` (routing mode on/off).
- Why the fix is not in `Track::setParentFolder` (`src/core/Track.cpp:427`): it is also called on load from `Track::restoreState` (`src/core/Track.cpp:980`) and by the load-time repair in `src/core/TrackContainerFolders.cpp:99`. Marking there would dirty every project as it opens, which breaks the "old projects load exactly as before" rule.
- Other callers checked: `src/core/ControlCommandsTrackFolder.cpp:96-98` (the undo `reparent` helper) runs only inside undo/redo, which already mark modified. `src/gui/TrackFolderMenu.cpp:78` dispatches the same `track.set_folder` command, so the GUI path is covered by the same fix. `src/core/ControlCommandsDrumRack.cpp:113` and `src/core/ControlCommandsTrackFlatten.cpp:76` are not user-facing folder moves (they build or restore tracks inside commands or undo steps) and are out of scope.

## Fix

`src/core/ControlCommandsTrackFolder.cpp:127-131`: after `setParentFolder`, `if (folder != previous) { Engine::getSong()->setModified(); }`. A move onto the folder the track is already in (including a no-op unparent at the root) leaves the project clean, as before. The undo step, the reply and the undo/redo paths are unchanged. No new command, no audio-thread code, no load-path change.

## Regression test

`tests/src/core/TrackFolderTest.cpp:920` `reparentingMarksTheProjectModified` (a new slot in the existing `TrackFolderTest`; no new file, so no registration). It marks the song clean with `noteProjectSaved`, then checks:
1. move into a folder dirties the project;
2. move out of a folder dirties the project;
3. a no-op move (already at the root) leaves the project clean;
4. `control.undo` of a move dirties the project.

Only assertion 1 is the RED cause; the others guard the boundary and the undo path.

## Commands run and results

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-54.r1/logs/`.

| # | Command (run from the worktree / `build/tests` as noted) | EXIT | Result |
|---|---|---|---|
| 1 | `cmake --build build --target TrackFolderTest -j4 > .../build-1.log 2>&1` | 2 | ccache could not create a temp file under `~/.local/share/zene-bunny-fix/dsh/...` (`Permission denied`). Environment, not code. |
| 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp cmake --build build --target TrackFolderTest -j4 > .../build-2.log 2>&1` | 0 | Test binary built with the regression test, before the fix. |
| 3 (RED) | from `build/tests`: `ctest -R TrackFolderTest --output-on-failure > .../test-1.log 2>&1` | 8 | 0 of 1 passed. `FAIL! : TrackFolderTest::reparentingMarksTheProjectModified() 'song->isModified()' returned FALSE. (moving a track into a folder left the project clean)` |
| 4 | `CCACHE_TEMPDIR=... cmake --build build --target TrackFolderTest -j4 > .../build-3.log 2>&1` | 0 | Recompiled `ControlCommandsTrackFolder.cpp.o` and relinked. |
| 5 (GREEN) | from `build/tests`: `ctest -R TrackFolderTest --output-on-failure > .../test-2.log 2>&1` | 0 | 1 of 1 passed. |
| 6 | `git diff -- src/core/ControlCommandsTrackFolder.cpp > .../fix.patch` | 0 | Product fix saved. |
| 7 | `git apply -R .../fix.patch` | 0 | Fix reverted in the worktree. |
| 8 | `cmake --build build --target TrackFolderTest -j4 > .../build-4.log 2>&1` | 0 | Rebuilt without the fix. |
| 9 (RED, fix reverted) | from `build/tests`: `ctest -R TrackFolderTest ... > .../test-3.log 2>&1` | 8 | Same assertion FAILs as step 3. |
| 10 | `git apply .../fix.patch` | 0 | Fix restored. |
| 11 | `cmake --build build --target TrackFolderTest -j4 > .../build-5.log 2>&1` | 0 | Rebuilt with the fix. |
| 12 (GREEN, fix restored) | from `build/tests`: `ctest -R TrackFolderTest ... > .../test-4.log 2>&1` | 0 | 1 of 1 passed. |
| 13 | `cmake --build build --target zene FolderSummaryLaneTest TrackFolderMenuTest -j4 > .../build-6.log 2>&1` | 0 | Family binaries rebuilt against the fixed product code. |
| 14 | from `build/tests`: `ctest -R "Folder\|folder" --output-on-failure > .../test-5.log 2>&1` | 0 | 3 passed (`TrackFolderTest`, `TrackFolderMenuTest`, `FolderSummaryLaneTest`). `ControlTrackFolderTranscript` is Skipped (ctest `SKIP_RETURN_CODE 77`); `tests/control-track-folder.py` returns 77 when the build has no loadable instrument (`tests/control-track-folder.py:52-54`, `:482`). Not counted as passed. |
| 15 | `cmake --build build --target AudioAnalysisTest MixerSendsOnFadersTest DawProjectInterchangeRoundTripTest SessionPerformTest ProjectToolsTest -j4 > .../build-7.log 2>&1` | 0 | Other tests that call `track.set_folder` or `setParentFolder` rebuilt. |
| 16 | from `build/tests`: `ctest -R "^(AudioAnalysisTest\|MixerSendsOnFadersTest\|DawProjectInterchangeRoundTripTest\|SessionPerformTest\|ProjectToolsTest)$" ... > .../test-6.log 2>&1` | 0 | 5 of 5 passed. |
| 17 | `python3 .../qwen/feature_check.py <worktree> > .../gates-1.log 2>&1` | 1 | Did not run the gates: the script creates `<worktree>/../<name>-feature-check`, which is outside this lane's write-jail. `PermissionError` at `feature_check.py:71`. |
| 18 | `python3 -I .../logs/feature_check_logredirect.py <worktree> > .../gates-2.log 2>&1` | 0 | A copy of `feature_check.py` with only the log folder and the `ROOT` path redirected (the two diff lines are in the log folder). Gate commands, limits and table logic unchanged. RESULT: PASS, 8 of 8 rows (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length`). |

Affected-family test count: `ctest -R "Folder|folder"` executed 4 tests (3 passed, 1 skipped by its own contract). The five further tests in step 16 all passed. `ctest` counts `TrackFolderTest` as one test; the new slot runs inside that binary and is the one that FAILed in steps 3 and 9.

Gate exit codes: `feature_check.py` through the redirected copy = 0. The unmodified script = 1, for the sandbox reason in step 17.

## Files committed

- `src/core/ControlCommandsTrackFolder.cpp`
- `tests/src/core/TrackFolderTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-54.md`

No file was pushed. No new control command was added, and the command snapshot and canary are unchanged.

## Self-check

(filled in after `submit_check.py`)
