# Cluster new-p12-113: markers crossed after resume

Verdict: **FIXED**

## Evidence

- [src/core/MarkerActions.cpp:152](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-113/src/core/MarkerActions.cpp:152): while paused, the watcher now records the current tick as the resume mark. On resume, `follow()` checks the interval from that tick. A stopped transport still clears the mark through `advance(now, false)`.
- [tests/src/core/SessionPerformTest.cpp:226](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-113/tests/src/core/SessionPerformTest.cpp:226): the regression waits for the watcher to observe pause, creates a marker after that tick, resumes, crosses it, and checks the action's effect. The earlier marker test now removes its marker at line 150 to keep this scenario isolated.

## Verification

The red run was made with the production fix reversed using `fix.patch`, then rebuilt. It failed on the regression assertion: the session quantisation remained `None` (0), expected `Bar` (1). The fix was reapplied, rebuilt, and the same CTest command passed. One affected-family CTest target ran (`SessionPerformTest`, 1/1); the red QtTest output reported 10 passed and the new regression failed.

| Evidence item | Result |
|---|---|
| Affected-family tests | 1 CTest target executed (`SessionPerformTest`), 1/1; RED had 10 QtTest passes and the regression failure |
| Files committed | `src/core/MarkerActions.cpp`; `tests/src/core/SessionPerformTest.cpp`; `docs/qwen-fixes-2026-10/cluster-new-p12-113.md` |

| Phase | Command and working directory | EXIT | Result |
|---|---|---:|---|
| RED build, from the worktree | `cmake --build build --target zene SessionPerformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-113.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Rebuilt with the fix reversed |
| RED test, from `build/tests` | `ctest -R '^SessionPerformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-113.r1/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 8 | Regression failed as expected |
| GREEN build, from the worktree | `cmake --build build --target zene SessionPerformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-113.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Rebuilt with the fix reapplied |
| GREEN test, from `build/tests` | `ctest -R '^SessionPerformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-113.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | 1/1 CTest target passed |
| Complexity gate, from the worktree | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-113.r1/logs/complexity-gate-4.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Passed |
| File-length gate, from the worktree | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-113.r1/logs/file-length-gate-4.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Passed |
