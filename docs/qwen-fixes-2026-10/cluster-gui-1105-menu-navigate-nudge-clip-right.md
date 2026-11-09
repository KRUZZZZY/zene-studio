FIXED

# Cluster gui-1105-menu-navigate-nudge-clip-right: the Navigate refusal notification was placed off-screen

## Integration onto 050/m0-train-26 (round 2, 2026-11-09)

Integration job: the accepted patch (`bugs/qwen-gui-1105-menu-navigate-nudge-clip-right` @ `41327bece6`) onto
`050/m0-train-26` @ `af010bc3cc`. Logs are in
`~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs`.

- **Conflict resolution:** none was needed. The driver's three-way apply fell back to a direct apply, and all
  three files applied cleanly (`integration-apply.log`: "Applied patch to 'src/gui/widgets/TextFloat.cpp'
  cleanly"). The train's newer `displayMessage` code (the theme-17 `a11y::announceStatus` block,
  `TextFloat.cpp:144-148`) comes after the clamp call at `TextFloat.cpp:142` and is untouched. The float is
  positioned before it is announced, as in the accepted patch.
- **Semantic difference from the accepted patch:** none. `git diff 41327bece6 --cached -- src tests` printed
  0 lines (`01-diff-vs-accepted.txt`), so the integrated product and test code is byte-identical to the
  accepted branch.
- **Failure paths and platform hazards:** the parent branch (a refusal anchored to a maximized window,
  `TextFloat.cpp:134`) and the no-parent branch (`:140`) both go through `keepOnScreen` (`:49`). `screen() ==
  nullptr` returns early, so the float stays where it was put. A float larger than the screen pins to the
  top-left of the area (the `std::max` guard). On multi-monitor setups the clamp uses the anchor's own screen. This is
  Qt-only code (`QWidget::screen()`, Qt >= 5.14; the tree is Qt6) with no platform `#ifdef`. It is GUI-thread
  only, with nothing on the audio thread and no undo, save-data or control-command change.

| Step | Command | EXIT |
| --- | --- | --- |
| Build (ccache) | `cmake --build build --target zene GrooveMenuTest -j4 > LOGS/02-build.log` | 2 (ccache temp dir outside write jail: "Permission denied"; not a compile error) |
| Build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > LOGS/03-build.log` | 0 |
| GREEN | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > LOGS/04-test-green.log` | 0 (1/1) |
| 4b revert | `git diff --cached -- src/gui/widgets/TextFloat.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| 4b RED | rebuild `zene GrooveMenuTest` (`05-build-red.log`, EXIT 0); `ctest -R '^GrooveMenuTest$'` > `06-test-red.log` | 8 (`screen.contains(placed)` FALSE: "refusal placed at 835,2 146x80, screen 0,0 800x800", GrooveMenuTest.cpp:198; 4 passed, 1 failed) |
| 4b reapply | `git apply LOGS/fix.patch` | 0 |
| 4b GREEN | rebuild `zene GrooveMenuTest PluginShortcutsTest` (`07-build-green.log`, EXIT 0); `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$'` > `08-test-green.log` | 0 (2/2) |
| Family | rebuild `PianoRollBenchmarkTest Vst3InstrumentIntegrationTest` (`11-build-family.log`, EXIT 0); `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest\|PianoRollBenchmarkTest\|Vst3InstrumentIntegrationTest)$'` > `12-test-family.log` | 0 (4/4) |
| Gates (direct) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/09-feature-check.log` | 1 (PermissionError creating `<worktree>-feature-check`, outside the write jail) |
| Gates | `FC_LOGS=LOGS/fc python3 LOGS/fc_wrapper.py <worktree> > LOGS/10-feature-check.log` (unchanged script source, log dir redirected only) | 0 (8/8 PASS, RESULT: PASS) |

The round-1 evidence for the accepted patch follows below, unchanged.

## Verdict: REAL

- `src/gui/MenuBarExtras.cpp:87` – Navigate > Nudge Clip Right runs `arrangement.nudge` through
  `addCommandAction`. With no clip selected the command refuses, and `announce()` reports it with
  `TextFloat::displayMessage(..., parent)` (`src/gui/CommandRun.cpp:95`). Here `parent` is the menu's host, which
  is the main window (`hostOf`, `src/gui/CommandRun.cpp:49-53`).
- `src/gui/widgets/TextFloat.cpp:134` (was line 113) places a float that has a parent at
  `parent->width() + 2`, just past the parent's right edge. That position works for a knob, but not for a
  maximized main window: the float lands past the screen's right edge.
- Reproduced in the test before the fix: `refusal placed at 835,2 146x80, screen 0,0 800x800`.

## Fix

`src/gui/widgets/TextFloat.cpp:49` adds `keepOnScreen()`. It clamps the float's frame inside the anchor screen's
`availableGeometry()`. `displayMessage` calls it once, after either placement (`TextFloat.cpp:142`), so the
parent branch and the no-parent branch behave the same way. A float that already fits on screen is not moved,
so every other caller places its message exactly as before. Nothing here runs on the audio thread. No undo
step, saved data or control command changes.

Siblings I checked:
- `FloatModelEditorBase.cpp:137` and `Fader.cpp:544` use the same `width() + 2` offset through `moveGlobal`, but
  their anchor is a small control inside a window, not a top-level window. I left them unchanged to keep this fix
  narrow. A knob sitting against the screen's right edge could in principle show the same thing; this report
  does not cover that.
- All 30 `displayMessage` callers now go through the clamp. On-screen placements are unchanged.

## Regression test

`tests/src/gui/GrooveMenuTest.cpp:177` `aMenuRefusalOverAFullScreenWindowStaysOnScreen`. It stretches the main
window across the screen, triggers the real Navigate > Nudge Clip Right action with no clip selected, and
requires exactly one TextFloat whose frame lies inside the screen's available geometry (line 198). It has no
timing and no sleeps.

## Evidence (logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs)

ccache's temporary directory is outside this lane's write jail (`01-build-red.log`: "Permission denied"), so
every build after that ran with `CCACHE_DISABLE=1`.

| Step | Command | EXIT |
| --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target GrooveMenuTest -j4 > LOGS/07-build-red.log` | 0 |
| RED test | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > LOGS/08-test-red.log` | 8 (`screen.contains(placed)` FALSE: 835,2 146x80 on 0,0 800x800) |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > LOGS/11-build-green.log` | 0 |
| GREEN test | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > LOGS/12-test-green.log` | 0 (2/2) |
| 4b revert | `git diff -- src/gui/widgets/TextFloat.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| 4b without fix | rebuild `zene GrooveMenuTest` (13-build-4b-without.log, EXIT 0); `ctest -R '^GrooveMenuTest$'` > 14-test-4b-without.log | 8 (same failure, 835,2) |
| 4b reapply | `git apply LOGS/fix.patch` | 0 |
| 4b with fix | rebuild (15-build-4b-with.log, EXIT 0); `ctest -R '^GrooveMenuTest$'` > 16-test-4b-with.log | 0 (1/1) |
| Family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest\|PianoRollBenchmarkTest\|Vst3InstrumentIntegrationTest)$'` > 18-test-family.log (every test that uses TextFloat) | 0 (4/4) |
| Gates | `python3 LOGS/run_feature_check.py <worktree> LOGS/feature-check > LOGS/20-gates.log` | 0 (8/8 PASS) |

About the gates: `feature_check.py` creates `<worktree>/../<name>-feature-check`, which is outside this lane's
write jail. The direct run failed with `PermissionError` (EXIT 1, `19-gates.log`). `run_feature_check.py` executes
the unchanged script source and changes only that log directory, to `LOGS/feature-check`. No gate, limit or
baseline was changed.

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt` (registers the inherited TextFloat.cpp change), and this report.

## Self-check

- **The test fails with the fix reverted and passes with it:** yes. Reverted with `git apply -R LOGS/fix.patch`
  (EXIT 0), rebuilt (EXIT 0), then `ctest -R '^GrooveMenuTest$'` gave EXIT 8:
  `screen.contains(placed)` FALSE, refusal at 835,2 on 0,0 800x800 (`14-test-4b-without.log`). Reapplied with
  `git apply LOGS/fix.patch` (EXIT 0), rebuilt (EXIT 0), and the same ctest gave EXIT 0 (`16-test-4b-with.log`).
- **The test is deterministic:** it has no timers, sleeps or waits. The seam is geometry. The main window is set
  to the offscreen screen's `availableGeometry()`, and the test checks that it reaches the right edge. It calls
  `QAction::trigger()`, which runs `arrangement.nudge` synchronously. `displayMessage` positions the float
  synchronously before it returns. The test then reads `frameGeometry()` straight away.
- **The fix is at the cause and complete:** the off-screen position is computed in `displayMessage`
  (`TextFloat.cpp:134`), and the clamp sits there, at `TextFloat.cpp:142`. It covers both branches and all 30
  `displayMessage` callers (`rg -n displayMessage src plugins`). `CommandRun.cpp:95` still passes the menu's host
  window, which is correct, so it needs no change. The knob and fader floats (`FloatModelEditorBase.cpp:137`,
  `Fader.cpp:544`) do not go through `displayMessage` and anchor to small controls. I left them unchanged on
  purpose and noted them above as out of scope.
- **No collateral damage:** the change only affects GUI placement. Project load and save, saved data, undo and
  control commands are untouched. The clamp moves a float only when part of it would fall outside the screen's
  available area. `std::max` guards a float larger than the screen, which then pins to the top-left corner of the
  area instead of producing an inverted clamp range. Every test that uses TextFloat passed (4/4,
  `18-test-family.log`).
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable; the verdict is FIXED.
- **The "gates (feature_check.py)" row in submit_check:** inside this lane, `submit_check.py:117` runs
  `feature_check.py` directly, and that script's `mkdir` of `<worktree>/../<name>-feature-check` is denied by the
  write jail (`19-gates.log`: PermissionError, EXIT 1). The unchanged gate source, run with only the log
  directory redirected, passes 8/8 with EXIT 0 (`20-gates.log`). The loop's own run outside the jail should be
  able to create that directory.
