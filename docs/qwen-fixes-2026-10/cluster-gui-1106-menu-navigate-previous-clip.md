FIXED

# cluster gui-1106-menu-navigate-previous-clip: Navigate refusal notices are placed outside the main window

## Verdict: REAL, fixed at the cause

- `src/gui/MenuBarExtras.cpp:67` creates the Navigate menu with `bar->window()` (the main window) as its parent;
  `src/gui/CommandRun.cpp:127-128` runs each menu command with `hostOf(menu)`, which walks up past the
  menus (`src/gui/CommandRun.cpp:49-53`) to that window.
- `src/gui/CommandRun.cpp:95-96` (`announce`) shows the refusal with `TextFloat::displayMessage(..., parent)`.
- `src/gui/widgets/TextFloat.cpp:113` (before the fix) placed every parented message at `parent->width() + 2`.
  That is correct for a widget (a knob, a button) inside a window. For a window it is past the window's right
  edge, so the user never sees it. The RED run measured this: the float was at x=906 while the window covered x=2..902.
- Other callers pass a window too: `src/gui/shell/InspectorPanel.cpp:74` (`from->window()`),
  `src/gui/shell/InspectorChannelRouting.cpp:87,96` (`->window()`), `src/gui/AafImportMenu.cpp:60`
  (`QApplication::activeWindow()`), and every menu that goes through `addCommandAction`. All of them had the same bug.
  The only parented caller of `displayMessage` is `CommandRun::announce`. Every other caller passes no parent.
  So I fixed it in `displayMessage`, and every one of those paths gets the fix.

## Fix

`src/gui/widgets/TextFloat.cpp:111-122`: a widget parent (`!parent->isWindow()`) still gets the message beside it,
unchanged. A window parent gets the same placement the no-parent path already uses (lower left inside the window,
`QPoint(32, h - tf->height() - 8)`), measured against that window. Calls with no parent behave as before.
No header change. No audio-thread code, undo or command changes. No saved data is involved.

## Tests (tests/src/gui/GrooveMenuTest.cpp)

- `aRefusalFromAMainMenuIsShownInsideTheWindow`: shows the main window at 900x600, installs or gets the real
  Navigate menu (`installNavigateMenu`) and triggers "Previous Clip" with no clip selected. It checks that exactly one
  TextFloat appears and that its global geometry lies inside the window's global rect. This is the regression test.
- `aFloatForAWidgetStillOpensBesideIt`: a child widget parent still gets its float at `mapToGlobal(0,0) + (width+2, 0)`.
  It guards the sibling path; it passes both before and after the fix.
- Deterministic: no sleeps or timers are waited on. Geometry is read straight after the synchronous trigger.

## Integration (round 2: 050/m0-train-26 @ af010bc3cc)

Accepted patch from `bugs/qwen-gui-1106-menu-navigate-previous-clip` @ ff0780bae8 applied to the newer line in this worktree
(`integration-apply.log`: TextFloat.cpp, GrooveMenuTest.cpp, upstream-modifications.txt applied cleanly; no conflict markers).
`TextFloat.cpp:111-122` is identical to the accepted design. Nothing was redesigned. The evidence below was re-measured on
this integrated tree (the round-1 numbers are not reused).

## Evidence (logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-previous-clip.r2/logs)

| Step | Command | EXIT |
| --- | --- | --- |
| build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > build-1.log` | 0 |
| 4b revert | `git diff --cached -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b rebuild | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > build-2-reverted.log` | 0 |
| RED (fix reverted) | `ctest -R '^GrooveMenuTest$' --output-on-failure > test-1-red.log` (from build/tests) | 8: `FAIL! aRefusalFromAMainMenuIsShownInsideTheWindow ... float at 906,4 142x76 is outside the window 2,2 900x600`; 0 of 1 passed |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| rebuild | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > build-3-green.log` | 0 |
| GREEN + family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > test-2-green.log` | 0: 100% tests passed out of 2 |
| gates (as given) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-1.log` | 1: PermissionError, it makes `<worktree>-feature-check` beside the worktree, outside the write-jail |
| gates (log path moved) | `python3 -I logs/feature_check_logpath.py <worktree> > gates-2.log` | 0: all 8 rows PASS, RESULT: PASS |

`feature_check_logpath.py` differs from `feature_check.py` by exactly two lines (`ROOT` fixed to the script folder, `logs` = `logs/fc`
in the LOG FOLDER); checks, limits and baselines are unchanged.

## Files committed

- `src/gui/widgets/TextFloat.cpp` (fix)
- `tests/src/gui/GrooveMenuTest.cpp` (regression + sibling guard)
- `tests/upstream-modifications.txt` (TextFloat.cpp's existing line 1349 extended with this reason; file ends with a newline)
- `docs/qwen-fixes-2026-10/cluster-gui-1106-menu-navigate-previous-clip.md` (this report)

## Self-check

- **Fails with the fix reverted, passes with it:** yes. The RED/GREEN rows above show it: with `git apply -R logs/fix.patch`
  the test fails with EXIT=8 (`float at 906,4 142x76 is outside the window 2,2 900x600`). After `git apply logs/fix.patch`
  it passes with EXIT=0 (100% of 1). Both runs (round 2, this tree) used a rebuilt `zene` + `GrooveMenuTest` (build EXIT=0 each time).
- **Deterministic:** yes. No sleeps, timers or wall-clock timing. The test sets the window size itself (900x600),
  triggers the real Navigate action synchronously (`QAction::trigger`, which runs `runCommand` -> `announce` ->
  `displayMessage` in the same call), then reads `TextFloat::geometry()` straight away. It removes earlier floats
  (`qDeleteAll`) before and after, so other test functions cannot leak floats into it. The float's 4000 ms close timer never matters.
- **At the cause and complete:** the wrong offset was in `TextFloat::displayMessage` (`src/gui/widgets/TextFloat.cpp:113`).
  Its only parented caller is `CommandRun::announce` (`src/gui/CommandRun.cpp:95`). Every other `displayMessage` caller
  (MainWindow, ClipView, ClipLockFeedback, PianoRoll, DataFile, TrackContainer, VstEffect, Vestige, FileBrowser,
  LmmsStyle, ControlCommandsTheme, ArrangerFileDrop, ...) passes no parent and is unchanged. The window parents
  (`addCommandAction` menus, `InspectorPanel.cpp:74`, `InspectorChannelRouting.cpp:87,96`, `AafImportMenu.cpp:60`) all get the
  fix. The widget parents (`TransportReadouts.cpp:338`, `DevicePanel.cpp:190`, `DeviceCard.cpp:80,395`,
  `DeviceCardState.cpp:167`) keep the beside-it placement, and `aFloatForAWidgetStillOpensBesideIt` guards that.
  The two sibling `moveGlobal(this, width()+2 ...)` users (`FloatModelEditorBase.cpp:137`, `Fader.cpp:544`) anchor to
  widgets, never to windows, so I left them alone.
- **No collateral damage:** this is a placement-only GUI change. No saved data, file format, undo, control command or audio-thread
  code is touched, and no limit or check was added. The no-parent placement is unchanged.
- **ALREADY-FIXED / FALSE-POSITIVE:** not applicable. The verdict is FIXED, and the RED run reproduced the bug.
- **Gate row in submit_check:** `submit_check.py` runs the original `feature_check.py`. That script creates
  `<worktree>-feature-check` beside the worktree, which is outside this lane's write-jail, so it fails with PermissionError
  (`logs/gates-1.log`, `logs/submit-1.log`). The same checks, run with only the log directory moved
  (`logs/feature_check_logpath.py`), pass with EXIT=0 and RESULT: PASS (`gates-2.log`, run on this integrated tree).
  The loop's own run outside the jail should give the same result. That has not been verified from here.
