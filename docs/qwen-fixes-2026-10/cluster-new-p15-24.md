# Cluster new-p15-24

Verdict: **FIXED**

Finding: `project.restore_revision` restores a retained revision over the live project file, and it always records the
undo as `restoreProjectRevision(target, 0)`. When the live target did not exist, the restore creates the file, but
nothing was rotated into revision 0, so that undo cannot remove the created file and can write unrelated bytes into the
path. Reproduced on the unfixed tree (RED, below): undo left the file behind with the bytes of a stale revision 0, or,
with no revision 0, undo failed.

## Evidence (file:line on this branch, before the fix)

- `src/core/ProjectRevisions.cpp:364-368`: `rotateProjectRevision` runs only `if (QFileInfo::exists(projectPath))`, and
  `:243-247` returns 0 with nothing kept when the file is missing. So a restore into a missing path leaves revision 0
  as it was and does not hold the created file.
- `src/core/ControlCommandsProjectFiles.cpp` (unfixed, `git show HEAD:` line 372): the undo was
  `restoreProjectRevision(target, 0, &reason)` for every restore.
- The sibling `revisions.restore` already has the correct shape: `src/core/ControlCommandsRevisions.cpp:147-168`
  (`undoRestoreStep`: when the live file did not exist, the undo removes the created file) and `:398` (`liveExisted`
  read before the restore). So the defect is confined to `project.restore_revision`; the sibling is ALREADY-FIXED.

## Fix (product code: `src/core/ControlCommandsProjectFiles.cpp` only)

- `:73-84` `removeRestoreCreatedFile`: removes the created file; reports a failed removal through
  `control::recordActionRestoreFailure`, as the sibling does.
- `:373` `const bool liveExisted = QFileInfo::exists(target);`, read BEFORE the restore replaces the file.
- `:391-394` undo: when `!liveExisted` remove the created file; otherwise the unchanged restore of revision 0.
- `:438-447` the advertised `inverse` is `control.undo` (the recorded step, which removes the file) when the restore
  created the file; the existing-file case still advertises `project.restore_revision` revision 0.
- Redo is unchanged. Its target is found by content (`revisionHoldingBytes`). A missing-target restore rotates
  nothing, so revision N is still in the set after the undo removed the file.

Choices made here:
- The removal helper is a copy of the sibling's file-local removal branch, not a shared function. Sharing it would need
  a header change (`ControlReversibility.h` or `ProjectRevisions.h`) and a wider rebuild. This file was at 472 code
  lines of the 500 limit before the change, so the copy is kept small.
- The first draft of the fix took the file to 502 code lines (over the 500 limit, a new item). The transaction branch was
  rewritten as two ternaries and the undo branch as one line. The file is now 489 code lines (base 472).
- No command was added, and the command's description and schemas are unchanged (the `ControlCommandsSnapshot` canary
  is not touched).

Known limit, not changed: `QFileInfo::exists` follows symlinks, so a dangling symlink at the target counts as missing.
An undo would then remove the restored file, and the symlink is not put back. `revisions.restore` behaves the same way.

## Regression test

`tests/src/core/ProjectRestoreMissingTargetTest.cpp` (new). Driven through the registry with a real Engine:
1. `undoOfRestoreIntoMissingPathRemovesTheCreatedFile`: the finding's scenario (a deleted project with its `.rev*` set).
   Undo removes the file; redo restores revision 1.
2. `undoOfRestoreIntoMissingPathWithoutRevisionZeroRemovesTheFile`: no revision 0 at all.
3. `restoreIntoMissingPathAdvertisesUndoAsItsInverse`: the journal's advertised inverse is `control.undo`.
4. `undoOfRestoreOverExistingFileRestoresThePreviousBytes`: guard for the existing-file case (passes before and after).

Registration (same commit): `tests/CMakeLists.txt` (test list and the offscreen environment property),
`tests/fork-sources.txt` and `tests/all-sources.txt` (sorted with `LC_ALL=C`, as the recipe sorts), and
`tests/upstream-modifications.txt` (the `tests/CMakeLists.txt` entry).

## Commands and exit codes

Working directory is the worktree unless noted. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-24.r1/logs`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build target (stale build tree) | `cmake --build build --target ProjectRestoreMissingTargetTest -j4` | 2 (no rule: the tree predated the new file) | build-1.log |
| Re-configure | `cmake build` | 97 (bunny-guard: configuring is the parent's job) | configure-1.log |
| Regenerate build system | `cmake --build build --target cmake_check_build_system` | 0 | regen-1.log |
| Build with compiler cache | `cmake --build build --target ProjectRestoreMissingTargetTest -j4` | 2 (ccache temp file "Permission denied") | build-2.log |
| Build without compiler cache | `CCACHE_DISABLE=1 cmake --build build --target ProjectRestoreMissingTargetTest -j4` | 0 | build-3.log |
| **RED** (unfixed code) | `cd build/tests && ctest -R ProjectRestoreMissingTargetTest --output-on-failure` | **8** (3 FAIL: cases 1, 2, 3) | test-red-1.log |
| GREEN, first run | same ctest, after the fix | 8 (case 3 read the wrong place: `__transaction` is consumed by the registry; test corrected to `revtest::stateOf`) | test-green-1.log |
| GREEN | same ctest | **0** | test-green-2.log |
| 4b: save product diff | `git diff -- src/core/ControlCommandsProjectFiles.cpp > …/logs/fix-final.patch` | 0 | fix-final.patch |
| 4b: revert | `git apply -R …/logs/fix-final.patch` | 0 | |
| 4b: rebuild unfixed | `CCACHE_DISABLE=1 cmake --build build --target zene ProjectRestoreMissingTargetTest -j4` | 0 | build-12-final-unfixed.log |
| 4b: **RED** | `cd build/tests && ctest -R ProjectRestoreMissingTargetTest --output-on-failure` | **8** (same 3 FAIL) | test-final-red.log |
| 4b: re-apply | `git apply …/logs/fix-final.patch` | 0 | |
| 4b: rebuild fixed | `CCACHE_DISABLE=1 cmake --build build --target zene ProjectRestoreMissingTargetTest -j4` | 0 | build-11-final-fix.log |
| 4b: **GREEN** | `cd build/tests && ctest -R ProjectRestoreMissingTargetTest --output-on-failure` | **0** | test-final-green.log |
| Case count | `./ProjectRestoreMissingTargetTest` (in `build/tests`) | Totals: 6 passed, 0 failed (init, 4 cases, cleanup) | |

Affected family (`ctest -R "ProjectRestoreMissingTargetTest|ProjectRevisionRestoreStagingTest|RevisionTimelineTest|ReversibilityUndoTest|ReversibilityContractTest|ReversibilityUndoDeviceTest|ProjectRecoveryTest|ControlRegistryTest" -j4`, with the `build-13-family-final.log` build first):
8 tests executed. 7 pass. `ReversibilityUndoDeviceTest` fails (EXIT 8, test-family-final.log). It fails the same way on the
UNFIXED tree (`git apply -R`, rebuild, `ctest -R ReversibilityUndoDeviceTest`: EXIT 8, `Totals: 4 passed, 2 failed,
2 skipped`, test-device-unfixed.log). The messages say "the LADSPA host plugin 'ladspaeffect' is not in this build" and "this build
exposes no loadable built-in instrument" (`src/core/ControlDeviceHosted.cpp:87`). The test has no reference to the restore code
(0 matches). So this is a build-environment failure in this tree, not caused by the change.

Gates. `python3 …/qwen/feature_check.py <worktree>` exited 1 with `PermissionError` at `feature_check.py:71`
(`logs.mkdir`): it writes its logs into `<worktree>-feature-check`, outside the write-jail. I did not edit the shared script.
I ran the same commands it runs, from the worktree:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 (after the sorted-position fix below) |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 (prints REPRODUCES) |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (29 REGRESSION items in the whole tree; none in a file this branch changes) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (same 29 items as above, which include no ControlCommandsProjectFiles.cpp item) |

The ratchet items are listed in `gate4-complexity.log` and `gate4-file-length.log`. I checked them against the changed
file list (`ControlCommandsProjectFiles.cpp`, `tests/CMakeLists.txt`, the three manifests, the new test): 0 matches. Under
the feature_check rule (only items in changed files count), both ratchet rows pass. `ControlCommandsProjectFiles.cpp`
was on the new-over-500 list at 502 code lines; it is now 489 and off the list.

The manifest gates first failed because the recipe derives from `git ls-files` and the new file was untracked, and then
because the entry was out of the `LC_ALL=C` sort order. The new file is staged (`git add`, explicit path) and the entry moved to its sorted
position; both manifest gates then reproduced.

## Self-check

(Filled after `submit_check.py`; see the section below.)
