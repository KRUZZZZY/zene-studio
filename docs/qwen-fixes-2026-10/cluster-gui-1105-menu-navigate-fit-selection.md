FIXED

# Cluster gui-1105-menu-navigate-fit-selection: Fit Selection refusal notification is positioned off-screen

## Verdict: REAL, fixed at the cause

Evidence path:
- `src/gui/MenuBarExtras.cpp:81`: Navigate > Fit Selection runs `arrangement.zoom {fit: selection}` through `addCommandAction`.
- `src/gui/CommandRun.cpp:127-128`: the action calls `runCommand(hostOf(menu), ...)`. `hostOf` (`CommandRun.cpp:49-53`) walks past every QMenu, so for the Navigate menu (parented to `bar->window()`, `MenuBarExtras.cpp:67`) the anchor is the **main window**.
- `src/gui/CommandRun.cpp:95-96`: `announce()` passes that anchor to `TextFloat::displayMessage`.
- `src/gui/widgets/TextFloat.cpp:111-114` (before the fix): any non-null parent was placed at `QPoint(parent->width() + 2, 0)`, which is to the right of the whole window. With a maximized or full-width window that is off the screen.

Measured before the fix (offscreen, 1600x1000 main window): `message at (1604,2) is outside the window 1600x1000+0+0`.

## Fix

`src/gui/widgets/TextFloat.cpp` `displayMessage`: when the anchor is a whole window (`parent->isWindow()`), the message now goes to that window's lower-left area. That is the same placement the no-parent path already used for the main window. Widget anchors (knobs, clips, timeline) are still placed beside the anchor at `width() + 2`, exactly as before. The test checks this as well.

Callers checked (`rg -n "displayMessage\("`): every caller passes either no parent, which is unchanged, or a child widget, which is unchanged. The only window anchors come from `CommandRun.cpp` `announce()`, and they reach it through `hostOf(...)`: the main window for menu-bar menus, or a dialog/form window. Each of those used to be placed off the window and is now placed inside it. `Fader.cpp:544` and `FloatModelEditorBase.cpp:137` call `moveGlobal` directly on child widgets, so they are untouched. Nothing here runs on the audio thread, and there is no undo, save-data or command change.

## Regression test

`tests/src/gui/GrooveMenuTest.cpp` `aMessageOverTheMainWindowLandsInsideIt` (line 176). It installs the Navigate menu the way `Shell.cpp:148` does, triggers Fit Selection in a fresh empty project, and asserts that exactly one TextFloat appeared and that its top-left lies inside the main window. It then checks that a child-widget anchor still places the message at `child.mapToGlobal(width()+2, 0)`. The test is deterministic: it uses no timers or sleeps, and the timeout float is inspected synchronously.

## Commands and results (logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-fit-selection.r1/logs)

Note: ccache's temp dir (`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is outside this lane's write-jail (01-build-red.log: Permission denied), so every build ran with `CCACHE_DISABLE=1`.

| Step | Command | EXIT |
|---|---|---|
| RED build | `CCACHE_DISABLE=1 cmake --build build --target GrooveMenuTest -j4 > logs/05-build-red.log` | 0 |
| RED test | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > logs/06-test-red.log` | 8 (FAIL: `message at (1604,2) is outside the window 1600x1000+0+0`) |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > logs/07-build-green.log` | 0 |
| GREEN family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > logs/08-test-green.log` | 0 (2/2 tests passed) |
| 4b reverse | `git diff -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b build | `CCACHE_DISABLE=1 cmake --build build --target GrooveMenuTest -j4 > logs/09-build-reverted.log` | 0 |
| 4b test without fix | `ctest -R '^GrooveMenuTest$' --output-on-failure > logs/10-test-reverted.log` | 8 (same FAIL, 1604,2) |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > logs/11-build-reapplied.log` | 0 |
| 4b test with fix | `ctest -R '^GrooveMenuTest$' --output-on-failure > logs/12-test-reapplied.log` | 0 (1/1 passed) |

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 with `PermissionError` (logs/13-gates.log). The script creates `<worktree>/../<name>-feature-check`, which is outside this lane's write-jail. I ran the same gate commands it runs (`feature_check.py:22-29`) from the worktree instead, with logs in logs/14..21:

| Gate | EXIT |
|---|---|
| fork-sources | 0 |
| all-sources-reproduce | 0 |
| no-upstream-regression | 0 |
| check-namespace | 0 |
| no-tautology | 0 |
| doc-figures | 0 |
| complexity --check | 1 (red on the train on purpose; no REGRESSION line names TextFloat.cpp or GrooveMenuTest.cpp) |
| file-length --check | 1 (red on the train on purpose; no REGRESSION line names TextFloat.cpp or GrooveMenuTest.cpp) |

I did not run the base-listing comparison (the script's cached base checkout) myself. The ratchet result above comes from grepping the REGRESSION lines for my two files: `grep -h "^REGRESSION" logs/*-gate-complexity.log logs/*-gate-file-length.log | grep -E "TextFloat|GrooveMenuTest"` printed nothing.

## Files committed

- `src/gui/widgets/TextFloat.cpp`
- `tests/src/gui/GrooveMenuTest.cpp`
- `tests/upstream-modifications.txt` (one line for TextFloat.cpp, ends with a newline)
- `docs/qwen-fixes-2026-10/cluster-gui-1105-menu-navigate-fit-selection.md`

## Self-check

- **Fails without the fix, passes with it:** yes. With the fix reverted (`git apply -R logs/fix.patch`, EXIT=0), the rebuilt GrooveMenuTest exited 8 with `FAIL! aMessageOverTheMainWindowLandsInsideIt ... message at (1604,2) is outside the window 1600x1000+0+0` (logs/10-test-reverted.log). With the fix reapplied (`git apply logs/fix.patch`, EXIT=0) and the test rebuilt, it exited 0 with 1/1 passed (logs/12-test-reapplied.log).
- **Deterministic:** yes. The test uses no timers, sleeps or waits. `QAction::trigger()` runs `arrangement.zoom` synchronously, and `announce()` creates the TextFloat synchronously (`CommandRun.cpp:110-111`), so the test reads the float's geometry right away. Its 4 s auto-close timer never fires inside the test. The window geometry comes from the offscreen platform (1600x1000 at 0,0). The second half of the test passes `timeout 0`, so that float has no timer at all.
- **At the cause and complete:** the placement rule in `TextFloat::displayMessage` (`TextFloat.cpp:111-121`) is the cause, so a fix in `MenuBarExtras` alone would leave every other menu routed through `CommandRun.cpp:128/150` broken. All 30 `displayMessage(` call sites were checked: those with no parent are unchanged, and those with a child widget (EnvelopeAndLfoView, TimeLineWidget, PianoRoll, ClipView, ClipLockFeedback, FileBrowser, ...) are unchanged. Only window anchors change, and those come from `CommandRun.cpp` `announce()` via `hostOf`. The sibling placements that call `moveGlobal` directly (`Fader.cpp:544`, `FloatModelEditorBase.cpp:137`) use child widgets and are not touched. The test also asserts that the child-anchor position stays the same.
- **No collateral damage:** the fix touches only GUI placement. It changes no saved data, project format, command, undo step or audio-thread code, and adds no new limit or check. The GrooveMenuTest and PluginShortcutsTest family passes 2/2 (logs/08-test-green.log).
- **ALREADY-FIXED / FALSE-POSITIVE:** not applicable; the verdict is FIXED (the bug was real, reproduced at 1604,2 on 1600 px).
- **Gates row FAIL in submit_check:** `feature_check.py` cannot run in this lane. It does `mkdir <worktree>/../zene-qwen-gui-1105-menu-navigate-fit-selection-feature-check`, which is outside the write-jail (PermissionError, logs/13-gates.log). Every gate command it wraps was run directly; the results are in the Gates table above. All exit-0 gates passed, and neither ratchet lists an item in a changed file. The base-listing diff step was not reproduced. Someone with write access to the projects directory needs to rerun `feature_check.py` to turn that row green.
