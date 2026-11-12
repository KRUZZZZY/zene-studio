FIXED

# Cluster gui-1105-main-loop-points: Loop points does not respond to accessibility activation

## Integration re-verification (round 3, 2026-11-09)

State on entry: HEAD `4a3cdd9cd2` sits directly on the pinned target `af010bc3cc` (`git merge-base
--is-ancestor af010bc3cc HEAD`: true). No conflict markers in `include/NStateButton.h`,
`src/gui/widgets/NStateButton.cpp` or `tests/src/gui/NStateButtonActivationTest.cpp`. The train-side
change in the conflicting region, `setAccessibleName(toolTip())` in `changeState`
(`src/gui/widgets/NStateButton.cpp:81`), is kept. No product code changed in round 3: this round
only re-ran the evidence and resolved the gate failure.

Semantic difference from the accepted branch (`bugs/qwen-gui-1105-main-loop-points` @ `5436456d8`,
`git diff 5436456d8 HEAD -- include src tests/src`): `m_advancedOnPress` (set at press, cleared at
release) became `m_suppressReleaseClick`, set only while `ToolButton::mouseReleaseEvent` runs
(`NStateButton.cpp:106-113`, `onClicked` at `:115`). The accepted design is unchanged: mouse
advances at press time, every non-mouse `clicked()` advances once. The one behaviour difference is the
fix for the disabled-during-press hazard, held by the eighth test
`disabledDuringMousePressDoesNotSwallowTheNextClick`.

Failure paths covered (one test each in `NStateButtonActivationTest`): `QAbstractButton::click()`,
the accessibility press action, keyboard Space, a mouse click (advances once, at press), a press
released off the button, the button disabled between press and release, a right click, and the
empty-state button. Platform hazards: Qt API only, same on Linux/macOS/Windows; GUI thread only, no
audio-thread code; no save format, undo or control command touched.

Round-3 evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-loop-points.r3/logs/`):

| Step | Command | EXIT |
| --- | --- | --- |
| Build | `cmake --build build --target zene NStateButtonActivationTest AccessibilityWalkTest RevisionTimelineTest TimelineTest ArrangerTimelineTest -j4` (03) | 0 |
| Family GREEN | `cd build/tests && ctest -R 'NStateButtonActivationTest\|AccessibilityWalkTest\|Timeline\|TimeLine' --output-on-failure` (04) | 0, 5/5 tests passed |
| 4b revert | `git diff af010bc3cc HEAD -- include/NStateButton.h src/gui/widgets/NStateButton.cpp > logs/fix.patch && git apply -R logs/fix.patch`; `cmake --build build --target NStateButtonActivationTest -j4` (05) | 0 / 0 |
| 4b RED | `cd build/tests && ctest -R '^NStateButtonActivationTest$' --output-on-failure` (06) | **8**: `Totals: 5 passed, 5 failed` (programmaticClick, accessiblePressAction, spaceKey, pressReleasedOffTheButton…, disabledDuringMousePress…) |
| 4b reapply | `git apply logs/fix.patch`; `cmake --build build --target zene NStateButtonActivationTest -j4` (07) | 0 / 0 |
| 4b GREEN | `cd build/tests && ctest -R '^NStateButtonActivationTest$' --output-on-failure` (08) | 0, `Totals: 10 passed, 0 failed` (8 cases + init/cleanup) |
| Stock gate | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (01) | 1: `PermissionError` writing `<worktree>-feature-check/fork-sources.log` (outside this lane's write area; no gate ran) |
| Gate, logs redirected | `python3 runs/gui-1105-main-loop-points.r3/fc/fc_logredirect.py <worktree>` (02; per-gate logs in `logs/fc-gate-logs/`) | 0, RESULT: PASS, 8/8 rows; complexity and file-length: no new item in my files |

`fc_logredirect.py` imports the stock `feature_check` module unchanged and wraps only its `run()` so
that per-gate log files go to the log folder. Gate commands, the baseline (cached
`gate-baselines/af010bc3cc….json`) and the pass logic are the stock ones. `submit_check.py` calls the
stock script itself, so its "gates" row stays FAIL for this lane's sandbox, not for any gate.

## Integration onto 050/m0-train-26 (round 2)

The accepted patch (`bugs/qwen-gui-1105-main-loop-points` @ `5436456d8`) was applied to the train tip
`af010bc3cc` by the driver (`integration-apply.log`: every hunk "applied cleanly" on the fallback
direct application; no conflict markers remain in any file). The one line the newer tree changed,
`setAccessibleName(toolTip())` in `NStateButton::changeState` (`NStateButton.cpp:81`), is outside the
patch hunks and is kept. QA retained the accepted activation logic and refined the mouse suppression
to last only while the base mouse-release handler runs. This prevents a release consumed while the
button is disabled from leaving stale state that swallows the next non-mouse activation. An eighth
regression case covers that path. The only integration metadata difference remains the
`tests/upstream-modifications.txt` line, which extends the train's R8.5 reason.

Round-2 evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-loop-points.r2/logs/`):

| Step | Command | EXIT |
| --- | --- | --- |
| Build product + tests | `cmake --build build --target zene NStateButtonActivationTest AccessibilityWalkTest -j4` (01, 03) | 0 (first call: `zene` built, the test target was unknown until the build tree re-read `tests/CMakeLists.txt`; `cmake -S . -B build` is refused by the lane guard, EXIT 97, logs/02; the second `--build` then built both tests) |
| GREEN | `cd build/tests && ctest -R 'NStateButtonActivationTest\|AccessibilityWalkTest' --output-on-failure` (04) | 0, 2/2 |
| 4b revert | `git diff HEAD -- include/NStateButton.h src/gui/widgets/NStateButton.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b RED | `cmake --build build --target NStateButtonActivationTest -j4` (05); `ctest -R '^NStateButtonActivationTest$'` (06) | 0 / **8**: 5 passed, 4 failed (programmaticClick, accessiblePressAction, spaceKey, pressReleasedOff...) |
| 4b reapply | `git apply logs/fix.patch`; `cmake --build build --target zene NStateButtonActivationTest AccessibilityWalkTest -j4` (07) | 0 / 0 |
| Family | `cmake --build build --target RevisionTimelineTest TimelineTest ArrangerTimelineTest -j4` (09); `ctest -R 'NStateButtonActivationTest\|AccessibilityWalkTest\|Timeline\|TimeLine' --output-on-failure` (10) | 0 / 0, 5/5 passed. (A first family run, logs/08, gave EXIT 8 only because those three executables were not built yet: "Not Run".) |
| Gates | stock `feature_check.py <worktree>` (11) | 1: PermissionError creating `<worktree>-feature-check` outside the write area |
| Gates (log dir redirected) | `python3 runs/gui-1105-main-loop-points.r2/fc/feature_check_redirected.py <worktree>` (12) | 0, RESULT: PASS, 8/8 rows (differs from the stock script in 2 lines: `ROOT` pinned and the log directory) |

Supported-platform hazards: `QAbstractButton::click()`, `QAccessibleActionInterface` and `QTest::keyClick`
are Qt API with the same behaviour on Linux, macOS and Windows; the test runs offscreen. The widget is
GUI-thread only, so there is no audio-thread hazard. Not run: the full ctest suite (only the focused
family above) and a live GUI session.

## Verdict: REAL, fixed

Before the fix, `NStateButton` advanced its state only in `mousePressEvent` (historical
`src/gui/widgets/NStateButton.cpp:85-92`), and nothing was connected to `clicked()`.
`QAbstractButton::click()` emits `pressed()`/`released()`/`clicked()` without sending a mouse event.
The accessibility press action calls `click()`, and so do automation and keyboard Space activation,
so none of them changed the state. The timeline's Loop points button is an `NStateButton`
(`src/gui/editors/TimeLineWidget.cpp:108-115`), and `changedState` is what drives
`Timeline::setLoopEnabled` (`TimeLineWidget.cpp:112`). Its auto-scroll and stop-behaviour siblings
in the same toolbar had the same fault. The RED run below confirms this: `click()`, the
QAccessible press action and Space all left `state()` at 0.

## Fix (at the cause, in the shared widget, so all three timeline buttons are covered)

- `src/gui/widgets/NStateButton.cpp:42`: the constructor connects `clicked` to `onClicked()`.
- `src/gui/widgets/NStateButton.cpp:88`: `advanceState()` holds the old "next state, wrap around" step.
- `src/gui/widgets/NStateButton.cpp:97-104`: `mousePressEvent` still advances at press time, as
  before.
- `src/gui/widgets/NStateButton.cpp:107-114`: `mouseReleaseEvent` scopes the suppression flag
  around the base handler, which emits `clicked()` when the release is over the button. If release
  is delivered while the button is disabled, Qt consumes it before this override runs, and no
  stale suppression remains for the next activation.
- `src/gui/widgets/NStateButton.cpp:116-123`: `onClicked()` skips the `clicked()` emitted by the
  base mouse-release handler and advances on every other activation.
- `include/NStateButton.h:64-75`: the matching declarations and the scoped suppression flag
  (default `false`).

Behaviour kept: a left mouse click advances exactly once, at press time. A right click never
advances, because `QAbstractButton` ignores non-left presses and never emits `clicked()`. A button
with no states ignores activation. No saved data, undo, control command or audio-thread code is
involved; this is a GUI-thread widget only. The header is included only by
`src/gui/editors/TimeLineWidget.cpp` and `NStateButton.cpp` (both in `zene`, rebuilt) and the new test.

## Regression test

`tests/src/gui/NStateButtonActivationTest.cpp` (registered in `tests/CMakeLists.txt` next to
CPULoadWidgetTest, offscreen environment, and in `tests/fork-sources.txt`) covers:

- `click()`, including wrap-around
- the QAccessible press action
- the Space key
- a mouse click advancing exactly once
- a press released off the button not swallowing the next `click()`
- a release consumed while disabled not swallowing the next `click()`
- a right click not advancing
- an empty button ignoring activation

Deterministic: no timers, sleeps or threads.

## Commands and results

Logs are in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-loop-points.r1/logs/`.
`CCACHE_TEMPDIR` was pointed at the run folder because the shared ccache temp directory is not
writable from this lane.

| Step | Command | EXIT |
| --- | --- | --- |
| Build test, unfixed | `cmake --build build --target NStateButtonActivationTest -j4 > logs/06-build-red.log` | 0 |
| RED | `cd build/tests && ctest -R NStateButtonActivationTest --output-on-failure > logs/07-test-red.log` | 8 (5 passed, 4 failed: programmaticClick, accessiblePressAction, spaceKey, pressReleasedOff...) |
| Build fixed | `cmake --build build --target zene NStateButtonActivationTest AccessibilityWalkTest -j4 > logs/08-build-green.log` | 0 |
| GREEN | `ctest -R "NStateButtonActivationTest\|AccessibilityWalkTest" --output-on-failure > logs/09-test-green.log` | 0 (2/2) |
| 4b revert | `git diff -- include/NStateButton.h src/gui/widgets/NStateButton.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b rebuild / run | `cmake --build build --target NStateButtonActivationTest -j4`; `ctest -R NStateButtonActivationTest` (logs/11-test-revert.log) | 0 / **8** (4 failed) |
| 4b reapply | `git apply logs/fix.patch`; `cmake --build build --target zene NStateButtonActivationTest -j4` | 0 / 0 |
| Family | `cmake --build build --target RevisionTimelineTest TimelineTest ArrangerTimelineTest -j4`; `ctest -R "NStateButton\|AccessibilityWalk\|TimeLine\|Timeline" --output-on-failure > logs/15-test-family.log` | 0 / 0 (5/5 tests passed) |
| Gates | `feature_check.py .` (log-dir-redirected copy, see below) > logs/17-feature-check.log | 0 (RESULT: PASS, all 8 rows) |

On the first run of the family (logs/13-test-reapply.log), RevisionTimelineTest, TimelineTest and
ArrangerTimelineTest showed as "Not Run" because their executables had not been built in this tree.
After building them, all 5 tests passed (logs/15).

Gate note: the stock `feature_check.py` failed with `PermissionError` creating
`<worktree>/../zene-qwen-gui-1105-main-loop-points-feature-check`, which is outside this lane's write
area (logs/16-feature-check.log, EXIT=1). I ran a copy that differs only in its log directory, plus
`ROOT` pinned to the original script's directory so the same FIX-BASE and baseline cache are used.
The copy is `runs/gui-1105-main-loop-points.r1/fc/feature_check_logs_redirected.py`; `diff` shows
those 2 lines only. The gate commands and the PASS logic are unchanged. Results:

- PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
  doc-figures
- PASS (no new item in your files): complexity, file-length

## Files committed

- `include/NStateButton.h`
- `src/gui/widgets/NStateButton.cpp`
- `tests/src/gui/NStateButtonActivationTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`: the `.cpp` reason extended, the `.h` line added
- `docs/qwen-fixes-2026-10/cluster-gui-1105-main-loop-points.md`

## Self-check

Round 3 (current code, r3 logs): reverted product → `ctest -R '^NStateButtonActivationTest$'` EXIT 8,
5 of 8 cases fail (logs/06); reapplied → EXIT 0, all pass (logs/08); family 5/5 EXIT 0 (logs/04);
gates EXIT 0 via the log-redirect wrapper, stock script EXIT 1 on the PermissionError only (logs/01,
02). Release suppression is now scoped to `mouseReleaseEvent` (`NStateButton.cpp:106-113`), so a
press dragged off the button leaves no flag behind (no `clicked()`, flag reset on return). The
round-2 bullets below are kept as history.

- **The test fails with the fix reverted and passes with it.**
  - Reverted (`git apply -R logs/fix.patch`, EXIT 0; rebuild EXIT 0):
    `ctest -R NStateButtonActivationTest` gave EXIT 8, `Totals: 5 passed, 4 failed`
    (logs/11-test-revert.log).
  - Reapplied (`git apply logs/fix.patch`, EXIT 0; rebuild EXIT 0): EXIT 0, 5/5 in the family run
    (logs/15-test-family.log).
- **The test is deterministic.** It drives the widget synchronously through `QAbstractButton::click()`,
  `QAccessibleActionInterface::doAction(pressAction())`, `QTest::keyClick(Space)` and
  `QTest::mousePress/mouseRelease` at fixed points. It uses no timers, sleeps, threads or event-loop
  waits, and the results are read straight from `state()` and a `QSignalSpy`.
- **The fix is at the cause and complete.** The cause is in the shared widget, not in the
  loop-points wiring.
  - Every `NStateButton` user (`rg -n NStateButton src include`: only
    `src/gui/editors/TimeLineWidget.cpp:99,108,117`, i.e. auto-scroll, loop points and stop
    behaviour) now advances on every activation path.
  - Their connections (`TimeLineWidget.cpp:105,112-113,127-137`) go through `changedState`/`changeState`.
    Those are unchanged, so the timeline → button sync (`loopEnabledChanged` → `changeState`) does
    not loop back. `changeState` only emits on a real change, and `onClicked` is reached only by an
    activation.
  - Both entry paths (the mouse-press path and the `clicked()` path) go through one `advanceState()`.
  - A press dragged off the button clears the flag in `mouseReleaseEvent`.
  - A double click goes press → release → press → release. Each press advances once and each
    release's `clicked()` is skipped, the same count as before.
- **No collateral damage.** No serialization, model, undo, control command or audio-thread code
  changed. Loop state is still stored by `Timeline`, so old projects load as before.
  - No limit or check was added. The flag only suppresses the `clicked()` that pairs with a mouse
    release handler whose press already advanced the state.
  - AccessibilityWalkTest, which holds the earlier R8.5 accessible-name change to this file, and the
    Timeline, ArrangerTimeline and RevisionTimeline tests pass (5/5, logs/15).
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable, the verdict is FIXED (REAL). The RED
  run in logs/07 shows the bug on the unfixed branch tip.
- **Gates:** `submit_check.py` re-runs the stock `feature_check.py`, which cannot create its log
  directory outside this lane's write area (PermissionError, logs/16). The redirected-log copy
  (same gate commands and logic) reports RESULT: PASS with EXIT 0 (logs/17-feature-check.log).

## QA follow-up (2026-11-09)

Independent review found one stale-suppression path: if the button was disabled after a mouse press,
Qt consumed the release before `NStateButton::mouseReleaseEvent`, leaving `m_advancedOnPress` set.
The next programmatic/accessibility activation was then swallowed. The added deterministic test
`disabledDuringMousePressDoesNotSwallowTheNextClick` reproduced it against the submitted code:

| Step | Command | EXIT |
| --- | --- | --- |
| Reproduce submitted issue | `cmake --build build --target NStateButtonActivationTest -j12`; `cd build/tests && ctest -R '^NStateButtonActivationTest$' --output-on-failure` | 0 / 8; the new test reported actual state 1, expected 0 |
| Product fix and GUI family build | `cmake --build build --target zene NStateButtonActivationTest AccessibilityWalkTest RevisionTimelineTest TimelineTest ArrangerTimelineTest -j12` | 0 |
| Focused GUI family | `cd build/tests && ctest -R 'NStateButtonActivationTest|AccessibilityWalkTest|Timeline|TimeLine' --output-on-failure` | 0, 5/5 |
| Full product reversal | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-loop-points.r2/negctl/full-product-current.patch`; `cmake --build build --target zene NStateButtonActivationTest -j12`; `cd build/tests && ctest -R '^NStateButtonActivationTest$' --output-on-failure` | 0 / 0 / 8; five activation assertions failed, including the original click/accessibility/Space paths and the disabled-release case |
| Restore product paths | `git checkout -- include/NStateButton.h src/gui/widgets/NStateButton.cpp`; `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-loop-points.r2/negctl/qa-fix.patch`; `cmake --build build --target zene NStateButtonActivationTest AccessibilityWalkTest RevisionTimelineTest TimelineTest ArrangerTimelineTest -j12` | 0 / 0 / 0 |
| Stock feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-main-loop-points-af010bc3cc` | 0, all 8 rows PASS; no new complexity or file-length items |

The final implementation scopes `m_suppressReleaseClick` to the base mouse-release handler, which
keeps the original press-time mouse behavior and prevents a release that Qt consumes while the
button is disabled from poisoning the next non-mouse activation. The regression suite now has eight
cases, and the worktree was rebuilt and tested after restoring the patch.
