# Cluster new-p9-136: export format row ownership

**Verdict: FIXED**

## Evidence

- `src/gui/modals/ExportProjectDialog.cpp:237-243`: `onFileFormatChanged()` now deletes both `QLayoutItem`s returned by `takeRow()` and leaves the widgets parented to the format settings group box. Dialog destruction therefore releases controls that are inactive for the selected format.
- `tests/src/gui/ExportQualityControlsTest.cpp:107-147`: the regression changes FLAC to WAV, then checks that the detached compression combo is destroyed when the dialog goes out of scope.
- The original implementation failed at `compressionLevel.isNull()` (false). Reverting only the production patch reproduced that same failure; restoring it passed.

The affected ctest family contains one registered test case, `ExportQualityControlsTest` (CTest #251). Each GREEN run executed 1/1 case successfully. The RED runs reported 3 passed and 1 failed QtTest checks, with the new regression as the failure.

## Commands and results

Build commands ran from the worktree root. CTest commands ran from `build/tests`. Every build, test and gate output was written to a new log in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs`.

| Stage | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Original build | `cmake --build build --target zene ExportQualityControlsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Build succeeded before the fix. |
| Initial RED | `ctest -R '^ExportQualityControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | Regression failed at `compressionLevel.isNull()`. |
| Fixed build | `cmake --build build --target zene ExportQualityControlsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Build succeeded with the fix. |
| Initial GREEN | `ctest -R '^ExportQualityControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | 1/1 ctest case passed. |
| Save and reverse production patch | `git diff -- src/gui/modals/ExportProjectDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/fix.patch; echo EXIT=$?` | 0 | Only the production change was reverted. |
| Reversed build | `cmake --build build --target zene ExportQualityControlsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Build succeeded without the fix. |
| Proof RED | `ctest -R '^ExportQualityControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | Same regression assertion failed. |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/fix.patch; echo EXIT=$?` | 0 | Fix restored. |
| Restored build | `cmake --build build --target zene ExportQualityControlsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Build succeeded with the fix restored. |
| Proof GREEN | `ctest -R '^ExportQualityControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | 1/1 ctest case passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-136.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 | Passed. |

The fff grep tool was not available in this session; I searched the logs with `rg` for the requested error, failure and assertion terms.

## Files committed

- `src/gui/modals/ExportProjectDialog.cpp`
- `tests/src/gui/ExportQualityControlsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-136.md`
