# Cluster new-p8-312 — FIXED

## Verdict and evidence

**FIXED.** The finding is real. `slicerState()` in `src/core/ControlCommandsAudioSlice.cpp:152-166` serialized the final SlicerT boundary as `1.0`, the full source end. It now uses `clip.sampleWindow().sourceOut / sample.sampleSize()`. The regression in `tests/src/core/AudioEditingTest.cpp:230-263` trims a sample window to half its source, slices through `clip.slice_to_instrument`, saves the SlicerT state, and checks that its final boundary is `0.5`.

| Evidence | Command | Exit | Result |
|---|---|---:|---|
| RED on original production code | `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/test-3.log`) | 8 | Regression failed: final boundary `1`, expected `0.5`. CTest ran 1/1 target. |
| GREEN with fix restored | `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure -V` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/test-6.log`) | 0 | CTest ran 1/1 target; QtTest totals: 10 passed, 0 failed, 0 skipped. |
| Revert proof RED | Same CTest family after reversing only the product patch (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/test-5.log`) | 8 | The identical boundary assertion failed again: `1` versus `0.5`; QtTest totals: 9 passed, 1 failed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/gate-1.log`) | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` (log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/gate-2.log`) | 0 | Passed. |

The first test attempt ran before the SlicerT module target had been built. Its QtTest transcript showed the existing slicer test and the new regression skipped because the module was missing. After building `slicert`, the regression ran and produced the RED result above; no skipped test is counted as regression evidence.

## Build and test command record

Exit codes below were captured immediately with `rc=$?; printf 'EXIT=%s\n' "$rc"`. All build and test output was redirected to new files in the run log folder.

| Command | Exit |
|---|---:|
| `cmake --build build --target zene AudioEditingTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/build-1.log 2>&1` | 0 |
| `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/test-1.log 2>&1` | 0 (initial run; slicer slots skipped) |
| `cd build/tests && ctest -R '^AudioEditingTest$' -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/test-2.log 2>&1` | 0 (8 passed, 2 skipped: SlicerT was not built yet) |
| `cmake --build build --target zene AudioEditingTest slicert -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/build-2.log 2>&1` | 0 |
| `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/test-3.log 2>&1` | 8 (RED) |
| `cmake --build build --target zene AudioEditingTest slicert -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/build-3.log 2>&1` | 0 |
| `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/test-4.log 2>&1` | 0 (GREEN; 10 passed) |

## Revert proof and final verification

The required product-only patch proof used these exact commands:

```sh
git diff -- src/core/ControlCommandsAudioSlice.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/fix.patch
```

Exit: **0**. With the product patch reversed:

| Command | Exit |
|---|---:|
| `cmake --build build --target zene AudioEditingTest slicert -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/build-4.log 2>&1` | 0 |
| `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/test-5.log 2>&1` | 8 (same assertion RED) |

The patch was restored with:

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/fix.patch
```

Exit: **0**. Final build and green test:

| Command | Exit |
|---|---:|
| `cmake --build build --target zene AudioEditingTest slicert -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/build-5.log 2>&1` | 0 |
| `cd build/tests && ctest -R '^AudioEditingTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/test-6.log 2>&1` | 0 (GREEN; 10 passed) |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/gate-1.log 2>&1` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-312.r1/logs/gate-2.log 2>&1` | 0 |

## Files in the fix commit

- `src/core/ControlCommandsAudioSlice.cpp` — bound the final SlicerT point to the clip window end.
- `tests/src/core/AudioEditingTest.cpp` — added the trimmed-window regression. This product-new test file is already registered in `tests/fork-sources.txt`.
- `docs/qwen-fixes-2026-10/cluster-new-p8-312.md` — this evidence report.
