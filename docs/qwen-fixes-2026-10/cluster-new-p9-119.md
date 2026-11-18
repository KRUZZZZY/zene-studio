# Cluster new-p9-119 — FIXED

## Evidence

| Area | Evidence |
| --- | --- |
| Production fix | [EqSpectrumView.cpp:122-124](../../plugins/Eq/EqSpectrumView.cpp) saves the maximum merged-buffer value and returns zero energy when it is zero. |
| Regression | [AudioAnalysisTest.cpp:153-160](../../tests/src/core/AudioAnalysisTest.cpp) feeds one full FFT window of `{0.5f, -0.5f}` stereo frames and asserts finite zero energy. The original code fails the finite assertion with NaN. |
| Registry | [upstream-modifications.txt:807](../../tests/upstream-modifications.txt) records the inherited production-file change. |
| Test count | The affected CTest family is `AudioAnalysisTest`: 1 registered CTest test, covering 8 QtTest cases. The requested `EqSpectrumView` CTest name is not registered in this configured tree (`ctest -N -R '^EqSpectrumView$'` reports 0); the regression runs in the existing audio-analysis test. |
| Commit files | `plugins/Eq/EqSpectrumView.cpp`, `tests/src/core/AudioAnalysisTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p9-119.md`. |

The test compiles the private analyser implementation into `AudioAnalysisTest`. The unused GUI view's `Q_OBJECT` metadata is suppressed in that test translation unit, and its unused `EqHandle::freqToXPixel` reference is stubbed. The analyser code under test is the production implementation.

## Exact verification commands and results

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs`.

| Purpose | Exact command | Exit |
| --- | --- | ---: |
| Test discovery; listing only | `cd build/tests && ctest -N -R '^EqSpectrumView$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/inspect-01.log 2>&1` | 0 (0 tests listed) |
| Initial test-target build attempt; harness link failed before any test ran | `cmake --build build --target AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/build-01.log 2>&1; build_status=$?; echo EXIT=$build_status; exit "$build_status"` | 2 |
| Build corrected test harness against original production code | `cmake --build build --target AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/build-02.log 2>&1; build_status=$?; echo EXIT=$build_status; exit "$build_status"` | 0 |
| RED, original production code | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/test-01.log 2>&1; test_status=$?; echo EXIT=$test_status; exit "$test_status"` | 8 |
| Build with fix applied | `cmake --build build --target zene eq AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/build-03.log 2>&1; build_status=$?; echo EXIT=$build_status; exit "$build_status"` | 0 |
| GREEN, production fix applied | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/test-02.log 2>&1; test_status=$?; echo EXIT=$test_status; exit "$test_status"` | 0 |
| Remove the product fix for the proof cycle | `git diff -- plugins/Eq/EqSpectrumView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/fix.patch; patch_status=$?; echo EXIT=$patch_status; exit "$patch_status"` | 0 |
| Rebuild with fix reversed | `cmake --build build --target zene eq AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/build-04.log 2>&1; build_status=$?; echo EXIT=$build_status; exit "$build_status"` | 0 |
| RED, fix reversed | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/test-03.log 2>&1; test_status=$?; echo EXIT=$test_status; exit "$test_status"` | 8 |
| Restore the product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/fix.patch; patch_status=$?; echo EXIT=$patch_status; exit "$patch_status"` | 0 |
| Rebuild with fix restored | `cmake --build build --target zene eq AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/build-05.log 2>&1; build_status=$?; echo EXIT=$build_status; exit "$build_status"` | 0 |
| GREEN, same regression after restoration | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/test-04.log 2>&1; test_status=$?; echo EXIT=$test_status; exit "$test_status"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/gate-01.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit "$gate_status"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-119.r1/logs/gate-02.log 2>&1; gate_status=$?; echo EXIT=$gate_status; exit "$gate_status"` | 0 |

The two RED runs each report `AudioAnalysisTest::antiPhaseStereoHasZeroEnergy()` failing because `std::isfinite()` receives NaN. The GREEN runs pass the single registered CTest test. Both ratchet gates pass.
