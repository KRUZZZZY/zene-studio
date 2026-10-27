Verdict: FIXED

new-p9-163 (BUG 16): Graph range changes pass reversed bounds to `std::clamp`

Evidence: `graphModel::setRange(1, -1)` swapped nothing before this fix. It stored `m_minValue = 1`, `m_maxValue = -1` and then trimmed every sample with `std::clamp(sample, ymin, ymax)` ([Graph.cpp:497-501](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-163/src/gui/widgets/Graph.cpp:497), original). The `std::clamp` precondition (`lo <= hi`) was violated. The constructor had the same hole: it stored `ymin`/`ymax` unchecked at [Graph.cpp:482-484](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-163/src/gui/widgets/Graph.cpp:482), and the same reversed range then reached `drawSampleAt`'s `std::clamp(val, minValue(), maxValue())` ([Graph.cpp:765](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-163/src/gui/widgets/Graph.cpp:765)) and the `qBound` in the normalise path ([Graph.cpp:700](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-163/src/gui/widgets/Graph.cpp:700)).

Decision: reorder, do not reject. A reversed pair names the same interval, so the model stores it as `[min, max]`. Rejecting would silently keep an old range the caller did not ask for. No current caller passes reversed bounds: `Graph.cpp:50` uses `(-1, 1)`, `plugins/FreeBoy/FreeBoy.cpp:327` uses `(0, 15)`, and `plugins/DynamicsProcessor/DynamicsProcessorControls.cpp:50` and `plugins/WaveShaper/WaveShaperControls.cpp:48` use `(0, 1)`. Ordered inputs behave exactly as before.

The fix (the cause, at the model's range invariant):
- constructor: `m_minValue(std::min(ymin, ymax))`, `m_maxValue(std::max(ymin, ymax))` ([Graph.cpp:482-484](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-163/src/gui/widgets/Graph.cpp:482)). The step clamp already used `abs(ymax - ymin)`, which is order-independent.
- `setRange`: `if (ymin > ymax) { std::swap(ymin, ymax); }` runs before the no-op check, so `setRange(1, -1)` on `[-1, 1]` emits nothing ([Graph.cpp:491-492](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-163/src/gui/widgets/Graph.cpp:491)).

Regression tests (all in `tests/src/gui/EnvelopeGraphTest.cpp`, the registered family for this file):
- `setRangeStoresReversedBoundsInOrder` (line 115): `setRange(1, -1)` yields `minValue() == -1`, `maxValue() == 1`.
- `setRangeTrimsSamplesIntoOrderedRange` (line 123): samples `{-2, -0.5, 0.5, 2}` in `[-2, 2]`, then `setRange(1, -1)` gives `{-1, -0.5, 0.5, 1}`.
- `constructorStoresReversedBoundsInOrder` (line 136): `graphModel(1, -1, ...)` stores `[-1, 1]`.
- `setRangeEmitsOnlyForAChangedInterval` (line 143): the same interval reversed emits no `rangeChanged`; a real change emits once.
- `setRangeWithOrderedBoundsTrimsSamples` (line 154): control, ordered bounds `(-0.5, 0.5)`; passes before and after the fix.

Test matrix (written before the edit): normal ordered path (control); reversed setRange (fixed); reversed constructor (fixed); reconfiguration: repeated/same-interval set (no signal); trim correctness on narrowing. Audio-thread constraints: none. `setRange`, the constructor and `drawSampleAt` run on the GUI/model side, not the audio thread; the change adds no allocation, locking or logging (`std::swap` and `std::min/max` only). Undo: `graphModel` is not a command path; no change. Saved data: `WaveformData.cpp` loads samples only and never reads the range, so no project format changes. Platform: no platform-specific code.

Note on the test: my first trimming test had samples set outside the initial range `[-1, 1]` (`setSamples` does not clamp). After the swap, `setRange(1, -1)` equals the current range and the existing no-op early return skipped the trim. That early return is pre-existing and out of scope, so I changed the test's setup, not its expected values: the samples now start inside a wider `[-2, 2]` range. The first GREEN run's failure is logged in `test-2-green.log`.

| Evidence | Command (run from the worktree or `build/tests`, logs under `runs/new-p9-163.r1/logs/`) | Result |
| --- | --- | --- |
| First target build, before the fix | `cmake --build . --target EnvelopeGraphTest -j4 > build-1.log 2>&1` | EXIT=2; ccache could not write its temp file under the `dsh` path (`build-1.log:18`). Environment issue, no code error. |
| Same build with ccache disabled | `CCACHE_DISABLE=1 cmake --build . --target EnvelopeGraphTest -j4 > build-2.log 2>&1` | EXIT=0 |
| RED, first test version, unfixed code | `ctest -R '^EnvelopeGraphTest$' --output-on-failure > test-1-red.log 2>&1` (from `build/tests`) | EXIT=8; 9 passed, 4 failed (`test-1-red.log:14-29`) |
| GREEN attempt 1, first test version, fixed code | `ctest -R '^EnvelopeGraphTest$' ... > test-2-green.log` | EXIT=8; the trim test's precondition issue (see note above), 12 passed, 1 failed (`test-2-green.log:15-18`) |
| Test corrected; rebuild with fix | `CCACHE_DISABLE=1 cmake --build . --target EnvelopeGraphTest -j4 > build-4.log` | EXIT=0 |
| GREEN, corrected test, fixed code | `ctest -R '^EnvelopeGraphTest$' ... > test-3-green.log`; binary `./EnvelopeGraphTest > binary-green.log` | ctest EXIT=0 (1/1 passed); binary EXIT=0, 13 PASS, 0 FAIL (11 test functions + init/cleanup) |
| RED proof, step 4b, fix reverted | `git diff -- src/gui/widgets/Graph.cpp > fix.patch && git apply -R fix.patch` (DIFF_EXIT=0, REVERSE_EXIT=0); `CCACHE_DISABLE=1 cmake --build . --target EnvelopeGraphTest -j4 > build-5-unfixed.log` | BUILD EXIT=0 |
| RED, corrected test, unfixed code | `./EnvelopeGraphTest > binary-red-unfixed.log`; `ctest -R '^EnvelopeGraphTest$' ... > test-4-red-unfixed.log` | binary EXIT=4, 9 passed, 4 failed; ctest EXIT=8. The four failures are `setRangeStoresReversedBoundsInOrder`, `setRangeTrimsSamplesIntoOrderedRange`, `constructorStoresReversedBoundsInOrder`, `setRangeEmitsOnlyForAChangedInterval` (`binary-red-unfixed.log`). |
| Restore fix | `git apply fix.patch` (APPLY_EXIT=0); `CCACHE_DISABLE=1 cmake --build . --target EnvelopeGraphTest -j4 > build-6-refixed.log` | BUILD EXIT=0 |
| GREEN after restore (final) | `./EnvelopeGraphTest > binary-green-final.log`; `ctest -R '^EnvelopeGraphTest$' ... > test-5-green-final.log` | binary EXIT=0, 13 passed; ctest EXIT=0 |
| Affected family build (zene + 5 related targets) | `CCACHE_DISABLE=1 cmake --build . --target zene GraphWaveformEditorGuiTest WaveformDataTest SampleEditorDialogTest ExprSynthTest ExportQualityControlsTest EnvelopeGraphTest -j4 > build-7-family.log` | EXIT=0; 0 `error` lines |
| Affected family tests | `ctest -R '^(GraphWaveformEditorGuiTest\|WaveformDataTest\|SampleEditorDialogTest\|ExprSynthTest\|ExportQualityControlsTest\|EnvelopeGraphTest)$' --output-on-failure > test-6-family.log` | EXIT=0; 6/6 passed. `MultitapEchoControlDialog` is not a ctest target (`ctest -N`, the `-R` filter returns 6 tests, none named for it), so it was not run. |
| Feature check (the script itself) | `python3 .../qwen/feature_check.py <worktree> > gate-feature-check-1.log 2>&1` | EXIT=1 before any gate ran: `PermissionError` creating the sibling `zene-qwen-new-p9-163-feature-check` outside the write-jail (`gate-feature-check-1.log`). Not a gate result. |
| Gates, run one by one (the same eight commands the script lists) | see `gate-manual-summary-2.log` | fork-sources 0; all-sources-reproduce 0; no-upstream-regression 0; check-namespace 0; no-tautology 0; doc-figures 0; complexity 1; file-length 1 |
| Complexity / file-length regressions in my files | `grep -h REGRESSION gate-manual-complexity.log gate-manual-file-length.log \| grep 'Graph.cpp\|EnvelopeGraphTest'` | no match (exit 1). The 28 REGRESSION lines name other files (e.g. `XyPadPairTest.cpp`, `RoutingGraph.cpp`, `ControlCommandsMixerStrip.cpp`). None is new in a file this cluster changed. `Graph.cpp` is 774 lines now and was 771 at HEAD (`git show HEAD:src/gui/widgets/Graph.cpp \| wc -l`); the file-length gate fails only on items new in a changed file, and this change adds none. |
| Re-run after the registration entry, final tree | `bash tests/no-upstream-regression-gate.sh > gate-final-no-upstream-regression.log`; `bash tests/fork-sources-gate.sh > gate-final-fork-sources.log` | both EXIT=0 |

Registration: `src/gui/widgets/Graph.cpp` is inherited, so `tests/upstream-modifications.txt` gets one line for `cluster new-p9-163`. `tests/src/gui/EnvelopeGraphTest.cpp` was already registered and is an edit to an existing test, so no new registration. No new files and no new control commands; the `ControlCommandsSnapshot` canary is not affected.

Files committed for this verdict: `src/gui/widgets/Graph.cpp`, `tests/src/gui/EnvelopeGraphTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p9-163.md`.

## Self-check

- Feature check: the script could not run in this write-jail (its log directory is a sibling of the worktree). The eight gate commands it lists were run one by one with logs in the LOG FOLDER. Six pass. `complexity` and `file-length` exit 1 only with REGRESSION items in files this cluster did not change.
- RED/GREEN: RED is shown for the committed test file against unfixed `Graph.cpp` (`binary-red-unfixed.log`, 4 failures). GREEN is shown for the same test file with the fix (`binary-green-final.log`, 13 passed).
- Determinism: no clock, sleep, or race; the tests are pure model calls and one `QSignalSpy` count.
- Callers: all graphModel constructor callers were checked (see the decision above); `setRange` on `graphModel` has no callers in `src/` (the other `setRange` calls are on other types).
- Known pre-existing items left alone: `setRange` returns early when the range is unchanged even though the samples may lie outside it (only reachable through `setSamples`, which does not clamp). This is outside BUG 16 and was not changed.
