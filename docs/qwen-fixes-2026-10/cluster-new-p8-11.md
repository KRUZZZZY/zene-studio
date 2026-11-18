# Cluster new-p8-11

**Verdict: FIXED**

In [`ControlCommandsAutomationTools.cpp`](../../src/core/ControlCommandsAutomationTools.cpp#L153), `movePoint` now returns a tick-specific `Refused` result when the requested song tick is before the automation clip origin. Value-range validation remains separate and continues to use `automatableRangeRefusal`.

The regression in [`ControlAutomationScriptTest.cpp`](../../tests/src/core/ControlAutomationScriptTest.cpp#L358) creates a clip at song tick 384, addresses its point at tick 400, and requests tick 368 with a valid parameter value. It checks the exact tick-specific error and confirms the original point remains.

## Evidence

The final ctest family contained one registered test: `ControlAutomationScriptTest`. The original-code run failed on the regression assertion with `value 100 is outside the range 0..200`; the fixed run passed 1/1. The patch-removal proof repeated the same failure after removing only the production change.

| Check | Exact command | Exit |
|---|---|---:|
| Original-code build | `cmake --build build --target zene tripleoscillator ControlAutomationScriptTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/build-8.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/build-8.log; exit "$rc"` | 0 |
| Original-code RED | `cd build/tests && ctest -R ControlAutomationScriptTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/test-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/test-7.log; exit "$rc"` | 8 |
| Fixed build | `cmake --build build --target zene tripleoscillator ControlAutomationScriptTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/build-9.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/build-9.log; exit "$rc"` | 0 |
| First fixed GREEN | `cd build/tests && ctest -R ControlAutomationScriptTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/test-8.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/test-8.log; exit "$rc"` | 0 |
| Remove-only-the-fix proof | `git diff -- src/core/ControlCommandsAutomationTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/fix.patch` | 0 |
| Patch-removal build | `cmake --build build --target zene tripleoscillator ControlAutomationScriptTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/build-10.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/build-10.log; exit "$rc"` | 0 |
| Patch-removal RED | `cd build/tests && ctest -R ControlAutomationScriptTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/test-9.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/test-9.log; exit "$rc"` | 8 |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/fix.patch` | 0 |
| Reapplied build | `cmake --build build --target zene tripleoscillator ControlAutomationScriptTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/build-11.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/build-11.log; exit "$rc"` | 0 |
| Reapplied GREEN | `cd build/tests && ctest -R ControlAutomationScriptTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/test-10.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/test-10.log; exit "$rc"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/gate-3.log; exit "$rc"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-11.r1/logs/gate-4.log; exit "$rc"` | 0 |

Each test run executed one ctest test. Both builds surrounding the patch-removal run exited 0.

## Files in the commit

- `src/core/ControlCommandsAutomationTools.cpp`
- `tests/src/core/ControlAutomationScriptTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-11.md`
