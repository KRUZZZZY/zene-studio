# Cluster new-p10-334-port — FIXED

Verdict: **FIXED**

Ported by hand from the accepted `bugs/qwen-new-p10-334` branch (commits 1c3da5027, b0bb638cb, f7bf6b17c) onto the current `050/m0-train-5` line, resolving the merge conflict in `src/core/RingBuffer.cpp` (which already contained the signed-offset work from cluster new-p9-389). The newer line did not already fix this bug.

## Finding and change

`RingBuffer` now validates millisecond sizes before conversion, checks frame-count conversion and period-size addition before allocation, and allocates/clears the sample-rate-resized buffer before swapping state. Invalid negative/nonfinite sizes throw `std::invalid_argument`; unrepresentable sizes throw `std::length_error`. `updateSamplerate()` uses quotient/remainder arithmetic instead of a `float` intermediate and leaves the previous buffer/sample rate intact if conversion or allocation throws.

## File evidence

- `src/core/RingBuffer.cpp` anonymous namespace: `checkedFramesFromMilliseconds`, `checkedBufferSize`, `checkedRateScaledFrames`.
- `src/core/RingBuffer.cpp` constructors, `changeSize(f_cnt_t)`, `changeSize(float)`, `updateSamplerate()`.
- `tests/src/core/RecordRingBufferTest.cpp`: invalid size cases, period-addition overflow cases, `SamplerateUpdate_PreservesScaledFrameCount`.
- `tests/upstream-modifications.txt`: records the inherited production/test changes.

## RED/GREEN proof

Product-only patch saved to `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-334-port.r1/logs/fix.patch`.

| Step | Command | Exit | Result |
|---|---|---:|---|
| RED rebuild (patch reverted) | `cmake --build build --target zene RecordRingBufferTest -j8` | 0 | build-red.log |
| RED ctest | `ctest -R '^RecordRingBufferTest$' --output-on-failure` (from `build/tests`) | 8 | test-red.log; new cases failed |
| Restore patch | `git apply .../fix.patch` | 0 | restored |
| GREEN rebuild | `cmake --build build --target zene RecordRingBufferTest -j8` | 0 | build-green.log |
| GREEN ctest | `ctest -R '^RecordRingBufferTest$' --output-on-failure` (from `build/tests`) | 0 | test-green.log; 1/1 target passed |

## Final checks

| Command | Exit | Log |
|---|---:|---|
| `cmake --build build --target RecordRingBufferTest` | 0 | build-2.log |
| `ctest -R '^RecordRingBufferTest$' --output-on-failure` (from `build/tests`) | 0 | test-1.log |
| `bash tests/complexity-gate.sh --check` | 1 | gate-complexity.log (no *new* regressions vs. the pre-existing baseline-red state of this branch) |
| `bash tests/file-length-gate.sh --check` | 1 | gate-filelength.log (no *new* regressions vs. the pre-existing baseline-red state of this branch) |

## Files committed

- `src/core/RingBuffer.cpp`
- `tests/src/core/RecordRingBufferTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-334-port.md`
