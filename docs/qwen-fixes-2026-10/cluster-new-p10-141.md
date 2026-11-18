# Cluster new-p10-141: DrumSynth distortion bits underflow

**Verdict: FIXED**

## Finding and evidence

`src/core/DrumSynth.cpp:517-520` read the distortion `Bits` value without a range check, then assigned `2^(2*Bits)` to the `float` attenuation used as a divisor at line 870. For `Bits=-100`, that value underflows to zero. The renderer now rejects values outside the bundled presets' 0–7 range and refuses attenuation that is zero, negative, or nonfinite before allocating the output buffer or entering quantization.

The regression is `ImportDetectionTest::distortionRejectsUnderflowingDrumSynthBits` at `tests/src/core/ImportDetectionTest.cpp:205-227`. It changes the bundled `click.ds` preset to `Bits=-100` and checks that the public renderer refuses it without producing output. Against the original source it failed because the renderer returned 7,200 frames. With the fix it passes.

| Evidence | Command / result |
|---|---|
| RED, original source | `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/test-red-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=8; the assertion reported 7,200 frames. |
| GREEN, fixed source | `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/test-green-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0. |
| Affected family count | 1 registered CTest executable; 8 QtTest functions ran, 8 passed and 0 failed (`test-detail-1.log`). |
| Reversal proof RED | After reversing only `src/core/DrumSynth.cpp`, the same CTest command wrote `test-red-2.log` and exited 8; the assertion again reported 7,200 frames. |
| Reversal proof GREEN | After reapplying the patch and rebuilding, the same CTest command wrote `test-green-2.log` and exited 0. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/complexity-gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/file-length-gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0. |

## Build and proof commands

Each build command requested `zene` and `ImportDetectionTest`; every build exited 0.

- `cmake --build build --target zene ImportDetectionTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0 (regression built before production fix).
- `cmake --build build --target zene ImportDetectionTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0.
- `./ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/test-detail-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0.
- `git diff -- src/core/DrumSynth.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0.
- `cmake --build build --target zene ImportDetectionTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0.
- Reversal RED: `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/test-red-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=8.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0.
- `cmake --build build --target zene ImportDetectionTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0.
- Reversal GREEN: `ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-141.r1/logs/test-green-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0.

The `fff` grep tool was not available in this session; log searches used `rg` against the specified log folder.

## Files committed

- `src/core/DrumSynth.cpp`
- `tests/src/core/ImportDetectionTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-141.md`
