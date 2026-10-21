# Cluster new-p14-99 / new-p9-123 — EQ spectrum analyser (plugins/Eq/EqSpectrumView.cpp)

new-p14-99: FIXED

## Evidence — new-p14-99 (EQ spectrum analysis drops samples when a block crosses the FFT boundary)

| Area | Evidence |
| --- | --- |
| Verdict | REAL. The finding describes the code that was on this branch. |
| Bug as found | `plugins/Eq/EqSpectrumView.cpp:87-97` (before the fix) appended the **whole** block to `m_buffer` with `m_buffer[m_framesFilledUp++] = ...` and never stopped at the 2048 sample window, then `plugins/Eq/EqSpectrumView.cpp:126` reset the counter to `0` after the analysis. Every sample past the first 2048 of a crossing block was therefore discarded, and because it was written past the analysed window it also landed unwindowed in the upper half of the 4096 point FFT input (`m_buffer` is `FFT_BUFFER_SIZE*2` long, `plugins/Eq/EqSpectrumView.h:80`). With 300 frame periods the analyser reached 1800 samples and the 7th block pushed it to 2100: 2048 were analysed and 52 were dropped, and the drop delayed every following window by 52 samples. |
| Production fix | `plugins/Eq/EqSpectrumView.cpp:79-118` stages the merged samples in the new `m_pending` buffer, stops filling at the 2048 sample window, hands that window to the FFT (`m_buffer[0..2047]`) and keeps the frames of the block that did not fit into it pending for the next window. Nothing of the stream is dropped any more and the FFT input no longer receives stray samples above the window. |
| Header | `plugins/Eq/EqSpectrumView.h:83-84` declares `m_pending[FFT_BUFFER_SIZE]` and `m_pendingFrames` in place of the old `m_framesFilledUp` counter. |
| Other callers | `analyze()` is called from `plugins/Eq/EqEffect.cpp:172` (`m_inFftBands`) and `plugins/Eq/EqEffect.cpp:282` (`m_outFftBands`); `clear()` (`plugins/Eq/EqSpectrumView.cpp:252`) resets the pending count with the rest of the analyser state. Both analysers keep the same behaviour. The frozen reference copy in `tests/reference/Eq/` is untouched. |
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

new-p9-123: FIXED

## Evidence — new-p9-123 (EQ spectrum data is shared between audio and GUI without synchronization)

| Area | Evidence |
| --- | --- |
| Verdict | REAL. The finding describes the code that was on this branch. |
| Bug as found | The audio thread wrote the analyser state in `EqAnalyser::analyze()` (`plugins/Eq/EqSpectrumView.cpp:84-144` before the fix: `m_inProgress`, `m_bands`, `m_energy`, `m_sampleRate`), and the GUI read the same plain members: `EqSpectrumView::paintEvent` read `m_analyser->m_bands` directly (`plugins/Eq/EqSpectrumView.cpp:311` before the fix) and `getEnergy()` / `getInProgress()` / `getSampleRate()` beside it. `analyze()` runs on the audio thread (`plugins/Eq/EqEffect.cpp:172` and `plugins/Eq/EqEffect.cpp:282`), `paintEvent` on the GUI thread: a data race on the band array and on the flags. The progress flag is a plain `bool` and does not make the array reads safe - the reader can be halfway through the 2048 band array while the writer replaces it. `setActive()` is the mirror image: the GUI writes `m_active` while the audio thread reads it. |
| Production fix | `plugins/Eq/EqSpectrumView.cpp:154-207` - the analyser **publishes** the finished result: `publish()` writes the bands into atomic members behind a sequence counter, `copySpectrum()` hands the reader a whole snapshot (the bands, the energy they were measured with) and never a half written one: while a publication is in flight the reader is served the last whole spectrum. The analyser state the GUI reads became atomic (`m_energy`, `m_sampleRate`, `m_active`, `m_inProgress`). |
| Header | `plugins/Eq/EqSpectrumView.h:41-100` declares the published spectrum (`std::atomic<float> m_publishedBands[MAX_BANDS]`, `std::atomic<float> m_publishedEnergy`, `std::atomic<unsigned int> m_publishSequence`), the reader API `copySpectrum()` and the test seam `setPublishHook()`. |
| The reader | `plugins/Eq/EqSpectrumView.cpp:291-320` - `EqSpectrumView::paintEvent()` copies the published snapshot into its own `m_spectrumBands` (allocated once in the constructor) and paints that; it never touches the array the audio thread writes. The paint path is otherwise unchanged: same early return, same cached path while an analysis is in progress, same peak decay. |
| Other callers | `m_bands` is read by `plugins/Eq/EqEffect.cpp:310` (`linearPeakBand`) - that runs on the audio thread right after `analyze()` wrote it, so it reads its own result and stays correct. The frozen reference copy in `tests/reference/Eq/` is untouched. The peak values `EqControls::m_lowShelfPeakL` and friends are a different object (they are written by the audio thread and drawn by the GUI, but they are not the analyser's spectrum); they are outside this finding and are unchanged. |
| Realtime | The audio thread gained no allocation, no lock, no wait and no logging: `publish()` is 2048 atomic stores plus two atomic increments, `clear()` publishes the emptied spectrum the same way. |
| Regression | `tests/src/core/AudioAnalysisTest.cpp` - `theSpectrumIsPublishedAsAWholeSnapshot()` analyses one window, reads the published snapshot, then lets the analyser publish the next spectrum **while a reader takes a copy of it** (through the `setPublishHook()` test seam, so the interleaving is deterministic - no threads, no sleeps). The reader must be served a whole spectrum: the copy must be refused as "not current" and must equal the previous spectrum, never a mixture of the two. The helper `readPublishedSpectrum()` compiles against the unfixed analyser as well, so without the fix the test **fails at runtime** with "the analyser publishes no snapshot, so the GUI reads the band array the audio thread writes" instead of failing to build. |
| Registry | `tests/upstream-modifications.txt` records the two inherited production files with the reason. |
| Test count | The affected family is `AudioAnalysisTest` (1 registered CTest test, 10 QtTest cases now), plus the sibling Eq tests `EqFilterTest` and `EqCurveTest`: 3 registered CTest tests, all passing. The requested `EqSpectrumView` CTest name is not registered in this configured tree (`ctest -N -R '^EqSpectrumView$'` lists 0 tests); the regression runs in the existing audio-analysis test, which compiles the production `plugins/Eq/EqSpectrumView.cpp` into it. |
| Commit files | `plugins/Eq/EqSpectrumView.cpp`, `plugins/Eq/EqSpectrumView.h`, `tests/src/core/AudioAnalysisTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p14-99.md`. |

### Exact verification commands and results (new-p9-123)

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-99.r1/logs`.

| Purpose | Exact command | Exit |
| --- | --- | ---: |
| Build with the fix applied | `cmake --build build --target zene eq AudioAnalysisTest > .../logs/build-07.log 2>&1` | 0 |
| Build the regression against the fixed product code | `cmake --build build --target AudioAnalysisTest > .../logs/build-08.log 2>&1` | 0 |
| **GREEN** — regression with the fix applied | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > .../logs/test-07.log 2>&1` | 0 (1 test passed) |
| Take the product fix out (step 4b) | `git diff -- plugins/Eq/EqSpectrumView.cpp plugins/Eq/EqSpectrumView.h > .../logs/fix-new-p9-123.patch && git apply -R .../logs/fix-new-p9-123.patch` | 0 |
| Rebuild with the fix reversed | `cmake --build build --target zene eq AudioAnalysisTest > .../logs/build-09.log 2>&1` | 2 — the test target did not build: `EqAnalyser has no member named setPublishHook` |
| Rebuild after the test was made to compile against the unfixed analyser as well | `cmake --build build --target AudioAnalysisTest > .../logs/build-10.log 2>&1` | 0 |
| **RED** — regression with the fix reversed | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > .../logs/test-08.log 2>&1` | **8** — `FAIL! theSpectrumIsPublishedAsAWholeSnapshot() 'readPublishedSpectrum(analyser, bands, energy, &current)' returned FALSE. (the analyser publishes no snapshot, so the GUI reads the band array the audio thread writes)` |
| Restore the product fix | `git apply .../logs/fix-new-p9-123.patch` | 0 |
| Rebuild with the fix restored | `cmake --build build --target zene eq AudioAnalysisTest > .../logs/build-11.log 2>&1` | 0 |
| **GREEN** — same regression after restoration | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > .../logs/test-09.log 2>&1` | 0 (1 test passed) |
| Build the sibling Eq test targets | `cmake --build build --target EqFilterTest EqCurveTest > .../logs/build-12.log 2>&1` | 0 |
| Whole Eq family with both fixes in place | `cd build/tests && ctest -R '^(EqFilterTest\|EqCurveTest\|AudioAnalysisTest)$' --output-on-failure > .../logs/test-10.log 2>&1` | 0 (3 of 3 tests passed) |
| Plugin level tests | `cmake --build build --target AudioPluginTest PluginControlDialogsTest > .../logs/build-13.log 2>&1` then `cd build/tests && ctest -R '^(AudioPluginTest\|PluginControlDialogsTest)$' --output-on-failure > .../logs/test-11.log 2>&1` | build 0 / test 8 — `AudioPluginTest` failed on two cases that need the plugin modules `nam_harness` and `gate` built into `build/plugins` (`the built Gate module was not found`); they are missing build artifacts of this tree, not a consequence of the fix |
| The same plugin test after building the missing modules | `cmake --build build --target nam_harness gate > .../logs/build-14.log 2>&1` / `.../logs/build-15.log 2>&1` then `cd build/tests && ctest -R '^AudioPluginTest$' --output-on-failure > .../logs/test-12.log 2>&1` | 0 (1 test passed) |

The RED run is a runtime failure, not a build error: the test helper `readPublishedSpectrum()` is written so that it compiles against the unfixed analyser (where the GUI reads the band array in place) and against the fixed one (where the analyser publishes a snapshot), and the assertion fails on the unfixed analyser.

## Self-check

| Question | Answer |
| --- | --- |
| Are both verdicts proven by code, not by assumption? | Yes: `new-p14-99` is the `m_buffer[m_framesFilledUp++]` fill with the counter reset at `plugins/Eq/EqSpectrumView.cpp:126` (before the fix) that discards the samples past the window; `new-p9-123` is the direct read of `m_analyser->m_bands` in `EqSpectrumView::paintEvent` (before the fix) against the audio thread's write in `analyze()`. |
| Do the regressions fail without the fix and pass with it? | Yes, both proved by the step 4b cycle above (RED exit 8 with the product fix reversed, GREEN exit 0 with it restored). The first RED was a plain assertion (5 analyses instead of 6), the second one a plain assertion too ("the analyser publishes no snapshot"). |
| Is the test deterministic (no sleeps, no wall clock, no racing threads)? | Yes: the first test counts the analyses of a fixed block sequence; the second test drives the interleaving through the `setPublishHook()` seam, so the reader takes its copy in the middle of a publication deterministically, on one thread. |
| Did the fix change anything outside the bug? | The analysed window content is unchanged for a block that does not cross the boundary (the same merged samples reach the same FFT window, and the upper half of the FFT input stays zero as before); the spectrum the GUI paints is the same spectrum, only read as a whole snapshot; the audio output of the EQ plugin is untouched (the analyser is display only - the peak values it feeds `EqControls` are drawn by the GUI and never reach the audio path). |
| Did the audio thread gain an allocation, a lock, a wait or a log? | No: the new publication is atomic stores and two atomic increments; the snapshot copy happens on the GUI thread into a buffer allocated in the constructor. |
| Are undo and the command surface untouched? | Yes: no control command was added or changed, and the fix touches no undoable state. |
| Are the gates green? | Yes: `feature_check.py` reports PASS on all eight rows (see `.../logs/gate-01.log` and `.../logs/gate-02.log`). |
