# Cluster new-p12-35

Verdict: **FIXED**

## Evidence

- `src/core/ControlCommandsRack.cpp:67` checks chain values before narrowing; the `rack.remove_chain` and `rack.set_selected` schemas bound `chain` to the `int` range at lines 263 and 334.
- `src/core/ControlCommandsRackMacros.cpp:74` checks macro index values before narrowing; `targetFromArgs` now receives checked `int` values at line 90. The `rack.macro_target_add` schema bounds `chain` and `effect` at lines 251 and 253, and `rack.macro_target_remove` bounds `target` at line 353.
- `tests/src/core/RackMacrosTest.cpp:404` exercises an out-of-range `target` through the command surface and verifies the bounds on all five integer fields.
- Before the fix, `target: 2147483648` reached the handler and returned `NotFound` (error kind 1); the regression expected schema-level `InvalidArgs` (error kind 3). With the fix reversed, the same assertion failed again.

## Verification

`RackMacrosTest` is the affected ctest family; ctest executed 1 registered test. The RED runs reported 9 Qt test cases passed and the new regression failed. The GREEN runs passed the registered test.

| Stage | Exact command (working directory) | Exit |
|---|---|---:|
| Regression build | `cmake --build build --target RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (worktree) | 0 |
| Original RED | `ctest -R RackMacrosTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/red-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (`build/tests`) | 8 |
| Fixed build | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (worktree) | 0 |
| Initial GREEN | `ctest -R RackMacrosTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/green-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (`build/tests`) | 0 |
| Save product patch | `git diff -- src/core/ControlCommandsRack.cpp src/core/ControlCommandsRackMacros.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (worktree) | 0 |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (worktree) | 0 |
| Reversed build | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (worktree) | 0 |
| Reversed RED | `ctest -R RackMacrosTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/proof-red-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (`build/tests`) | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (worktree) | 0 |
| Restored build | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (worktree) | 0 |
| Restored GREEN | `ctest -R RackMacrosTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/proof-green-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (`build/tests`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (worktree) | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-35.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (worktree) | 0 |

## Files in the commit

- `src/core/ControlCommandsRack.cpp`
- `src/core/ControlCommandsRackMacros.cpp`
- `tests/src/core/RackMacrosTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-35.md`
