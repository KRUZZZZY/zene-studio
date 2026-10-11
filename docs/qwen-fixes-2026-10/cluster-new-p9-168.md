# Cluster new-p9-168

**Verdict: FIXED**

`HardwareInsert::setSettings()` left `m_ping` unchanged when the insert was disabled. A ping already in `Waiting` therefore resumed if the insert was enabled again. Disabling now stores `PingState::Idle` with release ordering.

Evidence:

- [src/core/HardwareInsert.cpp:80](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-168/src/core/HardwareInsert.cpp:80): `setSettings()` now resets the ping state whenever `settings.enabled` is false (line 91).
- [tests/src/core/HardwareIoTest.cpp:285](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-168/tests/src/core/HardwareIoTest.cpp:285): regression drives an insert into `Waiting`, disables it, checks `Idle`, then reenables and processes another period to ensure it stays idle.

## Verification evidence

| Run | Exact command | Exit | Result |
|---|---|---:|---|
| Initial build | `cmake --build build --target HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Regression target built before the production fix. |
| RED | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Regression failed as intended: actual ping state 2 (`Waiting`), expected 0 (`Idle`). QtTest reported 11 passed and 1 failed. |
| Fixed build | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt the application and affected test target. |
| GREEN | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | CTest passed 1/1 `HardwareIoTest` executable. |
| Remove fix | `git diff -- src/core/HardwareInsert.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Reversed only the production fix for the negative-control run. |
| Negative-control build | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt with the fix removed. |
| Negative-control RED | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Same regression failed with actual 2 (`Waiting`) and expected 0 (`Idle`). QtTest reported 11 passed and 1 failed. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Restored the production fix. |
| Final build | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt with the fix restored. |
| Final GREEN | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | CTest passed 1/1 `HardwareIoTest` executable. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-168.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |

The affected CTest family ran one test executable. Its RED QtTest output counted 12 cases (11 passed, one failed); the GREEN family runs exited successfully. The `fff` grep tool was not available in this session, so build and test logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/HardwareInsert.cpp`
- `tests/src/core/HardwareIoTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-168.md`
