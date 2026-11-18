# Cluster new-p11-28

Verdict: **FIXED**

## Finding and code evidence

The finding is real. `applySwing()` used to delay a full-strength even-grid swing to the exact midpoint. Nearest quantization assigns that midpoint to the next even step on a repeated call.

- `src/core/ControlCommandsGrooveEdit.cpp:312-321` now caps the delay at `(grid - 1) / 2`, keeping the swung note on the odd side of the nearest-step boundary.
- `tests/src/core/ControlGrooveCommandsTest.cpp:313-339` updates the even-grid expectation to tick 17 and checks that repeating the same grid-12, full-strength swing preserves the first result.

The product source is already registered in `tests/fork-sources.txt`. No control command or header changed.

## Test evidence

The affected CTest family contains **1 test target**, `ControlGrooveCommandsTest`. The pre-fix QtTest run reported 13 passed and 1 failed. Its regression failure showed `repeated = 24` versus `first = 18`. The final ctest run passed 1/1 targets.

Commands below ran from the worktree root unless marked `build/tests`. Output and shell exit codes were captured in the listed numbered logs.

| Stage | Exact command | Exit | Log / result |
|---|---|---:|---|
| Build regression before fix | `cmake --build build --target ControlGrooveCommandsTest` | 0 | `build-1.log` |
| RED against original implementation (`build/tests`) | `ctest -R ControlGrooveCommandsTest --output-on-failure` | 8 | `test-1.log`; regression compared 24 with 18 |
| Build fixed implementation | `cmake --build build --target zene ControlGrooveCommandsTest` | 0 | `build-2.log` |
| GREEN after fix (`build/tests`) | `ctest -R ControlGrooveCommandsTest --output-on-failure` | 0 | `test-2.log`; 1/1 passed |
| Reverse only the product diff | `git diff -- src/core/ControlCommandsGrooveEdit.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-28.r1/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-28.r1/logs/fix.patch'` | 0 | `proof-1.log` |
| Rebuild reverted implementation | `cmake --build build --target zene ControlGrooveCommandsTest` | 0 | `build-3.log` |
| RED with product diff reversed (`build/tests`) | `ctest -R ControlGrooveCommandsTest --output-on-failure` | 8 | `test-3.log`; the repeated-swing regression failed again |
| Reapply product diff | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-28.r1/logs/fix.patch'` | 0 | `proof-2.log` |
| Rebuild final implementation | `cmake --build build --target zene ControlGrooveCommandsTest` | 0 | `build-4.log` |
| Final GREEN (`build/tests`) | `ctest -R ControlGrooveCommandsTest --output-on-failure` | 0 | `test-4.log`; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log`; PASS |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log`; PASS |

The fff grep tool was not exposed in this session. I searched the numbered logs with separate `rg -n` queries for `error`, `FAILED`, and `Assertion`.

## Commit contents

- `src/core/ControlCommandsGrooveEdit.cpp`
- `tests/src/core/ControlGrooveCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-28.md`
