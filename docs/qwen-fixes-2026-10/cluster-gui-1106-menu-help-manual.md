FIXED

# cluster gui-1106-menu-help-manual — Manual opens behind existing dialogs

## Verdict: REAL, fixed

`HelpCenter::showHelp` (src/gui/HelpCenter.cpp:227-247) ended with `QDialog::show()` + `raise()` only. When the
Help Center already exists and is visible (opened earlier, then About / Keyboard Shortcuts opened over it),
`show()` is a no-op and `raise()` does not activate the window, so the window manager keeps the active dialogs in
front and Help > Manual / Help > Command Reference appear to do nothing. The sibling path in this codebase that
brings a floating window forward, `MainWindow::toggleWindow` (src/gui/MainWindow.cpp:1268-1269), already does
`raise()` + `activateWindow()`.

Fix: src/gui/HelpCenter.cpp:244-245 — `raise()` followed by `activateWindow()`.

Callers checked (all reach the one function, so all are fixed together): Help menu Manual / Command Reference,
src/gui/WorkspaceWindows.cpp:76 (find a command), src/gui/ControlCommandsHelp.cpp:65 (F1 context help) and :84
(`app.help`). `GuidedTour` (src/gui/HelpCenter.cpp:412) is a child overlay of the main window, not a top-level
window, so it does not share this problem. No new control command, no audio-thread code, no saved-data change.

## Regression test

tests/src/gui/HelpAccessTest.cpp:478 `helpComesForwardOverOtherOpenWindows`: opens the Help Center, then opens and
activates two other top-level dialogs (stand-ins for About and Keyboard Shortcuts), checks the second one is the
active window, then calls `showHelp(…, false)` (Manual) and `showHelp(…, true)` (Command Reference) and requires
`QApplication::activeWindow() == centre` and the right tab each time. Deterministic: only
`QCoreApplication::processEvents()` delivers the offscreen platform's activation events; no sleeps or timeouts.

## Evidence (logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-help-manual.r1/logs)

Note: ccache could not write its temp dir (`01-build-red.log`: "ccache: error: failed to create temporary file …
Permission denied"), so every build ran with `CCACHE_DISABLE=1`.

| Step | Command | EXIT |
| --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target zene HelpAccessTest -j4 > logs/03-build-red.log` | 0 |
| RED test | `cd build/tests && ctest -R '^HelpAccessTest$' --output-on-failure > logs/04-test-red.log` | 8 — `FAIL! helpComesForwardOverOtherOpenWindows … Actual (QApplication::activeWindow()): QDialog … Expected (centre): HelpCenter`, HelpAccessTest.cpp(499); Totals 18 passed, 1 failed |
| GREEN build | same build `> logs/05-build-green.log` | 0 |
| GREEN test | same ctest `> logs/06-test-green.log` | 0 — 100% tests passed out of 1 |
| 4b revert | `git diff -- src/gui/HelpCenter.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b build / test without fix | `logs/07-build-4b-revert.log` / `logs/08-test-4b-revert.log` | 0 / 8 (Totals 18 passed, 1 failed — the new test) |
| 4b re-apply | `git apply logs/fix.patch` | 0 |
| 4b build / test with fix | `logs/09-build-4b-apply.log` / `ctest -R '^HelpAccessTest$' -V > logs/10-test-4b-apply.log` | 0 / 0 (Totals 19 passed, 0 failed) |
| Gates | `feature_check.py <worktree>` via `logs/fc_wrap.py` → `logs/12-feature-check.log` | 0 — RESULT: PASS (all 8 rows PASS) |

Affected family: `ctest -R '^HelpAccessTest$'` = 1 ctest test, 19 QtTest functions, all pass.

Gate note: `feature_check.py` run directly (`logs/11-feature-check.log`, EXIT=1) failed with PermissionError
creating `<worktree>-feature-check` beside the worktree, which is outside this lane's write-jail.
`logs/fc_wrap.py` runs the identical script with only that log-directory path replaced by `logs/feature-check`;
no gate logic, limit or baseline is changed.

Registration: src/gui/HelpCenter.cpp is already in tests/fork-sources.txt:2090; tests/src/gui/HelpAccessTest.cpp is
already listed in tests/upstream-modifications.txt. No new files in the product tree.

Files committed: src/gui/HelpCenter.cpp, tests/src/gui/HelpAccessTest.cpp,
docs/qwen-fixes-2026-10/cluster-gui-1106-menu-help-manual.md.

Not checked: a real X11/Wayland window manager. Focus-stealing prevention in some window managers can still
turn an activation request into an attention hint. The test covers Qt's activation request on the offscreen platform.
