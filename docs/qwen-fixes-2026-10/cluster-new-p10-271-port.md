# Cluster new-p10-271-port

**Verdict: FIXED**

Re-applied by hand on top of the 050/m0-train-5 line. `plugins/NeuralAmp/tests/nam_harness.cpp` now
computes the generated block total with `int64_t` operands and rejects runs above 4,194,304 samples
before model loading or vector allocation; generated mode also rejects negative warmup counts. The
checked total is used for the input/output buffers.

## Evidence

| Phase | Exact command (working directory: repository root unless noted) | Exit |
|---|---|---:|
| Original build | `cmake --build build --target nam_harness -j 8 > .../logs/build-1.log 2>&1; echo EXIT=$?` (after reversing product patch) | 0 |
| RED with product patch reversed | `python3 tests/neuralamp-harness-block-count.py build/plugins/nam_harness > .../logs/test-2.log 2>&1; echo EXIT=$?` | 1 |
| Restore product patch | `git apply .../logs/fix.patch; echo EXIT=$?` | 0 |
| Restored build | `cmake --build build --target nam_harness -j 8 > .../logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after restore | `python3 tests/neuralamp-harness-block-count.py build/plugins/nam_harness > .../logs/test-3.log 2>&1; echo EXIT=$?` | 0 |
| Initial GREEN (pre-reverse) | `python3 tests/neuralamp-harness-block-count.py build/plugins/nam_harness > .../logs/test-1.log 2>&1; echo EXIT=$?` | 0 |

Affected-family count: 1 targeted CLI regression (NeuralAmpHarnessBlockCount), run directly against
`build/plugins/nam_harness`.

Reverse-patch proof: `git diff -- plugins/NeuralAmp/tests/nam_harness.cpp > .../logs/fix.patch &&
git apply -R .../logs/fix.patch` (EXIT=0); rebuild EXIT=0; test EXIT=1 (RED). Restore:
`git apply .../logs/fix.patch` (EXIT=0); rebuild EXIT=0; test EXIT=0 (GREEN).

Gates (pre-existing failures on this branch, none introduced by this change):
- `bash tests/complexity-gate.sh --check` EXIT=1 (6 pre-existing "new function over target"
  regressions, none in touched files)
- `bash tests/file-length-gate.sh --check` EXIT=1 (14 pre-existing "new file over 500 lines"
  regressions, none in touched files)
- `bash tests/fork-sources-gate.sh` EXIT=0

## Files in this commit
- `plugins/NeuralAmp/tests/nam_harness.cpp`
- `tests/neuralamp-harness-block-count.py`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-271-port.md`

## Self-check

- Regression fails without the fix and passes with it (exit codes): Reverted the product change with `git apply -R .../logs/fix.patch` (EXIT=0), rebuilt nam_harness (BUILD_EXIT=0), ran `python3 tests/neuralamp-harness-block-count.py build/plugins/nam_harness` -> TEST_EXIT=1 (RED: `std::length_error: cannot create std::vector larger than max_size()`). Restored with `git apply .../logs/fix.patch` (EXIT=0), rebuilt (BUILD_EXIT=0), reran -> TEST_EXIT=0 (GREEN). Logs: `build-red.log`, `test-red.log`, `build-green.log`, `test-green.log`. Current (with sibling wide-cast consistency edits) also GREEN: `test-sib.log` TEST_EXIT=0 and ctest family (2/2 passed, `test-family.log`).
- Deterministic: the test uses no wall-clock timing, sleeps or races. It drives the headless `nam_harness` binary with a fixed minimal WaveNet JSON model and fixed `--block/--blocks/--warmup` values; the seam is the harness's own argument validation and the `kMaxGeneratedSamples` (4194304) allocation limit. Overflow case uses `--blocks 2147483647` with default warmup 20.
- Fix at cause and complete: the cause was `opt.blocks + opt.warmupBlocks` evaluated as `int` before conversion. Now both operands are cast to `int64_t` before addition, and the resulting `totalSamples` is checked against `kMaxGeneratedSamples` before any allocation. Sibling arithmetic in the same function was also made wide: the timed-loop offset `(warmupBlocks + b) * blockSize` now casts `warmupBlocks` to `int64_t` before adding `b`, and the `measuredIn`/`measuredOut` pointer offsets cast `warmupBlocks` to `int64_t` before multiplying by `blockSize`. The warmup-loop offset already cast `b` first. `totalSamples` (wide) is the single source used to size `input` and `output`.
- No collateral damage: old projects/saved data are unaffected (test harness only). The new limit only rejects generated runs above 4,194,304 samples; the ctest family `NeuralAmpHarnessBlockCount` + `NeuralAmpWavRunSampleRateWarning` both pass (2/2). The wav-run path is untouched and still processes arbitrary WAVs.
- Gates: `python3 .../feature_check.py .` -> RESULT: PASS (all gates). Logs in the feature-check directory.
