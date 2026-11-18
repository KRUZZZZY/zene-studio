# Cluster new-p12-120

Verdict: **FIXED**

## Finding and evidence

`src/core/MasteringReport.cpp:173-190` now writes through `QSaveFile`. It checks the complete JSON write and only replaces the destination after `commit()` succeeds. Open, write, and commit failures return `false` without truncating the previous report.

`tests/src/core/MasteringTest.cpp:445-466` creates a prior report, makes its directory unwritable while leaving the file writable, and verifies that a failed replacement returns `false` and retains the exact prior bytes. On the original implementation this test failed because `replacementSucceeded` was true after the prior file had been truncated and rewritten.

## Regression evidence

| Run | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED, original implementation | `ctest -R '^MasteringTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` (working directory: `build/tests`) | 8 | `!replacementSucceeded` returned false; QtTest totals: 9 passed, 1 failed. |
| GREEN, fixed implementation | `ctest -R '^MasteringTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` (working directory: `build/tests`) | 0 | CTest: 1/1 `MasteringTest` passed. |
| Proof RED, product patch reversed | `ctest -R '^MasteringTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` (working directory: `build/tests`) | 8 | The same `!replacementSucceeded` assertion failed; QtTest totals: 9 passed, 1 failed. |
| Proof GREEN, product patch restored | `ctest -R '^MasteringTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` (working directory: `build/tests`) | 0 | CTest: 1/1 `MasteringTest` passed. |

Affected family: one CTest test (`MasteringTest`). Its RED run executed ten QtTest cases (nine passed and the regression failed); the fixed family passed 1/1.

## Builds, patch proof, and gates

Build commands and exits:

- `cmake --build build --target MasteringTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` — 0.
- `cmake --build build --target zene MasteringTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` — 0.
- `git diff -- src/core/MasteringReport.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/fix.patch; status=$?; echo EXIT=$status; exit $status` — 0.
- `cmake --build build --target zene MasteringTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` — 0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/fix.patch; status=$?; echo EXIT=$status; exit $status` — 0.
- `cmake --build build --target zene MasteringTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` — 0.

Ratchet gates:

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/gate-complexity-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` — 0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-120.r1/logs/gate-file-length-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` — 0.

The `fff` grep tool was not exposed in this session; logs were searched term by term with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/MasteringReport.cpp`
- `tests/src/core/MasteringTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-120.md`
