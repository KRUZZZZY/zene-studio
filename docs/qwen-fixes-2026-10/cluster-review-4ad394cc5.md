# Cluster review-4ad394cc5: an unreadable library file was overwritten and undone to empty bytes

**Verdict: ALREADY-FIXED**

The finding is real for the pre-fix code: `readBackIfAny` set `existed = true` while leaving
`bytes` empty when the old file existed but could not be opened/read, so `writeClip` replaced it
and the undo step wrote empty bytes back.

On this branch the fix is already present (commit `ea81eae46`, "fix(rework 4ad394cc5): a library
save overwrote a file it could not read, and undo then wrote empty bytes back", an ancestor of
HEAD `de5a85a96`):

- `src/core/ControlCommandsLibraryClips.cpp:252` adds `PreviousFile::readable`.
- `src/core/ControlCommandsLibraryClips.cpp:263-269` marks `readable = false` on open failure and on a read error.
- `src/core/ControlCommandsLibraryClips.cpp:288-297` (`writeKeepingPrevious`) refuses **before any write** when
  the file exists but cannot be read.
- `src/core/ControlCommandsLibraryClips.cpp:313` returns the refusal from `saveClip` before `addUndoStep`
  (line 316) runs, so a refusal registers no undo action and touches no file.

The regression test is `LibraryBrowserTest::aSaveNeverReplacesAFileItCannotRead`
(`tests/src/core/LibraryBrowserTest.cpp:119`): it creates an existing file, makes it write-only,
skips under an account that reads regardless of permissions (e.g. root), then expects a `Refused`
result and the original bytes intact. It is a genuine RED/GREEN test — proven below by
reverse-applying the product hunk of the fix commit.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| GREEN on branch HEAD | `ctest -R LibraryBrowserTest --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` from `build/tests`; **EXIT=0**, `100% tests passed out of 1`. |
| Reverse the product fix | `git show ea81eae46 -- src/core/ControlCommandsLibraryClips.cpp > .../logs/fix.patch` then `git apply -R .../logs/fix.patch; echo EXIT=$?`; **EXIT=0**. |
| RED without the fix | rebuild LibraryBrowserTest **EXIT=0**; `ctest -R LibraryBrowserTest --output-on-failure > .../logs/test-2-red.log 2>&1; echo EXIT=$?`; **EXIT=8**, `FAIL! LibraryBrowserTest::aSaveNeverReplacesAFileItCannotRead()`, actual `refused.errorKind = 0`, expected `ControlErrorKind::Refused = 5` (`LibraryBrowserTest.cpp(148)`). |
| Restore the fix | `git apply .../logs/fix.patch; echo EXIT=$?`; **EXIT=0**; rebuild **EXIT=0**. |
| GREEN with the fix | `ctest -R LibraryBrowserTest --output-on-failure > .../logs/test-3-green.log 2>&1; echo EXIT=$?`; **EXIT=0**, `100% tests passed out of 1`. |
| Affected family | `LibraryBrowserTest`: 1 CTest target executed, 21 QTest slots, 1/1 CTest target passed on both green runs; without the fix the same target failed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1; echo EXIT=$?`; **EXIT=1** (pre-existing; see note). |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-filelength.log 2>&1; echo EXIT=$?`; **EXIT=1** (pre-existing; see note). |

### Gate note

Both ratchets already fail on this branch at HEAD with a **zero product diff** (the worktree has
no source changes for this cluster). The failures name functions/files unrelated to this finding
(e.g. `tests/src/gui/XyPadPairTest.cpp` CCN 16, `src/core/TakeLane.cpp` 520 lines), so they are
inherited, not introduced. They are not re-anchored or otherwise touched here.

## Build and test commands

All builds ran from the worktree root; all ctest runs ran from `build/tests`.

```text
cmake --build build --target LibraryBrowserTest > .../review-4ad394cc5.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0 (HEAD, already built)
ctest -R LibraryBrowserTest --output-on-failure > .../logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc                            # EXIT=0, GREEN
git show ea81eae46 -- src/core/ControlCommandsLibraryClips.cpp > .../logs/fix.patch                                          # patch generated
git apply -R .../logs/fix.patch; rc=$?; echo EXIT=$rc                                                                        # EXIT=0, product fix reversed
cmake --build build --target LibraryBrowserTest > .../logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc                            # EXIT=0
ctest -R LibraryBrowserTest --output-on-failure > .../logs/test-2-red.log 2>&1; rc=$?; echo EXIT=$rc                          # EXIT=8, RED
git apply .../logs/fix.patch; rc=$?; echo EXIT=$rc                                                                           # EXIT=0, product fix restored
cmake --build build --target LibraryBrowserTest > .../logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc                            # EXIT=0
ctest -R LibraryBrowserTest --output-on-failure > .../logs/test-3-green.log 2>&1; rc=$?; echo EXIT=$rc                        # EXIT=0, GREEN
bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1; rc=$?; echo EXIT=$rc                             # EXIT=1 (pre-existing)
bash tests/file-length-gate.sh --check > .../logs/gate-filelength.log 2>&1; rc=$?; echo EXIT=$rc                            # EXIT=1 (pre-existing)
```

Full log directory: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-4ad394cc5.r1/logs`.

Files committed:

- `docs/qwen-fixes-2026-10/cluster-review-4ad394cc5.md`

No product or test source was changed: the fix and its regression test were already on the branch.
