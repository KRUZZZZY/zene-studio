# Cluster new-p10-374: Labels lose literal ampersands

**Verdict: FIXED**

`src/gui/shell/ShellChrome.cpp:75-93` now parses the label before its tab-separated shortcut suffix. A doubled `&&` becomes one displayed ampersand, while a single mnemonic marker is removed. The existing `labelOf()` consumers use the parsed result for both button sizing and painting.

Regression coverage is in `tests/src/gui/ShellHomesTest.cpp:250-259`. For `Rock && Roll`, it checks the text-beside-icon button width against the rendered label `Rock & Roll`.

The regression failed before the fix with actual width 87 vs expected 94 (15 Qt test cases passed and the new case failed). Reversing only the product patch reproduced the same failure. Both the fixed focused run and the three-test shell family run passed.

## Evidence

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs`. Build commands ran from the worktree root; ctest commands ran from `build/tests`.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Build original code and regression target | `cmake --build build --target zene ShellHomesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | Built before RED run. |
| RED on original code | `ctest -R '^ShellHomesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/red-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 8 | `ShellHomesTest` 1/1 target failed at the new assertion, actual 87 vs expected 94. QtTest totals: 15 passed, 1 failed. |
| Build fixed code | `cmake --build build --target zene ShellHomesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | Rebuilt `zene` and the affected test target. |
| GREEN focused run | `ctest -R '^ShellHomesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/green-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | `ShellHomesTest` passed 1/1. |
| Reverse product patch | `git diff -- src/gui/shell/ShellChrome.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/fix.patch` | 0 | Removed only the product fix for the proof check. |
| Build reverted code | `cmake --build build --target zene ShellHomesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | Rebuilt before rerunning the regression. |
| RED with product patch reversed | `ctest -R '^ShellHomesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/proof-red-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 8 | Same assertion failed again, actual 87 vs expected 94; 15 passed, 1 failed. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/fix.patch` | 0 | Restored the fix. |
| Rebuild restored code | `cmake --build build --target zene ShellHomesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | Rebuilt before final proof run. |
| GREEN with product patch restored | `ctest -R '^ShellHomesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/proof-green-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | `ShellHomesTest` passed 1/1. |
| Build shell-facing test targets | `cmake --build build --target zene ShellHomesTest ShellTest AccessibilityHelperTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/build-5.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | Rebuilt all three affected test targets and `zene`. |
| Shell-family verification | `ctest -R '^(AccessibilityHelperTest|ShellHomesTest|ShellTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/family-green-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | 3/3 CTest targets passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-374.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 | Passed. |

## Files committed

- `src/gui/shell/ShellChrome.cpp`
- `tests/src/gui/ShellHomesTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-374.md`
