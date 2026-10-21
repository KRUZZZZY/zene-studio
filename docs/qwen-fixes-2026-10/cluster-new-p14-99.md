# Cluster new-p14-99 / new-p9-123 — EQ spectrum analyser (plugins/Eq/EqSpectrumView.cpp)

new-p14-99: FIXED

## Evidence — new-p14-99 (EQ spectrum analysis drops samples when a block crosses the FFT boundary)

| Area | Evidence |
| --- | --- |
| Verdict | REAL. The finding describes the code that was on this branch. |
| Bug as found | `plugins/Eq/EqSpectrumView.cpp:87-97` (before the fix) appended the **whole** block to `m_buffer` with `m_buffer[m_framesFilledUp++] = ...` and never stopped at the 2048 sample window, then `plugins/Eq/EqSpectrumView.cpp:126` reset the counter to `0` after the analysis. Every sample past the first 2048 of a crossing block was therefore discarded, and because it was written past the analysed window it also landed unwindowed in the upper half of the 4096 point FFT input (`m_buffer` is `FFT_BUFFER_SIZE*2` long, `plugins/Eq/EqSpectrumView.h:61`). With 300 frame periods the analyser reached 1800 samples and the 7th block pushed it to 2100: 2048 were analysed and 52 were dropped, and the drop delayed every following window by 52 samples. |
| Production fix | `plugins/Eq/EqSpectrumView.cpp:79-118` stages the merged samples in the new `m_pending` buffer, stops filling at the 2048 sample window, hands that window to the FFT (`m_buffer[0..2047]`) and keeps the frames of the block that did not fit into it pending for the next window. Nothing of the stream is dropped any more and the FFT input no longer receives stray samples above the window. |
| Header | `plugins/Eq/EqSpectrumView.h:57-70` declares `m_pending[FFT_BUFFER_SIZE]` and `m_pendingFrames` in place of the old `m_framesFilledUp` counter. |
| Other callers | `analyze()` is called from `plugins/Eq/EqEffect.cpp:172` (`m_inFftBands`) and `plugins/Eq/EqEffect.cpp:282` (`m_outFftBands`); `clear()` (`plugins/Eq/EqSpectrumView.cpp:190`) resets the pending count with the rest of the analyser state. Both analysers keep the same behaviour. The frozen reference copy in `tests/reference/Eq/` is untouched. |
| Regression | `tests/src/core/AudioAnalysisTest.cpp` — `theAnalyserKeepsTheSamplesThatDoNotFitIntoTheWindow()` feeds 41 blocks of 300 frames (12300 frames) and counts the analyses the analyser ran (the analyser disarms itself after every analysis, so `getActive()` is false after each one). The contract is that a stream of N frames yields exactly `N / 2048` whole windows: **6** with the fix, **5** without it, because the dropped tail delayed every following window. |
| Registry | `tests/upstream-modifications.txt` records the two inherited production files with the reason. |
| Test count | The affected family is `AudioAnalysisTest` (1 registered CTest test, 9 QtTest cases now), plus the sibling Eq tests `EqFilterTest` and `EqCurveTest`: 3 registered CTest tests, all passing. The requested `EqSpectrumView` CTest name is not registered in this configured tree (`ctest -N -R '^EqSpectrumView$'` lists 0 tests); the regression runs in the existing audio-analysis test, which compiles the production `plugins/Eq/EqSpectrumView.cpp` into it. |
| Commit files | `plugins/Eq/EqSpectrumView.cpp`, `plugins/Eq/EqSpectrumView.h`, `tests/src/core/AudioAnalysisTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p14-99.md`. |

### Exact verification commands and results (new-p14-99)

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-99.r1/logs`.

| Purpose | Exact command | Exit |
| --- | --- | ---: |
| Baseline build of the test target | `cmake --build build --target AudioAnalysisTest > .../logs/build-01.log 2>&1` | 0 |
| Baseline test run (unmodified tree) | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > .../logs/test-01.log 2>&1` | 0 (1 test passed) |
| Build with the regression added, product code untouched | `cmake --build build --target AudioAnalysisTest > .../logs/build-02.log 2>&1` | 0 |
| **RED** — regression against the original product code | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > .../logs/test-02.log 2>&1` | **8** — `FAIL! theAnalyserKeepsTheSamplesThatDoNotFitIntoTheWindow() Actual (analyses): 5, Expected (expected): 6` |
| Build with the fix applied | `cmake --build build --target zene eq AudioAnalysisTest > .../logs/build-03.log 2>&1` | 0 |
| **GREEN** — regression with the fix applied | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > .../logs/test-03.log 2>&1` | 0 (1 test passed) |
| Take the product fix out (step 4b) | `git diff -- plugins/Eq/EqSpectrumView.cpp plugins/Eq/EqSpectrumView.h > .../logs/fix-new-p14-99.patch && git apply -R .../logs/fix-new-p14-99.patch` | 0 |
| Rebuild with the fix reversed | `cmake --build build --target zene eq AudioAnalysisTest > .../logs/build-04.log 2>&1` | 0 |
| **RED** — same regression with the fix reversed | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > .../logs/test-04.log 2>&1` | **8** — `Actual (analyses): 5, Expected (expected): 6` |
| Restore the product fix | `git apply .../logs/fix-new-p14-99.patch` | 0 |
| Rebuild with the fix restored | `cmake --build build --target zene eq AudioAnalysisTest > .../logs/build-05.log 2>&1` | 0 |
| **GREEN** — same regression after restoration | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > .../logs/test-05.log 2>&1` | 0 (1 test passed) |
| Build the sibling Eq test targets | `cmake --build build --target EqFilterTest EqCurveTest > .../logs/build-06.log 2>&1` | 0 |
| Whole Eq family with the fix in place | `cd build/tests && ctest -R '^(EqFilterTest\|EqCurveTest\|AudioAnalysisTest)$' --output-on-failure > .../logs/test-06.log 2>&1` | 0 (3 of 3 tests passed) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-01.log 2>&1` | see the table below |

The two RED runs report the same assertion: the analyser ran 5 analyses where the stream of 12300 frames contains 6 whole windows — the 52 samples dropped at every window boundary delayed the sixth window past the end of the stream.
