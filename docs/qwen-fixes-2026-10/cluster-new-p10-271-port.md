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
