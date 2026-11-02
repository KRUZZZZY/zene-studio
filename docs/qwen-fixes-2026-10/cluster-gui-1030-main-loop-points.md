FIXED

# cluster gui-1030-main-loop-points - "Loop points misses accessible button activation"

## Verdict: REAL, fixed

`NStateButton` advanced its state only in `mousePressEvent` (the old `src/gui/widgets/NStateButton.cpp:85-92`).
`QAbstractButton::click()` - the accessible button action, and the keyboard's Space via `keyReleaseEvent` - emits
`clicked()` without any mouse press, so the state, the connected `Timeline::setLoopEnabled` and the ruler redraw never
ran. The Loop points button is wired at `src/gui/editors/TimeLineWidget.cpp:108-115`
(`changedState` -> `Timeline::setLoopEnabled`). The auto-scroll and stop-behaviour buttons (same file, lines 99 and 117)
are the same class and had the same defect, so they are fixed by the same change (siblings checked: those three are the
only `NStateButton` users, `rg NStateButton src include`).

## Fix

`NStateButton` now overrides `QAbstractButton::nextCheckState()` (virtual, protected) instead of `mousePressEvent`.
Qt calls it from `QAbstractButtonPrivate::click()` for every activation path (mouse release, Space, `click()`, the
accessible action), once per activation, and `QAbstractButton` itself ignores non-left mouse buttons, so the left-button
check is kept by Qt. `src/gui/widgets/NStateButton.cpp` (`nextCheckState`), `include/NStateButton.h`.
One visible difference: a mouse click now advances on release (standard button semantics) instead of on press; a press
dragged off the button and released outside no longer toggles it. Nothing else changes: state order, tooltips,
accessible name, `changedState` signal, project/saved data are untouched. Not on the audio thread; no new commands.

## Test

`tests/src/gui/NStateButtonActivationTest.cpp` (new, registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt`),
driving the real toolbar from `TimeLineWidget::addToolButtons` on a real `Timeline`, no timing: accessible `click()`
enables/disables the loop; Space enables it; a mouse click advances exactly one state; a right click does nothing; a
Timeline-side change still moves the button; the three-state auto-scroll button cycles.
`tests/upstream-modifications.txt` entry for `src/gui/widgets/NStateButton.cpp` extended and one added for
`include/NStateButton.h`.

## Evidence

| step | command (from build/tests unless noted) | exit | result |
|---|---|---|---|
| RED build | `cmake --build build --target NStateButtonActivationTest -j4` (log build-3-red.log) | 0 | built on unfixed code |
| RED run | `ctest -R NStateButtonActivationTest --output-on-failure` (test-2-red.log) | 8 | 4 passed, 4 failed; `anAccessibleActivationEnablesTheLoop`: state 0, expected 1 |
| GREEN build | `cmake --build build --target zene NStateButtonActivationTest AccessibilityWalkTest ArrangerTimelineTest -j4` | 0 | |
| GREEN run | `ctest -R NStateButtonActivationTest --output-on-failure` (test-6-reapplied-green.log) | 0 | 1/1 passed (all 6 cases) |
| family | `ctest -R "ArrangerTakePunchTest\|NStateButtonActivationTest"` (test-4-green.log); `AccessibilityWalkTest`, `ArrangerTimelineTest` (test-3-green.log) | 0 | all pass (4 tests executed in total) |
| 4b revert | `git diff -- src include > logs/fix.patch && git apply -R logs/fix.patch` | 0 | |
| 4b rebuild + run | `cmake --build build --target NStateButtonActivationTest -j4` -> 0; `ctest -R NStateButtonActivationTest` (test-5-reverted-red.log) | 8 | FAILS: 4 failed (click, Space, timeline-change, three-state) |
| 4b reapply | `git apply logs/fix.patch`; rebuild -> 0; ctest | 0 | PASSES |
| gates | `python3 logs/feature_check_redirected.py <worktree>` (gates-2.log) | 0 | 8/8 PASS (see note) |

Files committed: `include/NStateButton.h`, `src/gui/widgets/NStateButton.cpp`, `tests/src/gui/NStateButtonActivationTest.cpp`,
`tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt`, this report.

## Environment notes (not product issues)

- The new test target did not exist in the pre-configured build tree and `cmake` is shimmed against configuring;
  `cmake --build build --target cmake_check_build_system` regenerated it through the normal build path.
- ccache could not write its temp dir (`.../dsh/home/ccache-tmp-r6`, permission denied); I set
  `CCACHE_TEMPDIR` to the log folder for the builds.
- `feature_check.py` creates `<worktree>-feature-check` beside the worktree, outside the write jail
  (PermissionError, `gates-1.log`, exit 1). I ran a copy with only `ROOT` pinned and the log directory redirected into
  the log folder (`feature_check_redirected.py`, diff is those three lines); the gate logic and baselines are the original's.
