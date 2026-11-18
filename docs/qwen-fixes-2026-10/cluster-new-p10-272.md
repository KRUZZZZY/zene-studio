# Cluster new-p10-272: WAV input accepts a zero sample rate

**Verdict: FIXED**

## Evidence

[`plugins/NeuralAmp/tests/nam_harness.cpp:247`](../../plugins/NeuralAmp/tests/nam_harness.cpp) now rejects a parsed sample rate of zero along with missing data and non-mono input. `wav-run` only receives the rate after this check, so it cannot calculate duration from a zero-rate WAV.

[`tests/src/core/NeuralAmpWavInputTest.cpp:80`](../../tests/src/core/NeuralAmpWavInputTest.cpp) builds a valid mono PCM16 WAV with rate 0 and asserts that the actual `readWav` function rejects it. Its positive-rate control at line 90 verifies that a 44100 Hz WAV remains accepted. The test includes the harness translation unit with its CLI `main` renamed so it calls the exact parser under test.

## Regression proof

All CTest commands ran from `build/tests`. The affected family contains one registered CTest test; it covers both the zero-rate regression and the positive-rate control.

| Phase | Exact command | Exit | Evidence |
|---|---|---:|---|
| RED, unfixed code | `ctest -R NeuralAmpWavInputTest --output-on-failure` | 8 | `test-1.log`: `zeroSampleRateIsRejected()` failed because `readWav` returned true. |
| GREEN, fixed code | `ctest -R NeuralAmpWavInputTest --output-on-failure` | 0 | `test-2.log`: 1/1 tests passed. |
| Inverse-patch RED | `ctest -R NeuralAmpWavInputTest --output-on-failure` | 8 | `test-3.log`: the same assertion failed after removing the production guard. |
| Restored GREEN | `ctest -R NeuralAmpWavInputTest --output-on-failure` | 0 | `test-4.log`: 1/1 tests passed after reapplying the guard. |

The inverse-patch steps also succeeded: `git diff -- plugins/NeuralAmp/tests/nam_harness.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-272.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-272.r1/logs/fix.patch` exited 0; `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-272.r1/logs/fix.patch` exited 0.

## Builds and gates

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-272.r1/logs`.

| Exact command | Exit | Logs |
|---|---:|---|
| `cmake --build build --target zene NeuralAmpWavInputTest` | 2 | `build-1.log`; CMake regenerated the target during this first invocation, but Make still resolved the new target against its previous rules. |
| `cmake --build build --target zene` | 0 | `build-2.log`, `build-4.log`, `build-6.log`, `build-8.log` |
| `cmake --build build --target NeuralAmpWavInputTest` | 0 | `build-3.log`, `build-5.log`, `build-7.log`, `build-9.log` |
| `bash tests/fork-sources-gate.sh` | 0 | `gate-3.log` |
| `bash tests/complexity-gate.sh --check` | 0 | `gate-4.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-5.log` |

The requested fff grep tool was unavailable in this session. I searched the logs with `rg -n`, checking `error`, `FAILED`, and `Assertion` separately.

## Files committed

- `plugins/NeuralAmp/tests/nam_harness.cpp`
- `tests/src/core/NeuralAmpWavInputTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-272.md`
