# Cluster new-p9-177: short FFT windows

**Verdict: FIXED**

`precomputeWindow()` accepted zero- and one-sample Blackman-Harris, Hamming, and Hanning windows. Zero length returned success after dividing the gain by zero; length one reached the cosine formula with a zero denominator. The guard at `src/core/fft_helpers.cpp:109-112` now returns `-1` for those three window types when `length < 2`. Rectangular and default window behavior is unchanged.

The regression at `tests/src/core/AudioAnalysisTest.cpp:105-113` checks lengths zero and one for all three window types. Its first run failed on the original code with actual `0`, expected `-1`. The negative control also failed at the same assertion after reversing only the production patch.

## Evidence

The affected family is `AudioAnalysisTest`: **1 CTest target ran (1/1)** in each invocation; no run selected zero tests.

| Check | Command | Exit |
| --- | --- | ---: |
| Original build | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/build-1.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/build-1.log; exit "$rc"` | 0 |
| **RED** original code | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/test-1.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/test-1.log; exit "$rc"` | **8** |
| Fixed build | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/build-2.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/build-2.log; exit "$rc"` | 0 |
| **GREEN** fixed code | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/test-2.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/test-2.log; exit "$rc"` | **0** |
| Negative-control patch reversal | `git diff -- src/core/fft_helpers.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/fix.patch` | 0 |
| Negative-control build | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/build-3.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/build-3.log; exit "$rc"` | 0 |
| **RED** with fix reversed | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/test-3.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/test-3.log; exit "$rc"` | **8** |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/fix.patch` | 0 |
| Restored build | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/build-4.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/build-4.log; exit "$rc"` | 0 |
| **GREEN** after restore | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/test-4.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/test-4.log; exit "$rc"` | **0** |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/gate-1.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/gate-1.log; exit "$rc"` | **0** |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/gate-2.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-177.r1/logs/gate-2.log; exit "$rc"` | **0** |

## Files committed

- `src/core/fft_helpers.cpp`
- `tests/src/core/AudioAnalysisTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-177.md`
