FIXED

# cluster gui-1030-menu-help-accessibility-check — Accessibility report is positioned outside a maximized window

## Verdict: REAL, fixed

How it happens on the base `af010bc3c`:

- `src/gui/WorkspaceMenus.cpp:165`: Help > Accessibility Check runs `window.accessibility_report` with `show = true`.
- `src/gui/CommandRun.cpp:49-52` (`hostOf`) walks past the QMenu to its host, the main window.
  `src/gui/CommandRun.cpp:95` (`announce`) calls `TextFloat::displayMessage(..., parent)` with that window.
- `src/gui/widgets/TextFloat.cpp:113` (base) placed the float at `parent->width() + 2`, which is just past the
  window's right edge. When the window is maximized, that is past the screen's right edge, so nobody sees the report.
  The regression measured the report at x=1604 on an 800-px offscreen screen.

## Fix

`src/gui/widgets/TextFloat.cpp:50` adds a new file-local `keepOnScreen()`. After `displayMessage` places the float,
this clamps it into the `availableGeometry()` of its anchor's screen. It runs in both branches: the parent branch
(`:135-136`) and the no-parent lower-left-of-main-window branch (`:142-143`), so the two placements stay consistent.
A float that already fits on screen is not moved, so every message that was visible before shows exactly where it did.
GUI thread only. Nothing on the audio thread, no undo change, no new command.

Siblings checked: `TextFloat::moveGlobal` (`include/TextFloat.h:56`) is only called from `displayMessage`, so all
~30 `displayMessage` callers get the fix. `SimpleTextFloat::moveGlobal` (knob, fader and clip value floats) is a
different class. Those floats sit beside small widgets, not the window, and are left unchanged (out of scope, and not
covered by this regression).

## Regression

`tests/src/gui/GrooveMenuTest.cpp:176` `aMessageOverAMaximizedWindowIsOnScreen`. It sizes the real main window to its
screen's available geometry, triggers the real Help > Accessibility Check `QAction`, and asserts that the new
TextFloat's frame lies inside the screen (`:201`). The test is deterministic: no sleeps or timers. It runs one
`processEvents()` so the shell's queued menu install runs.

## Evidence (logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1030-menu-help-accessibility-check.r1/logs)

| Step | Command | EXIT |
| --- | --- | --- |
| build test (ccache disabled: its temp dir is outside the write-jail, build-1 EXIT=2) | `CCACHE_DISABLE=1 cmake --build build --target GrooveMenuTest -j4 > LOGS/build-3.log 2>&1` | 0 |
| RED (before fix) | `ctest -R '^GrooveMenuTest$' --output-on-failure > LOGS/test-2-red.log` → `FAIL! aMessageOverAMaximizedWindowIsOnScreen 'area.contains(report->frameGeometry())' (report at 1604,2 290x196 is not inside the screen 0,0 800x800)` | 8 |
| build after fix | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > LOGS/build-4.log` | 0 |
| GREEN + family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > LOGS/test-3-green.log` → 100% tests passed out of 2 | 0 |
| 4b revert | `git diff -- src/gui/widgets/TextFloat.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| 4b reverted build / test | `build-5-reverted.log` EXIT=0; `ctest -R '^GrooveMenuTest$'` → same FAIL (1604,2) (`test-4-reverted.log`) | 8 |
| 4b reapply | `git apply LOGS/fix.patch` | 0 |
| 4b reapplied build / test | `build-6-reapplied.log` EXIT=0; `ctest -R '^GrooveMenuTest$'` → 100% passed out of 1 (`test-5-reapplied.log`) | 0 |
| zene rebuilt with fix | `CCACHE_DISABLE=1 cmake --build build --target zene -j4 > LOGS/build-7-zene.log` | 0 |
| gates, direct | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → PermissionError creating `<worktree>/../…-feature-check` (outside the write-jail) | 1 |
| gates, same script | `python3 runs/…r1/feature_check_runner.py <worktree> LOGS/feature-check`. This runs `feature_check.py` verbatim with only its log dir replaced. All 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item), file-length (no new item) | 0 |

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt` (reason line for TextFloat.cpp), and this report.

## Self-check

- **Gates (submit_check FAIL row):** `submit_check.py:117` runs `feature_check.py` directly. That script writes its
  logs to `<worktree>/../<name>-feature-check`, which is outside this lane's write-jail, so it fails with
  `PermissionError` before any gate runs (`gates-3-direct.log`, EXIT=1). This is not a gate failure. The same script,
  run with only its log dir moved into the run folder (`runs/…r1/feature_check_runner.py`), passes all 8 rows on the
  committed tree (`gates-4.log`, EXIT=0). Running `feature_check.py` from outside the jail should give the same PASS.
- **The regression fails with the product change reverted and passes with it:** yes.
  `git apply -R LOGS/fix.patch` (EXIT 0), then rebuild (EXIT 0), then `ctest -R '^GrooveMenuTest$'`: EXIT 8, with
  `report at 1604,2 290x196 is not inside the screen 0,0 800x800`. Then `git apply LOGS/fix.patch` (EXIT 0), rebuild
  (EXIT 0), and the same ctest: EXIT 0, 1/1 passed.
- **The test is deterministic:** it has no wall-clock timing, sleeps or races. The values are fixed by the test: the
  window geometry is set to `screen->availableGeometry()` (offscreen platform, 800x800). The real menu `QAction` is
  triggered synchronously, and `announce()` builds the float inside `trigger()`. One `QCoreApplication::processEvents()`
  runs the shell's queued zero-delay menu install (`Shell.cpp:145-151`), which is an event-loop sync point, not a timer
  wait. The report's 8 s auto-close timer never fires while the test runs, and the test closes the report itself.
- **The fix is at the cause and complete:** placement happens only in `TextFloat::displayMessage`, and both branches
  now clamp. `TextFloat::moveGlobal` has no other caller (searched with `rg -n "moveGlobal\("`). Every
  `displayMessage` caller (≈30, including `CommandRun.cpp:95`, `MainWindow.cpp:2029`, `ControllerTemplateMenu.cpp:71`)
  goes through it. `SimpleTextFloat` is a separate class for value floats beside small widgets and was deliberately
  left alone.
- **No collateral damage:** the clamp moves a float only when part of it is outside the available screen area, so any
  float that was fully visible before is placed exactly as before. There is no saved-data, project-load, undo or
  command change (ControlCommandsSnapshot is untouched). The fix adds no limit that other callers could exceed. If the
  float is bigger than the screen, `std::max` pins it to the top-left instead of giving `std::clamp` an inverted range.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable. The verdict is FIXED, and the cause is traced from
  `WorkspaceMenus.cpp:165` through `CommandRun.cpp:49,95` to `TextFloat.cpp:113` (base).
- Realtime: GUI-thread-only code.
- Registration: `tests/upstream-modifications.txt` has the new reason line (the file ends with a newline). No new
  source files.
