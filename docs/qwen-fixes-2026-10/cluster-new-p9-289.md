# Cluster new-p9-289

Verdict: **FIXED**

`src/core/MixerSurroundMix.cpp:186-200` now records whether each stereo side receives a nonzero contribution and clears only that side's quiet flag. The regression in `tests/src/core/SurroundTest.cpp:233-260` checks a hard-left surround send with both receiver flags initially quiet. It fails on the original implementation because the left flag remains quiet.

## Evidence

| Check | Command | Exit | Result |
|---|---|---:|---|
| RED on original code | `cd build/tests && ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 | 1/1 CTest test ran; only the new assertion failed (7 passed, 1 failed). |
| GREEN after fix | `cd build/tests && ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | 1/1 CTest test passed. |
| Reverse-patch proof, RED | `cd build/tests && ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 | 1/1 CTest test ran; the new assertion failed (`leftAwake` false). |
| Reapplied patch, GREEN | `cd build/tests && ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | 1/1 CTest test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Passed after the report was added. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Passed after the report was added. |

Affected-family count: **1 CTest test** (`SurroundTest`).

## Validation commands and exit codes

Each build rebuilt `zene` and `SurroundTest`; each command printed the recorded exit code.

- `cmake --build build --target zene SurroundTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `cmake --build build --target zene SurroundTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `cd build/tests && ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=8**; initial test draft leaked probe state into the following test, corrected before `test-2`.
- `cd build/tests && ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=8**
- `cmake --build build --target zene SurroundTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `cd build/tests && ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `git diff -- src/core/MixerSurroundMix.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `cmake --build build --target zene SurroundTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `cd build/tests && ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=8**
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `cmake --build build --target zene SurroundTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `cd build/tests && ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-289.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — **EXIT=0**

## Files committed

- `src/core/MixerSurroundMix.cpp`
- `tests/src/core/SurroundTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-289.md`
