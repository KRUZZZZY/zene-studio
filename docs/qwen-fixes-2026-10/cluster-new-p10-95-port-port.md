# cluster new-p10-95-port-port

**Verdict: FIXED**

Round 2, on top of this branch's commits `ab888d1ff` (test + seam) and `b975b30b0` (rounding fix): Luna's r1 review
(REWORK) is addressed - the sum-and-increment rounding introduced in `b975b30b0` overflowed signed `int` for large
valid readings; it is replaced with overflow-safe `std::midpoint` arithmetic, and two large-reading boundary
regressions were added, rebuilt and re-proven RED/GREEN.

## The finding, and why it is REAL

Finding: "CPU load display can stay at zero for a steady one-percent load" (`src/gui/widgets/CPULoadWidget.cpp:96`
in the historical tree).

Before the fix, `CPULoadWidget::updateCpuLoad()` computed the smoothed readout with plain integer division:

```cpp
const int new_load = (m_currentLoad + profilerLoad()) / 2;   // src/gui/widgets/CPULoadWidget.cpp:110 (round 0)
```

`(0 + 1) / 2 == 0`, and the guard below (`if (new_load != m_currentLoad)`) then does nothing: from a displayed 0%
a steady 1% profiler reading never moves `m_currentLoad`, so neither the LED bar nor the tooltip's `DSP total` ever
updates. REAL, exactly as filed. The original regression proved it (r1: ctest exit 8 with the rounding change
reversed, exit 0 restored).

## Luna's r1 defect and the round-2 fix

Round 1 fixed the truncation with:

```cpp
const int new_load = target > m_currentLoad
	? (m_currentLoad + target + 1) / 2     // overflows signed int for large readings
	: (m_currentLoad + target) / 2;        // (current + target) overflows too once both pass INT_MAX/2
```

`AudioEngine::cpuLoad()` returns an `int` with **no 100% ceiling** (`include/AudioEngineProfiler.h:51-53`,
`src/core/AudioEngineProfiler.cpp:48-54`: `100.f * periodElapsed / timeLimit` - an uncapped ratio), so valid
readings can sit near 2^30 and the `+ 1` upward expression overflows for current `1073741823` + target
`1073741824` (their sum is exactly `INT_MAX`; `+1` wraps). The downward `(current + target) / 2` has the same
class of defect once the two readings sum past `INT_MAX`.

The fix at cause, `src/gui/widgets/CPULoadWidget.cpp:110-118` (with `#include <numeric>`):

```cpp
const int target = profilerLoad();
const int new_load = std::midpoint(target, m_currentLoad);
```

`std::midpoint(a, b)` rounds ties toward its first argument (verified with a local `g++ -std=c++20` probe:
`midpoint(1,0)==1`, `midpoint(0,1)==0`, `midpoint(2,1)==2`, `midpoint(1,2)==1`), so passing the new reading as
the first argument keeps exactly the intended "rounds toward the target" semantics in both directions: from 0% a
steady 1% reaches 1%, from 1% a reading of 0% comes back. For every non-negative pair it is algebraically equal
to the round-1 expression (`ceil((c+t)/2)` on a rise, `floor((c+t)/2)` on a fall) - so behaviour outside the
overflow region is bit-identical - while the standard mandates the midpoint be computed without signed overflow,
which fixes both boundary directions.

## Siblings and callers checked (consistent)

* `updateCpuLoad()` is called only by `m_updateTimer` (same file, 100 ms GUI timer) and, in tests, directly.
* `m_currentLoad` feeds `paintEvent`'s clipping (`std::min(m_currentLoad, 100)`, `src/gui/widgets/CPULoadWidget.cpp:81`)
  and the tooltip - both unchanged and safe for large values (painting stays capped at 100).
* Raw `cpuLoad()` consumers do not average, so they never had the rounding bug and are unchanged:
  `src/core/AudioEngine.cpp:575` (`criticalXRuns` overload detection, a `>= 99` comparison),
  `src/gui/shell/TransportReadouts.cpp:419` (raw percent readout), `src/core/ControlCommandsTrackLooks.cpp:198`
  (cast to `double`).
* The profiler's per-detail values (`detailLoad`) use their own averaging inside the profiler and are only
  printed - untouched.

## The regression test

`tests/src/gui/CPULoadWidgetTest.cpp` (registered in `tests/CMakeLists.txt`, `tests/all-sources.txt`,
`tests/fork-sources.txt` from the round-1 commit; registration untouched this round - only slots were added).
5 test slots, all driven through the `LMMS_TESTING` seam `CPULoadWidget::setCpuLoadForTesting(int)`
(`include/CPULoadWidget.h:52`) with injected readings and direct `QMetaObject::invokeMethod(...,
Qt::DirectConnection)` calls:

1. `aSteadyOnePercentReadingReachesTheReadout` - the finding (0% + steady 1% -> 1% and holds).
2. `everyRiseOfOnePercentAdvancesTheReadout` - 1% -> 2% keeps advancing.
3. `aReadingThatFallsBackToZeroClearsTheReadout` - 1% -> 0% comes back.
4. `largeReadingsRoundUpWithoutOverflow` (new) - Luna's exact boundary: hold the readout at 1,073,741,823, then
   inject 1,073,741,824; the readout must become 1,073,741,824. The old expression wraps to
   `-1073741824` (observed in the RED log).
5. `largeReadingsRoundDownWithoutOverflow` (new) - hold 1,500,000,000, inject 1,000,000,000; the midpoint is
   1,250,000,000 and the readout must land there. Under the old expression the convergence and the fall both
   wrap (observed: stuck at `886433244%` in the RED log).

No wall-clock timing, no sleeps, no races: readings are injected through the seam, updates are synchronous direct
invocations, the audio device is stopped (`initTestCase`), no event loop runs the widget's timer, and the
"reach the large value" loops are bounded (<= 64 synchronous updates of an integer halving loop).

## Evidence (commands and exit codes as run)

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-95-port-port.r2/logs/`.

| # | command (from the worktree unless noted) | exit | result |
|---|---|---|---|
| 1 | `cmake --build build --target zene CPULoadWidgetTest -j12` (round-1 product code + the new boundary tests) | 0 | build-1-red.log |
| 2 | `cd build/tests && ctest -R '^CPULoadWidgetTest$' --output-on-failure` | **8** | **RED**: `Totals: 5 passed, 2 failed`; `largeReadingsRoundUpWithoutOverflow` saw `DSP total: -1073741824%` (overflow wrap), `largeReadingsRoundDownWithoutOverflow` stuck at `DSP total: 886433244%` (test-1-red.log) |
| 3 | fix applied (`std::midpoint(target, m_currentLoad)` + `<numeric>`); `cmake --build build --target zene CPULoadWidgetTest -j12` | 0 | build-2-green.log |
| 4 | `cd build/tests && ctest -R '^CPULoadWidgetTest$' --output-on-failure` | **0** | **GREEN**: `100% tests passed out of 1` (test-2-green.log) |
| 5 | `git diff -- src/gui/widgets/CPULoadWidget.cpp > logs/fix.patch` | 0 | product-only patch (the averaging change, no test hunks) |
| 6 | `git apply -R logs/fix.patch` | 0 | fix reverted (round-1 sum-and-increment restored) |
| 7 | `cmake --build build --target zene CPULoadWidgetTest -j12` | 0 | build-3-reversed.log |
| 8 | `cd build/tests && ctest -R '^CPULoadWidgetTest$' --output-on-failure` | **8** | **RED proof**: `Totals: 5 passed, 2 failed`, same two boundary assertions (test-3-reversed.log) |
| 9 | `git apply logs/fix.patch` | 0 | fix restored |
| 10 | `cmake --build build --target zene CPULoadWidgetTest -j12` | 0 | build-4-restored.log |
| 11 | `cd build/tests && ctest -R '^CPULoadWidgetTest$' --output-on-failure` | **0** | **GREEN again**: `100% tests passed out of 1` (test-4-restored.log) |
| 12 | `cd build/tests && ctest -R "CPULoadWidgetTest\|WorkflowFeedbackTest\|TaskIndicatorTest\|ItemListPanelTest\|CanvasKeyboardTest" --output-on-failure` | **0** | affected family: `100% tests passed out of 5` (test-5-family.log) |
| 13 | `cd build/tests && ./CPULoadWidgetTest` | **0** | slot detail: `Totals: 7 passed, 0 failed` (5 test slots + init/cleanup) (test-6-slots.log) |
| 14 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | all rows PASS (gate-1-feature-check.log) |
| 15 | `bash tests/complexity-gate.sh --check` | **1** | pre-existing branch findings only; `rg CPULoadWidget` over the log finds nothing (exit 1) - none of this change's files are listed (gate-2-complexity.log) |
| 16 | `bash tests/file-length-gate.sh --check` | **1** | pre-existing branch findings only (`TakeLane.cpp`, `CollabTransportTest.cpp`, ...); no changed file listed (gate-3-file-length.log) |
| 17 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (after the final edit, tests/upstream-modifications.txt) | **0** | all rows PASS (gate-4-feature-check-final.log) |

`feature_check.py` rows (commands 14 and 17): fork-sources PASS, all-sources-reproduce PASS,
no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS
("no new item in your files"), file-length PASS ("no new item in your files"). `RESULT: PASS`.

Fix proof, step 4b: `fix.patch` was reverse-applied (command 6) and the test failed (command 8, exit 8, two
assertions - both boundary slots; the three original slots still pass, as round-1 code satisfies them), then
re-applied (command 9) and everything passed (commands 11 and 13, exit 0). The patch contains only the product
averaging change, so the RED state is a real assertion failure, not a build failure.

Raw ratchet note (as Luna requested): `tests/complexity-gate.sh --check` and `tests/file-length-gate.sh --check`
both exit **1** on this branch, on findings that predate this change and are in files this change does not touch
(commands 15-16; `rg -n CPULoadWidget` over both logs matches nothing). The batch gate `feature_check.py`, which
scopes these ratchets to the files changed, exits **0**. No gate limit or baseline was modified.

## Files committed (this round)

* `src/gui/widgets/CPULoadWidget.cpp` - the fix: `std::midpoint(target, m_currentLoad)` + `#include <numeric>`
* `tests/src/gui/CPULoadWidgetTest.cpp` - two large-reading boundary regression slots
* `tests/upstream-modifications.txt` - reason line for the inherited cpp updated to describe the overflow-safe midpoint
* `docs/qwen-fixes-2026-10/cluster-new-p10-95-port-port.md` - this report

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Yes: reversed
  with `git apply -R logs/fix.patch` (exit 0) + rebuild (exit 0), `ctest -R '^CPULoadWidgetTest$'` exit **8**
  (`Totals: 5 passed, 2 failed` - both boundary slots; test-3-reversed.log). Restored with
  `git apply logs/fix.patch` (exit 0) + rebuild (exit 0), ctest exit **0** (`100% tests passed out of 1`;
  test-4-restored.log). The identical test also failed on the untouched round-1 code before any fix this round
  (step 2: exit 8) and passes after (step 4: exit 0).
* **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).**
  Yes: everything goes through `CPULoadWidget::setCpuLoadForTesting(int)` (`include/CPULoadWidget.h:52`) with the
  fixed values 0, 1, 2, 1073741823, 1073741824, 1500000000, 1000000000; updates are synchronous
  `QMetaObject::invokeMethod(..., Qt::DirectConnection)` calls to the real `updateCpuLoad` slot; the only loop is
  a bounded (<= 64 iterations) deterministic integer convergence loop asserting the tooltip each step; no event
  loop, no timer, no sleep, no audio period; the audio device is stopped in `initTestCase`.
* **The fix is at the cause and complete: every caller and sibling of what you change was checked and stays
  consistent.** Callers of `updateCpuLoad`: only `m_updateTimer` (100 ms, this file) and the tests. Consumers of
  `m_currentLoad`: `paintEvent` (already clipped with `std::min(m_currentLoad, 100)`, unchanged) and the
  tooltip. Siblings reading `cpuLoad()`: `AudioEngine.cpp:575` (comparison), `TransportReadouts.cpp:419` (raw
  display), `ControlCommandsTrackLooks.cpp:198` (double cast) - none average, none changed. The only averaging
  site for this value is `updateCpuLoad`, and it is the site that was fixed. Header untouched this round
  (`include/CPULoadWidget.h` was already consistent from round 1), so no extra target rebuild was needed beyond
  the `zene` + `CPULoadWidgetTest` rebuilds shown above.
* **No collateral damage: old projects and saved data still load as before, and any limit or check you added does
  not hit other callers that legitimately exceed it.** The change is display-only rounding inside one GUI slot:
  no save/load path, no file format, no limit, no check, no API. For all non-negative in-range pairs the
  `std::midpoint` result equals the previous expression bit-for-bit, so behaviour outside the overflow region is
  unchanged; only the previously-overflowing (undefined/wrapped) region now produces the correct rounded value.
  No new numbers are capped: `cpuLoad()`'s uncapped contract is untouched (a limit would have hit
  `criticalXRuns()`' `>= 99` overload detection and the raw readouts - none were added).
* **Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal.**
  `updateCpuLoad` runs on the GUI timer thread (QTimer, same thread as before); the fix changes one arithmetic
  expression - `std::midpoint` is a constexpr free function with no allocation, no locking and no I/O. The calls
  it reaches (`setToolTip`, `update()`) already existed on that path unchanged. The audio-thread side
  (`AudioEngineProfiler::finishPeriod`) was not touched.
* **Each edit is ONE undo step, and no new command has an honest reversibility class issue.** This is a bug fix
  with no command, no control path and no undo/redo surface: nothing was added to the ControlRegistry, so the
  ControlCommandsSnapshot canary is untouched (no `ControlCommands*` file is in the diff).
* **Qt 5.15 / MSVC / macOS portability.** `std::midpoint` is C++20 standard library (`<numeric>`), available in
  all three toolchains the project targets; the rest of the file's Qt API is unchanged (no Qt-6-only call).
* **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: ...** Not applicable - the verdict is FIXED; the defect
  was REAL and its evidence is the RED run above (`DSP total: -1073741824%` for the exact reading pair Luna
  named, on the un-fixed code).
