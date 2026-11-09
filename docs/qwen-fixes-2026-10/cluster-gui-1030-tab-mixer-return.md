FIXED

# gui-1030-tab-mixer-return - Return channel button can be clipped off-screen

## Verdict: REAL (root cause is not the one Luna named)

Luna blamed the channel scroll area's six-strip minimum (`src/gui/MixerView.cpp:203`). Measured, that is a
secondary cause. The main one is the mode bar: `MixerModeBar` (`src/gui/MixerModeBar.cpp`, constructor) is one rigid
`QHBoxLayout` of 20-odd controls whose `minimumSizeHint()` is 1576 px wide. `MixerView`'s outer layout
(`MixerView.cpp:309`, `SetMinimumSize`) turns the widest child's minimum into the mixer's minimum, the shell
panel passes it up, and `MainWindow` ends up with a minimum width of **2000 px** when the Mix view is shown with the
Inspector (min 200, `Shell.cpp:204`) and the Browser (min 220, `Shell.cpp:205`) open. A 1024-wide screen cannot show
that, so the right edge of the mixer - New mixer channel and Return - is off screen. Scrolling the channel strips
cannot reach it, as Luna said.

Measurements (probe test, before the fix; logs `test-3.log`, `test-4.log`): mixer min 1576x418; mode bar min
1576x38; splitter (master + channels + add column + rack) min 763x380; window min 2000x683. After the fix: mixer min
507x428, window min 931x693.

Sweep evidence: `terra-pilot/buttons/runs/m0-train-24/results.jsonl.bak-pre-reprep` records the Return button as
`missing: the control was not on screen in its view`; the re-run in `results.jsonl` clicked it at (1103,1153) on a
larger screen. That fits a layout that only fails when the window is narrow. I did not take a screenshot on a real
1024x768 display - the proof is the offscreen test below.

## Fix

- `src/gui/MixerModeBar.cpp`: the controls now sit in one row inside `ModeBarScroll`, a `QScrollArea` with minimum
  width 0 that scrolls sideways when the bar is narrower than its controls and grows its height by the scroll bar's
  while the bar shows. Every control, object name, accessible name, command property and signal is unchanged; the
  bar's public accessors are untouched. At a wide window nothing changes (no scroll bar, same height).
- `src/gui/MixerView.cpp:203`: the channel area's minimum width is two strips, not six. The area scrolls, so a smaller
  minimum only matters when the panel is narrow. Without it the splitter alone still needs 763 px, i.e. a 1183 px
  window with both side panels.
- `tests/upstream-modifications.txt`: one new line for `src/gui/MixerView.cpp`. `MixerModeBar.cpp` is already in
  `tests/fork-sources.txt`.
- No new command, nothing on the audio thread, no saved-data change.

## Regression test

`tests/src/gui/MixerSelectionIdentityTest.cpp`, `theReturnButtonStaysOnAThousandPixelScreen`: Mix view, Inspector and
Browser revealed; asserts the window's minimum width is <= 1024, resizes it to 1024x768, asserts the width stuck and
that `newReturnBtn` is visible and wholly inside the window, then scrolls the mode bar to its end and asserts the last
control (`mixerModeMatrix`) is inside the bar. No sleeps, timers or wall-clock use.

## Evidence

| Step | Command (from `build/tests` unless noted) | Result |
| --- | --- | --- |
| RED (fix reverted) | `ctest -R '^MixerSelectionIdentityTest$' --output-on-failure` (log `test-11-RED.log`) | EXIT=8, `'win->minimumSizeHint().width() <= 1024' returned FALSE. (window minimum width 2000)`, 0 of 1 passed |
| revert / re-apply | `git diff -- src/gui/MixerModeBar.cpp src/gui/MixerView.cpp > .../logs/fix.patch && git apply -R fix.patch`; later `git apply fix.patch` | EXIT=0 / EXIT=0 |
| rebuild | `CCACHE_DISABLE=1 cmake --build . --target zene MixerSelectionIdentityTest -j4` (`build-11-revert.log`, `build-12.log`) | EXIT=0 / EXIT=0 |
| GREEN | `ctest -R '^MixerSelectionIdentityTest$' --output-on-failure` (`test-10.log`, then in `test-14-family.log`) | EXIT=0, 1 of 1 passed |
| family | `ctest -R '^Mixer' --output-on-failure` (`test-14-family.log`) after building all 21 `Mixer*` targets (`build-14.log`, EXIT=0) | EXIT=0, 21 of 21 passed |

Gates (run by hand - see Notes): `tests/fork-sources-gate.sh` EXIT=0; `tests/all-sources-reproduce.sh` EXIT=0;
`tests/no-upstream-regression-gate.sh` EXIT=0; `tests/scripted/check-namespace` EXIT=0; `tests/no-tautology-gate.sh`
EXIT=0; `python3 tests/doc-figures-gate.py` EXIT=0; `tests/complexity-gate.sh --check` EXIT=1 (7 REGRESSION lines) and
`tests/file-length-gate.sh --check` EXIT=1 (26) - red on the train on purpose; none of those lines names
`MixerModeBar`, `MixerView` or `MixerSelectionIdentityTest` (grep over both logs, no match). Logs in `logs/gates/`.

Committed files: `src/gui/MixerModeBar.cpp`, `src/gui/MixerView.cpp`, `tests/src/gui/MixerSelectionIdentityTest.cpp`,
`tests/upstream-modifications.txt`, this report.

## Integration onto 050/m0-train-26 (`af010bc3cc`) - round 2

Source `bugs/qwen-gui-1030-tab-mixer-return @ 63cf701cf`; the three-way merge conflicted in `src/gui/MixerModeBar.cpp`
because the newer line added `Pick...` (`mixerModePickSelected`), `Follow Selection` (`mixerModeFollow`) and the
"Selected" filter to the bar's constructor. The merged result keeps the accepted design unchanged: every new control
sits inside the same `ModeBarScroll` row (`MixerModeBar.cpp:135-176`, no conflict markers left), `MixerView.cpp:205`
keeps the two-strip minimum, the regression test is unchanged. No new design was needed.

**One semantic difference from the accepted patch, found by running the neighbours of the changed file:**
`AccessibilityWalkTest::noSurfaceHasAnUnnamedFocusableWidget` failed on the merged tree with `unnamed focusable
widgets: Mixer/QScrollArea: 1 (allowed 0)` (`test-5.log`, EXIT=8). The new `ModeBarScroll` is a focusable
`QScrollArea` with no accessible name. Fix: `scroll->setAccessibleName(tr("Mixer mode controls"))`
(`src/gui/MixerModeBar.cpp`, next to the `new ModeBarScroll`). Nothing else changed.

Integration evidence (all from `build/`, logs in `.../gui-1030-tab-mixer-return.r2/logs/`):

| Step | Command | Result |
| --- | --- | --- |
| build merged tree | `CCACHE_DISABLE=1 cmake --build . --target zene MixerSelectionIdentityTest -j4` (`build-1.log`) | EXIT=0 |
| GREEN (merged) | `ctest -R '^MixerSelectionIdentityTest$' --output-on-failure` from `build/tests` (`test-1.log`) | EXIT=0, 1 of 1 |
| revert product files | `git diff --cached -- src/gui/MixerModeBar.cpp src/gui/MixerView.cpp > logs/fix.patch && git apply -R logs/fix.patch` | EXIT=0 |
| rebuild | same cmake command (`build-2-revert.log`) | EXIT=0 |
| RED | same ctest (`test-2-RED.log`) | EXIT=8, `'win->minimumSizeHint().width() <= 1024' returned FALSE. (window minimum width 2000)` |
| re-apply + rebuild | `git apply logs/fix.patch`; cmake (`build-3-reapply.log`) | EXIT=0 / EXIT=0 |
| GREEN | same ctest (`test-3-GREEN.log`) | EXIT=0, 1 of 1 |
| neighbours, before the name fix | `ctest -R '^(VcaStripLifetimeTest\|MixingRoutingTest\|AccessibilityWalkTest)$'` (`test-5.log`) | EXIT=8, AccessibilityWalkTest failed (above) |
| rebuild all 21 `Mixer*` targets + `VcaStripLifetimeTest`, `MixingRoutingTest`, `AccessibilityWalkTest` | `cmake --build . --target ...` (`build-7.log`) | EXIT=0 |
| family after the name fix | `ctest -R '^(VcaStripLifetimeTest\|MixingRoutingTest\|AccessibilityWalkTest\|Mixer)'` (`test-7-family.log`) | EXIT=0, 24 of 24 passed |

(The RED/GREEN pair was run before the accessible-name line was added; that line does not touch the layout, and the
final `MixerSelectionIdentityTest` pass is inside the 24-of-24 run above.)

Gates, run by hand (logs in `logs/gates/`): `fork-sources-gate.sh` EXIT=0; `all-sources-reproduce.sh` EXIT=0;
`no-upstream-regression-gate.sh` EXIT=0; `scripted/check-namespace` EXIT=0; `no-tautology-gate.sh` EXIT=0;
`doc-figures-gate.py` EXIT=0; `complexity-gate.sh --check` EXIT=1 (7 REGRESSION lines) and
`file-length-gate.sh --check` EXIT=1 (26) - red on the train on purpose; none of those lines names `MixerModeBar`,
`MixerView` or `MixerSelectionIdentityTest` (the only matches in the gate logs are the informational registration
listings of no-upstream-regression). `feature_check.py` itself could not run in that sandbox: it creates
`<worktree>-feature-check/` outside the write area (`gates-1.log`, `PermissionError`, EXIT=1). *(Superseded in
round 3: the script now runs and passes - see below.)*

Committed files: `src/gui/MixerModeBar.cpp`, `src/gui/MixerView.cpp`, `tests/src/gui/MixerSelectionIdentityTest.cpp`,
`tests/upstream-modifications.txt`, this report.

Not checked: a real 1024x768 display; tests outside the `Mixer*` family plus the three neighbours named above (the full
ctest suite was not run).

## Integration re-verification - round 3

Round-2 QA (Luna, ACCEPT-NOTE) noted the report's `feature_check.py` claim was stale: the script runs and passes in
the review environment. Re-verified here on the unchanged integrated tree (`026771efd8`; no product code changed this
round). The only semantic difference from the accepted patch is still the one documented above - the accessible name
on `ModeBarScroll` (`MixerModeBar.cpp:141`), added so `AccessibilityWalkTest` stays green. The regression test and
the registration line are byte-identical to the accepted patch (`diff` of both diffs: no output).

| Step | Command | Result |
| --- | --- | --- |
| build focused | `CCACHE_DISABLE=1 cmake --build build --target zene MixerSelectionIdentityTest VcaStripLifetimeTest MixerModesTest MixerMeteringTest MixerZonesTest AccessibilityWalkTest -j8` (`build-1.log`) | EXIT=0 |
| build family | `CCACHE_DISABLE=1 cmake --build build --target <21 Mixer*> VcaStripLifetimeTest MixingRoutingTest AccessibilityWalkTest -j8` (`build-2-family.log`) | EXIT=0 |
| GREEN | `ctest -R '^MixerSelectionIdentityTest$' --output-on-failure` from `build/tests` (`test-1.log`) | EXIT=0, 1 of 1 passed |
| family | `ctest -R '^Mixer' --output-on-failure` (`test-2-family.log`) | EXIT=0, 21 of 21 passed |
| neighbours | `ctest -R '^(VcaStripLifetimeTest\|MixingRoutingTest\|AccessibilityWalkTest)$' --output-on-failure` (`test-3-neighbours.log`) | EXIT=0, 3 of 3 passed |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-feature-check.log`) | EXIT=0, `RESULT: PASS`, 8/8 rows PASS |
| RED | `git diff af010bc3cc..HEAD -- src/gui/MixerModeBar.cpp src/gui/MixerView.cpp > logs/fix.patch && git apply -R logs/fix.patch`; rebuild (`build-3-revert.log`); `ctest -R '^MixerSelectionIdentityTest$'` (`test-4-RED.log`) | EXIT=0 / EXIT=0 / **EXIT=8**, `window minimum width 2000` |
| GREEN | `git apply logs/fix.patch`; rebuild (`build-4-reapply.log`); `ctest -R '^MixerSelectionIdentityTest$'` (`test-5-GREEN.log`) | EXIT=0 / EXIT=0 / EXIT=0, 1 of 1 passed |

### Affected failure paths and supported-platform hazards

Failure paths: `MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen` is the only test that asserts
the window minimum width (RED above). `AccessibilityWalkTest::noSurfaceHasAnUnnamedFocusableWidget` is the only test
that sees the new `ModeBarScroll` and needs its accessible name. The 21 `Mixer*` tests plus
`VcaStripLifetimeTest`/`MixingRoutingTest` cover the surrounding layout and routing. All pass.

Supported-platform hazards: the change uses only long-standing Qt APIs (`QScrollArea`, `QScrollBar::rangeChanged`,
`QWidget::setAccessibleName`, `updateGeometry`, `minimumSizeHint`/`sizeHint` overrides) present in Qt 5.15 and 6; no
Qt 6-only API, no `QString::midRef`. `MixerView.cpp` already included `<QScrollArea>`; `MixerModeBar.cpp` adds
`<QScrollArea>`/`<QScrollBar>`. No new file, no new command, no audio-thread or saved-data change. Build host is
Qt 6.12.0; the APIs are unchanged since Qt 5.

## Notes - what did not go as the brief assumed

- **ccache**: the build tree's ccache temp dir (`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not
  writable from this sandbox (`build-1.log`: `Permission denied`). I built with the environment variable
  `CCACHE_DISABLE=1`; no config was changed.
- **feature_check.py in round 2**: at 10:35 it could not create `<worktree>-feature-check/` from that sandbox
  (`PermissionError`, `gates-1.log`), so I ran its eight gate commands by hand. Round-2 QA noted the claim was stale
  and that the script runs in the review environment. In round 3 it runs and passes as written (round-3 section);
  the hand-run results were the same. No gate limit or baseline was changed.
- **Not checked**: a real (non-offscreen) 1024x768 display and the scroll bar's look under the real theme; the mode
  bar's keyboard focus scrolling (Qt's `QScrollArea` scrolls a focused child into view, not exercised here); tests
  outside the `Mixer*` family.

## Self-check

- **RED with the product change reverted, GREEN with it.** RED: `ctest -R '^MixerSelectionIdentityTest$'` with
  `git apply -R fix.patch` applied, EXIT=8, `window minimum width 2000` (`test-11-RED.log`; re-run in round 3:
  `test-4-RED.log`, EXIT=8, same assertion). GREEN: same command with the patch re-applied, EXIT=0, 1 of 1 passed
  (`test-14-family.log`: 21 of 21 in the `^Mixer` family, EXIT=0; round 3: `test-5-GREEN.log`, EXIT=0, and
  `test-2-family.log`, 21 of 21, EXIT=0).
- **Deterministic.** The test reads layout state only: `minimumSizeHint()`, widget geometry after `resize()`, and the
  scroll bar's own maximum. The only event handling is `QCoreApplication::processEvents()` once, to let the panel
  moves settle, and `sendPostedEvents(nullptr, QEvent::LayoutRequest)`. No timers, sleeps, clocks or threads. It
  passed in every run (`test-10.log`, `test-12`/`14` family runs; round 3 `test-1.log`, `test-5-GREEN.log`).
- **Cause and completeness.** Callers and siblings checked: the only constructor of `MixerModeBar` is
  `MixerView.cpp:114`; its public accessors (`viewButton`, `densityButton`, `filterBox`, `followButton`, the chip
  buttons, `zonesButton`) return members and are untouched; the three other tests that touch it
  (`MixerModesTest`, `MixerMeteringTest`, `MixerZonesTest`) pass. The other minimum-width contributors measured
  before the fix were master 64, rack 245, add column 64 and the channel area (448 with six strips); only the
  channel area was an arbitrary choice, so only it was changed. After the fix the window minimum is 931 px, under
  1024 with both side panels open. The routing view and the compact/expanded density use the same bar and area, so
  they inherit the change.
- **No collateral damage.** Nothing is saved or loaded: the change is widget layout only. `mixerzones` layout state,
  the splitter sizes written by `window.mixer_zones`, project files and commands are untouched. The two-strip
  minimum is a lower bound, so no existing wider size is refused; at the sizes the mixer had before, it lays out as
  before (no scroll bar, same bar height). No limit or check was added that another caller could exceed.
- **Verdict is FIXED**, not ALREADY-FIXED or FALSE-POSITIVE, so there is no not-real claim to evidence.
- **Gates row of `submit_check.py`.** `feature_check.py` now runs as written: EXIT=0, `RESULT: PASS`, all eight rows
  PASS (`gates-feature-check.log`). The round-2 `PermissionError` was a sandbox write-area limit that no longer
  applies; no gate limit or baseline was changed.
