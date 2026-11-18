# Cluster new-p12-164: WAV-run sample-rate warning

**Verdict: FIXED**

In [nam_harness.cpp](../../plugins/NeuralAmp/tests/nam_harness.cpp), the generated-signal warning at lines 380–385 still compares against `opt.sampleRate`. WAV-run now checks `spec.sampleRate` against the WAV's `wavRate` after `readWav` succeeds at lines 401–406. The new integration test at [neuralamp-wav-run-sample-rate-warning.py](../../tests/neuralamp-wav-run-sample-rate-warning.py) runs a 48 kHz model against a generated 44.1 kHz WAV and requires the warning to name 44.1 kHz.

## Regression evidence

The affected CTest family is `NeuralAmpWavRunSampleRateWarning`: one test executed in each run. The test failed before the fix, passed after it, failed again with the product patch reversed, and passed after the patch was restored.

| Run | Exact command | Exit |
|---|---|---:|
| Original build (`build-1.log`) | `cmake --build build --target zene nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| RED (`test-red-1.log`) | `(cd build/tests && ctest -R '^NeuralAmpWavRunSampleRateWarning$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/test-red-1.log 2>&1); rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 |
| Fixed build (`build-2.log`) | `cmake --build build --target zene nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| GREEN (`test-green-1.log`) | `(cd build/tests && ctest -R '^NeuralAmpWavRunSampleRateWarning$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/test-green-1.log 2>&1); rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |

### Reverse-patch proof

| Step | Exact command | Exit |
|---|---|---:|
| Save product fix (`fix.patch`) | `git diff -- plugins/NeuralAmp/tests/nam_harness.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/fix.patch` | 0 |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/fix.patch` | 0 |
| Rebuild without fix (`build-proof-red-1.log`) | `cmake --build build --target zene nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/build-proof-red-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| RED without fix (`test-proof-red-1.log`) | `(cd build/tests && ctest -R '^NeuralAmpWavRunSampleRateWarning$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/test-proof-red-1.log 2>&1); rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/fix.patch` | 0 |
| Rebuild with fix (`build-proof-green-1.log`) | `cmake --build build --target zene nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/build-proof-green-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| GREEN with restored fix (`test-proof-green-1.log`) | `(cd build/tests && ctest -R '^NeuralAmpWavRunSampleRateWarning$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/test-proof-green-1.log 2>&1); rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |

## Required gates

| Gate | Command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/gate-complexity-final.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-164.r1/logs/gate-file-length-final.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |

`fff` grep was not available in this session, so `rg` was used to inspect the saved logs. The build logs had no `error` matches; the RED logs contained the missing-warning assertion and reported one failed test, while the GREEN logs reported one passed test.

## Files in this commit

- `plugins/NeuralAmp/tests/nam_harness.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/neuralamp-wav-run-sample-rate-warning.py`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-164.md`
