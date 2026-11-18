# Cluster new-p9-463

Verdict: **FIXED**

## Evidence

[`plugins/TapTempo/TapTempo.cpp:107`](../../plugins/TapTempo/TapTempo.cpp) now returns from `sync()` unless the measured BPM is positive and finite. The regression in [`tests/src/core/TapTempoTest.cpp:45`](../../tests/src/core/TapTempoTest.cpp) constructs a fresh tool, confirms its BPM is zero, and checks that syncing leaves the live song tempo unchanged.

Before the fix, the assertion failed with actual tempo `10` and expected tempo `140`. The song model clamped the zero value to its minimum.

## Build and regression commands

Commands ran from the worktree root unless marked `build/tests`. The first build attempt wrote `build-1.log`, but its execution wrapper yielded before exposing the command status; it was superseded as evidence by the recorded baseline build below.

| Step | Exact command | Recorded result |
| --- | --- | --- |
| Baseline build, before fix | `cmake --build build --target zene TapTempoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| RED, from `build/tests` | `ctest -R TapTempoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8`; 1/1 failed on the tempo comparison |
| Fixed build | `cmake --build build --target zene TapTempoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| GREEN, from `build/tests` | `ctest -R TapTempoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; 1/1 passed |

## Fix-removal proof

| Step | Exact command | Recorded result |
| --- | --- | --- |
| Save and reverse product patch | `git diff -- plugins/TapTempo/TapTempo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Rebuild without fix | `cmake --build build --target zene TapTempoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| RED without fix, from `build/tests` | `ctest -R TapTempoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8`; same tempo assertion failed |
| Restore saved fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Rebuild with fix restored | `cmake --build build --target zene TapTempoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| GREEN with fix restored, from `build/tests` | `ctest -R TapTempoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; 1/1 passed |

The affected CTest family matched and executed one test (`TapTempoTest`) per run; no zero-test run occurred.

## Ratchet gates

| Exact command | Recorded result |
| --- | --- |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-463.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |

The `fff grep` tool was not available in this session, so build, test, and gate logs were searched with `rg`, one requested term at a time.

## Files committed

- `plugins/TapTempo/TapTempo.cpp`
- `tests/src/core/TapTempoTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-463.md`
