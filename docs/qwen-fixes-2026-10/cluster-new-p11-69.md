# Cluster new-p11-69: crash report count wording

**Verdict: FIXED**

`src/gui/CrashReportMenu.cpp:103-119` now keeps the existing zero and one-report wording and formats the numeric count with plural wording for larger counts. `tests/src/gui/CrashReportMenuTest.cpp:92-104` checks both pending states for a count of two.

The regression was red against the original implementation and failed again when the production patch was reversed. It passed after the fix was restored.

## Evidence

The ctest working directory for all rows below was `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-69/build/tests`.

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| RED, original implementation | `ctest -R '^CrashReportMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/test-1.log 2>&1; test_exit=$?; echo EXIT=$test_exit; exit "$test_exit"` | 8 | One test failed: actual text was “Armed, one report not yet offered”; expected “Armed, 2 reports not yet offered”. |
| GREEN, fix applied | `ctest -R '^CrashReportMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/test-2.log 2>&1; test_exit=$?; echo EXIT=$test_exit; exit "$test_exit"` | 0 | 1/1 tests passed. |
| RED, production patch reversed | `ctest -R '^CrashReportMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/test-3.log 2>&1; test_exit=$?; echo EXIT=$test_exit; exit "$test_exit"` | 8 | Same assertion failed with the original singular text. |
| GREEN, production patch restored | `ctest -R '^CrashReportMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/test-4.log 2>&1; test_exit=$?; echo EXIT=$test_exit; exit "$test_exit"` | 0 | 1/1 tests passed. |
| Affected family count | `CrashReportMenuTest` ctest selection above | 1 test | The green runs each report 1/1 tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/gate-3.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit; exit "$gate_exit"` | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/gate-4.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit; exit "$gate_exit"` | 0 | Passed. |
| Regression patch capture and reverse | `git diff -- src/gui/CrashReportMenu.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/fix.patch` | 0 | Removed only the production fix for the reversal run. |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/fix.patch` | 0 | Restored the fix before the final build and green run. |

The affected-family count is **1 test**, with the single test passing in each green run. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs`.

Build commands, each with exit code 0:

- `cmake --build build --target CrashReportMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/build-1.log 2>&1; build_exit=$?; echo EXIT=$build_exit; exit "$build_exit"`
- `cmake --build build --target zene CrashReportMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/build-2.log 2>&1; build_exit=$?; echo EXIT=$build_exit; exit "$build_exit"`
- `cmake --build build --target zene CrashReportMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/build-3.log 2>&1; build_exit=$?; echo EXIT=$build_exit; exit "$build_exit"`
- `cmake --build build --target zene CrashReportMenuTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-69.r1/logs/build-4.log 2>&1; build_exit=$?; echo EXIT=$build_exit; exit "$build_exit"`

The `fff` grep tool was not available in this session (`command -v fff || true` exited 0 with no output). I searched the numbered logs with `rg`, one term at a time (`error`, `FAILED`, and `Assertion`).

## Files committed

- `src/gui/CrashReportMenu.cpp`
- `tests/src/gui/CrashReportMenuTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-69.md`
