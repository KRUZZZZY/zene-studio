# cluster new-p10-95-port-port

**Verdict: FIXED**

Port job: the fix accepted on `bugs/qwen-new-p10-95` (8ba72b48e, registration 2ee60dc95) could not merge into the
newest line because of a conflict in `tests/CMakeLists.txt`. It is re-applied here by hand on top of `050/m0-train-8`
(base 2da2ed356), with a deterministic regression test in place of the accepted branch's timed one.

## The finding, and why it is REAL

Finding: "CPU load display can stay at zero for a steady one-percent load" (`src/gui/widgets/CPULoadWidget.cpp:96`
in the historical tree).

Before the fix, `CPULoadWidget::updateCpuLoad()` computed the smoothed readout with plain integer division:

```cpp
const int new_load = (m_currentLoad + profilerLoad()) / 2;   // src/gui/widgets/CPULoadWidget.cpp:110
```

`(0 + 1) / 2 == 0`, and the guard below (`if (new_load != m_currentLoad)`) then does nothing: from a displayed 0%
a steady 1% profiler reading (the profiler's own float smoothing settles at ~1.4%, which its `int cpuLoad()`
truncates to 1) never moves `m_currentLoad`, so neither the LED bar nor the tooltip's `DSP total` ever updates.
REAL, exactly as filed.

## The fix

`src/gui/widgets/CPULoadWidget.cpp:110-114`: the midpoint rounds toward the new reading - up on a rise, down on a
fall - so any nonzero difference moves the readout by at least one:

```cpp
const int target = profilerLoad();
const int new_load = target > m_currentLoad
	? (m_currentLoad + target + 1) / 2
	: (m_currentLoad + target) / 2;
```

The upward-only round of the original acceptance (fb99c6e88) would have held a falling 1% at 1%; this widened
version, which is what the accepted port commit 8ba72b48e shipped, moves both ways.

Siblings/callers checked and left consistent:

* `updateCpuLoad()` is called only by `m_updateTimer` (same file, 100 ms) and, in tests, directly.
* `m_currentLoad` feeds `paintEvent`'s clipping (`std::min(m_currentLoad, 100)`) and the tooltip - both unchanged.
* Raw `cpuLoad()` consumers do not smooth, so they never had the rounding bug and are unchanged:
  `src/core/AudioEngine.cpp:575` (`criticalXRuns` overload detection), `src/gui/shell/TransportReadouts.cpp:419`
  (raw percent readout), `src/core/ControlCommandsTrackLooks.cpp:198` (`control.cpu_load`'s `engine_percent`).
* The profiler's per-detail values (`detailLoad`) use their own stronger averaging and are only printed; untouched.

## The regression test

New `tests/src/gui/CPULoadWidgetTest.cpp` (registered in `tests/CMakeLists.txt`, `tests/all-sources.txt` and
`tests/fork-sources.txt`). It is deterministic: it drives the real `updateCpuLoad()` slot through the new
`LMMS_TESTING` seam `CPULoadWidget::setCpuLoadForTesting(int)` (`include/CPULoadWidget.h:52`) with injected profiler
readings - no audio period is timed, no sleep, no wall-clock, no race. The seam is compiled out of the shipped build
(`tests/upstream-modifications.txt` declares both inherited files).

It asserts the tooltip's `DSP total` for:

1. a steady 1% reading from a displayed 0% reaches 1% and holds there (the finding);
2. a 1% -> 2% rise keeps advancing (truncation stall above zero);
3. a 1% -> 0% fall comes back to 0% (an upward-only round cannot pass).

## Evidence (commands and exit codes as run)

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-95-port-port.r1/logs/`.

| # | command (from the worktree unless noted) | exit | result |
|---|---|---|---|
| 1 | `cmake --build build --target cmake_check_build_system` (tests/CMakeLists.txt changed -> new target) | 0 | build graph regenerated |
| 2 | `cmake --build build --target CPULoadWidgetTest` (bug present) | 0 | build-1-red.log |
| 3 | `cd build/tests && ctest -R CPULoadWidgetTest --output-on-failure` | **8** | RED: `Totals: 2 passed, 3 failed`; failing assertion `CPULoadWidgetTest.cpp(59)`: tooltip does not contain `DSP total: 1%` (test-1-red.log) |
| 4 | `git diff -- src/gui/widgets/CPULoadWidget.cpp > logs/fix.patch` | 0 | only the rounding hunk |
| 5 | `cmake --build build --target CPULoadWidgetTest` (fix applied) | 0 | build-2-green.log |
| 6 | `cd build/tests && ctest -R CPULoadWidgetTest --output-on-failure` | **0** | GREEN: `100% tests passed out of 1` (test-2-green.log) |
| 7 | `git apply -R logs/fix.patch` | 0 | fix reverted |
| 8 | `cmake --build build --target CPULoadWidgetTest` | 0 | build-4-reverted.log |
| 9 | `cd build/tests && ctest -R CPULoadWidgetTest --output-on-failure` | **8** | RED again: `Totals: 2 passed, 3 failed` (test-4-reverted.log) |
| 10 | `git apply logs/fix.patch` | 0 | fix restored |
| 11 | `cmake --build build --target CPULoadWidgetTest` | 0 | build-5-restored.log |
| 12 | `cd build/tests && ctest -R CPULoadWidgetTest --output-on-failure` | **0** | GREEN again: `100% tests passed out of 1` (test-5-restored.log) |
| 13 | `cmake --build build --target WorkflowFeedbackTest TaskIndicatorTest ItemListPanelTest CanvasKeyboardTest` | 0 | affected GUI family built (build-3-family.log) |
| 14 | `cd build/tests && ctest -R "CPULoadWidgetTest\|WorkflowFeedbackTest\|TaskIndicatorTest\|ItemListPanelTest\|CanvasKeyboardTest" --output-on-failure` | **0** | `100% tests passed out of 5` (test-3-family.log) |
| 15 | `cmake --build build --target zene` (header change rebuilt the product) | 0 | build-6-zene.log |
| 16 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | all rows PASS (gates-1.log) |

`feature_check.py` rows: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS,
check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item), file-length PASS
(no new item). `RESULT: PASS`.

Fix proof, step 4b: with `logs/fix.patch` reverse-applied the test fails (step 9, exit 8); re-applied it passes
(step 12, exit 0). The product hunk it reverts is only the rounding change - the seam stays in both states, so
the RED state is a real assertion failure, not a build failure.

## Files committed

* `src/gui/widgets/CPULoadWidget.cpp` - the fix (line 110-114)
* `include/CPULoadWidget.h` - test seam + `profilerLoad()` declaration (declaration and definition together)
* `tests/src/gui/CPULoadWidgetTest.cpp` - new deterministic regression (fork-NEW)
* `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt` - registration
* `tests/upstream-modifications.txt` - declared divergence for the two inherited files
* `docs/qwen-fixes-2026-10/cluster-new-p10-95-port-port.md` - this report

## Self-check

* The regression test FAILS with the product change reverted and PASSES with it: steps 7-9 (reverse, build OK,
  ctest exit 8, 3 failed) and steps 10-12 (apply, build OK, ctest exit 0, 1/1). Both exit codes above.
* The test is deterministic: it uses the `setCpuLoadForTesting` seam (`include/CPULoadWidget.h:52`) to inject
  profiler readings; no wall-clock timing, sleeps or races; the audio device is stopped
  (`Engine::audioEngine()->audioDev()->stopProcessing()`), and no event loop runs the widget's timer.
* The fix is at the cause and complete: every caller of `updateCpuLoad`, the consumers of `m_currentLoad`
  (`paintEvent` clipping, tooltip) and the raw `cpuLoad()` readers were checked - the raw readers never smooth, so
  they are consistent and unchanged; `detailLoad` averaging is a separate profiler path, untouched.
* No collateral damage: the change is display-only rounding - no saved data, no project/limit/check, no API and no
  command changed. Old projects load and render exactly as before; nothing new is persisted.
* Realtime/undo: `updateCpuLoad` runs on the GUI timer, not the audio thread - no allocation, lock or I/O added;
  no control command and therefore no undo step was added.
* Portability: only Qt 5.15/Qt 6 common API is used; the seam is behind `LMMS_TESTING` and absent from the
  shipped build.
