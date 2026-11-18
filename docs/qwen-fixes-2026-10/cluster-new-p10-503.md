# Cluster new-p10-503: negative burst length

**Verdict: FIXED**

`burstAudio` in `tests/src/core/AudioBurstSupport.h:47-51` converted a negative frame count to `std::size_t` before validating `ticks`. It now throws `std::invalid_argument` at line 49 for every nonpositive tick count, before conversion or allocation. The regression is `tests/src/core/WarpQuantizeTest.cpp:131-134`.

The new test failed before the fix because the original helper attempted to create a vector larger than `max_size()`. Removing the production patch after the fix reproduced the same failure; restoring it passed.

## Evidence

The affected CTest family is `WarpQuantizeTest`: one test was selected and run per invocation. The fixed run passed 1/1. The test suite includes the new regression alongside its existing cases.

All commands below were run in the worktree unless another working directory is named. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs`.

The requested `fff` grep tool was not exposed in this session, and `command -v fff` found no local executable. I searched the logs with `rg` for `error`, `FAILED`, and `Assertion` instead.

| Check | Exact command | Exit |
| --- | --- | ---: |
| Build regression target before fix (`build-1.log`) | `cmake --build build --target WarpQuantizeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED before fix, from `build/tests` (`test-1.log`) | `ctest -R '^WarpQuantizeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Build fixed target (`build-2.log`) | `cmake --build build --target WarpQuantizeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN with fix, from `build/tests` (`test-2.log`) | `ctest -R '^WarpQuantizeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Save and reverse production patch (`fix.patch`) | `git diff -- tests/src/core/AudioBurstSupport.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild without fix (`build-3.log`) | `cmake --build build --target WarpQuantizeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED with production patch reversed, from `build/tests` (`test-3.log`) | `ctest -R '^WarpQuantizeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild with restored fix (`build-4.log`) | `cmake --build build --target WarpQuantizeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN with restored fix, from `build/tests` (`test-4.log`) | `ctest -R '^WarpQuantizeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild application (`build-5.log`) | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-503.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Files committed

- `tests/src/core/AudioBurstSupport.h`
- `tests/src/core/WarpQuantizeTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-503.md`
