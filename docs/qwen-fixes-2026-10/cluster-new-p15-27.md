FIXED

# Cluster new-p15-27 - undo can delete a newer file when the restore created the target (BUG 9)

## Decision: REAL, fixed here

`src/core/ControlCommandsRevisions.cpp` (line numbers on this branch after the fix):

- Before the fix, `undoRestoreStep` with `liveExisted == false` ran `QFile::remove(target)` unconditionally (`git show HEAD:src/core/ControlCommandsRevisions.cpp:160`; the finding cites line 159, the closing brace of the `liveExisted` branch just above it). The sha256 of what the restore wrote was already captured at the call site (`restoredSha`, `sha256OfFile(target)` after `restoreTimelineRevision`) and passed to `recordRestoreUndoStep`, but the undo never read it. So whatever was at the path when undo ran was deleted, including a different project written there after the restore.
- A restore created the file (no live file before), a different project was written to the same path, then `control.undo`: the newer project was removed. Reproduced by the regression test `undoLeavesAProjectWrittenAtThePathAfterTheRestore` (RED, below).

## The fix (cause, not symptom)

- `holdsRestoredBytes` (`src/core/ControlCommandsRevisions.cpp:148`): true only for a regular file, not a symlink (QFileInfo would follow a link), whose sha256 equals the restored sha. An empty restored sha never matches.
- `undoCreatedFileStep` (`:158`): nothing at the path - nothing to do, succeeds as before. A path that does not hold the restored bytes - the file was changed, a directory, a symlink, dangling or live - is left in place and reported with `recordActionRestoreFailure`. The reason names `revisions.restore`, so control.undo answers a typed `Refused` through the existing channel (`ControlCommandsControl.cpp` `actionRestoreFailureOf`). Only a file that still holds the restored bytes is removed, and the existing "could NOT be removed" report is kept.
- `undoRestoreStep` (`:184`) takes the restored sha and delegates the created-file branch. The branch was split out so the function stays under the complexity target (the first draft measured CCN 12, a new item; it is now CCN within target: the gate lists no item for this function).
- The `recovered_by` result text and the file-header/transaction comments now say the file is removed while it still holds the restored bytes. No command description or schema changed, so `ControlCommandsSnapshot` does not move, and no command was added.

Sibling check: `QFile::remove` calls elsewhere in the revision area are staging/rotation cleanup (`ProjectRevisions.cpp:101,128,178,397,400`, `RevisionTimeline.cpp:426-456`). `project.restore_revision` (`ControlCommandsProjectFiles.cpp:305-400`, its undo at `:372`) undoes only through `restoreProjectRevision(target, 0)` and never removes a created file. The only created-file removal in the group was the one fixed.

## Tests

Two test changes, both in the regression of this bug:

1. New file `tests/src/core/RevisionRestoreCreatedFileUndoTest.cpp` (registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`). Three cases, all through the registry (`revisions.restore`, `control.undo`) against real files:
   - `undoRemovesARestoreCreatedFileThatStillHoldsTheRestoredBytes` - normal path, unchanged: removed, `ok` (passes before and after).
   - `undoLeavesAProjectWrittenAtThePathAfterTheRestore` - BUG 9: a different project is written at the path; undo is `!ok`, `Refused`, names `revisions.restore`, the newer bytes are kept (**RED before the fix**).
   - `undoOfARestoreCreatedFileWithNothingLeftStillSucceeds` - the path was emptied after the restore; undo still succeeds (passes before and after; behaviour preserved).
2. **One existing assertion changed - reviewer attention needed.** `RevisionTimelineTest::undoOfRestoreCreatedDanglingSymlinkRemovesPath` asserted that undo REMOVES a dangling symlink written at the created path after the restore. That is exactly the bug (a newer entry at the path is deleted), so the test now asserts the fixed behaviour and is renamed `undoOfRestoreCreatedDanglingSymlinkLeavesTheNewerEntry`: undo is `!ok`, `Refused`, names `revisions.restore`, and the symlink stays. It was not weakened: the same setup now checks the opposite outcome, and it fails on the unfixed code (RED, below). The directory case `undoOfRestoreCreatedFileThatCannotBeRemovedFailsTyped` is unchanged and still passes.

Not tested, and why: an empty `restoredSha` (the sha read failed right after the restore) now refuses removal instead of removing. Reaching it needs an unreadable file the restore just wrote, which the harness cannot produce deterministically; it is a guard, not a tested path.

## Evidence (exact commands, EXIT codes; logs in `LOGS = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-27.r1/logs`)

Environment note: ccache's temp directory is outside the write jail, so every build was run with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp` (a plain environment variable, no config change). The first build attempt that hit the permission error is `build-1-red-target.log`/`build-1-red.log`.

| Step | Command (from the worktree or `build/tests`) | EXIT | Log |
|---|---|---|---|
| RED build, fix reverted | `git apply -R fix.patch` then `cmake --build build --target RevisionRestoreCreatedFileUndoTest RevisionTimelineTest -j2` | 0, 0 | `build-11-without-fix.log` |
| RED ctest | `ctest -R "RevisionTimelineTest\|RevisionRestoreCreatedFileUndoTest" --output-on-failure` | **8** | `test-10-without-fix.log` |
| RED binary, new file | `./RevisionRestoreCreatedFileUndoTest` | **1** - `FAIL! undoLeavesAProjectWrittenAtThePathAfterTheRestore() '!undone.ok' returned FALSE`; Totals 4 passed, 1 failed | `qtest-10-new-without-fix.log` |
| RED binary, existing file | `./RevisionTimelineTest` | **1** - `FAIL! undoOfRestoreCreatedDanglingSymlinkLeavesTheNewerEntry() '!undo.ok' returned FALSE`; Totals 31 passed, 1 failed | `qtest-11-rtt-without-fix.log` |
| GREEN apply | `git apply fix.patch`, then `cmake --build build --target RevisionRestoreCreatedFileUndoTest RevisionTimelineTest -j2` | 0, 0 | `build-12-with-fix-final.log` |
| GREEN ctest | `ctest -R "RevisionTimelineTest\|RevisionRestoreCreatedFileUndoTest" --output-on-failure` | **0** - 2/2 passed | `test-11-with-fix-final.log` |
| GREEN binary, new file | `./RevisionRestoreCreatedFileUndoTest` | **0** - Totals 5 passed (3 cases + init/cleanup) | `qtest-12-new-with-fix.log` |
| GREEN binary, existing file | `./RevisionTimelineTest` | **0** - Totals 32 passed | `qtest-13-rtt-with-fix.log` |
| zene target | `cmake --build build --target zene -j2` | 0 | `build-14-zene-final.log` |

Earlier history: the first RED/GREEN round (before the refactor, with the cases inside `RevisionTimelineTest`) gave the same result: RED `test-1-red.log` (ctest 8; 33 passed, 2 failed, at `RevisionTimelineTest.cpp:1039` and `:1083`), GREEN `test-2-green.log` (ctest 0; `qtest-2-green.log` 35 passed). The cases were then moved to their own file, and the table above is the final evidence.

Undo family, final code: `ctest -R "RevisionTimelineTest|RevisionRestoreCreatedFileUndoTest|ProjectRevisionRestoreStagingTest|ReversibilityUndoTest|ReversibilityUndoDeviceTest" --output-on-failure` - EXIT **8** (`test-12-family-final.log`). Per target:

- `RevisionTimelineTest` 32 passed, 0 failed (EXIT 0)
- `RevisionRestoreCreatedFileUndoTest` 5 passed (EXIT 0; 3 cases plus init/cleanup)
- `ProjectRevisionRestoreStagingTest` 5 passed (EXIT 0)
- `ReversibilityUndoTest` 10 passed, 5 skipped (EXIT 0)
- `ReversibilityUndoDeviceTest` 4 passed, **2 failed**, 2 skipped (EXIT 2 binary, 8 ctest). **Pre-existing, not from this change:** both failures are `'loaded.ok' returned FALSE (the LADSPA host plugin 'ladspaeffect' is not in this build)`. The test never references the revision code (`rg` found no `revisions.`/restore references in it). With the fix reverted, the same target fails the same two ways (`test-6-device-without-fix.log`, ctest 8).

### Gates (final code)

`feature_check.py` could not run: it writes its logs to `<worktree>/../zene-qwen-new-p15-27-feature-check`, outside the write jail (`PermissionError`, `feature-check-1.log`). Its rows were run directly, and the ratchet rule was applied by hand (`ratchet_compare.py`):

| Gate | Command | EXIT | Log |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate3-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate3-all-sources-reproduce.log` (REPRODUCES) |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate2-no-upstream-regression.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate2-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate2-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate2-doc-figures.log` |
| complexity | `bash tests/complexity-gate.sh --check` | 1 (red on the train) | `gate2-complexity.log` |
| file-length | `bash tests/file-length-gate.sh --check` | 1 (red on the train) | `gate2-file-length.log` |

For the two ratchet gates, `ratchet_compare.py` (`python3 -I ratchet_compare.py <worktree> 639e1d23e... LOGS`, EXIT 0) applies the feature_check rule: a REGRESSION item fails only if its file is in the changed set and its key is absent from the cached base listing (`gate-baselines/639e1d23e....json`). Result: complexity 7 regressions, **0 new in changed files**; file-length 22 regressions, **0 new in changed files**. The one changed-file item is `new file over 500 lines: tests/src/core/RevisionTimelineTest.cpp`, which the base listing already has. It grew from 816 to 819 code lines, all from the flipped assertion (+3); the new cases were moved to their own file so it does not grow further. `undoRestoreStep` is no longer listed (it was CCN 12 before the split).

Source-list fix: `all-sources.txt` is derived by `git ls-files`, so the new file had to be staged (`git add` of its explicit path) before the recipe could reproduce the manifest. The hand-inserted entry was already in sorted position and matched the recipe after staging; the list was not regenerated by hand.

## Residual risk / not verified

- A refused undo: whether the step then sits on the redo stack, and what `control.redo` does with it, is not tested. If a caller redoes, the restore replays and rotates the newer file into the keep-3 set (recoverable, not deleted); I did not verify that path.
- Windows: the dangling-symlink variant uses a `.lnk` name on Windows in the existing test; not run on Windows here.
- Identical content written after the restore is indistinguishable from the restored file and is removed; this is the contract the finding states (remove only while it holds the restored bytes).

## Self-check

(To be filled from `submit_check.py`, see the next section update.)
