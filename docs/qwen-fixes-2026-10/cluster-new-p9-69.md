# Cluster new-p9-69

Verdict: **FIXED**

## Evidence

- `src/core/ControlCommandsVideo.cpp:210-224` retains each extraction's `aboutToQuit` connection through the worker and disconnects it in the queued completion before landing the result. Successful, failed, and cancelled decodes use this completion path.
- `tests/src/core/VideoTrackTest.cpp:358-379` drives `video.extract_audio` with `background: true`, waits for the task to finish, and checks that the application's `aboutToQuit` receiver count returns to its baseline.
- Against the original implementation the regression failed with `receiversAfter = 2`, `receiversBefore = 1`. Reversing the production patch reproduced that failure; restoring it passed.
- The affected CTest family ran **1 test** (`VideoTrackTest`) on each invocation; zero tests ran on none of the invocations.

## Commands and results

Commands below ran from the worktree root unless the working directory is shown.

| Purpose | Exact command | Exit |
| --- | --- | ---: |
| Build regression target | `cmake --build build --target VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED on original code (cwd: `build/tests`) | `ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Build fixed `zene` and test target | `cmake --build build --target zene VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN (cwd: `build/tests`) | `ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Reverse only the production patch | `git diff -- src/core/ControlCommandsVideo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild with production patch reversed | `cmake --build build --target zene VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED with production patch reversed (cwd: `build/tests`) | `ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild with production patch restored | `cmake --build build --target zene VideoTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN after restoring patch (cwd: `build/tests`) | `ctest -R VideoTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-69.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

`fff grep` was not available in the tool list or shell, so build/test log searches used `rg` on the named log files.

## Files committed

- `src/core/ControlCommandsVideo.cpp`
- `tests/src/core/VideoTrackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-69.md`
