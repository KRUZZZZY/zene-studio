FIXED

# Cluster gui-1106-menu-help-accessibility-check: Accessibility report message is positioned off-screen

## Verdict: REAL, fixed - accepted patch integrated onto 050/m0-train-26

## Integration

- Source: `bugs/qwen-gui-1106-menu-help-accessibility-check` @ `4fc8a8ce8f6debe1944de23479756a1c0d879e8a`.
- Target: `050/m0-train-26` @ `af010bc3cc055d477045a54e2d866f59a95017d7` (the base this integration branch
  was pinned to; `git merge-base HEAD FIX-BASE` resolves to it).
- The round-1 QA review of the accepted fix was ACCEPT with no blocking items and no notes (r1 `qa-session.log`,
  final JSON: `"verdict": "ACCEPT", "blocking": []`), so this run's only task is the integration re-verification below.
- `git merge-base af010bc3cc055d477045a54e2d866f59a95017d7 4fc8a8ce8f6debe1944de23479756a1c0d879e8a`
  prints `af010bc3cc055d477045a54e2d866f59a95017d7` (EXIT=0). The source branch is a direct descendant of the target,
  so no line of the target is newer than the source branch and the accepted patch has no textual conflict.
  The driver's log shows it fell back to direct application (`integration-apply.log`), which then applied the patch
  cleanly to all four files.
- The applied working-tree diff for `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp` and
  `tests/upstream-modifications.txt` is byte-identical to `git diff af010bc3cc 4fc8a8ce8f` for those files:
  `diff -u logs/08-accepted-source-diff.patch logs/09-applied-diff.patch` EXIT=0 (log 10-diff-comparison.log).
  There is no conflict resolution and no semantic difference from the accepted patch.
- `git show af010bc3cc055d477045a54e2d866f59a95017d7:src/gui/widgets/TextFloat.cpp` is exactly the pre-fix file
  the patch was generated against, so the "newer line" named in the job reason is not present relative to this merge
  base.

## Path of the finding, verified in this tree

- `src/gui/MainWindow.cpp:510`: the Help menu is `new QMenu(this)`, so its parent is the main window.
- `src/gui/WorkspaceMenus.cpp:165`: Help > Accessibility Check runs `window.accessibility_report` with `show` true.
- `src/gui/CommandRun.cpp:49-53,128`: `hostOf(menu)` skips past the menus and returns the main window.
- `src/gui/CommandRun.cpp:95-96`: `announce` calls `TextFloat::displayMessage(..., parent)` with that window.
- `src/gui/widgets/TextFloat.cpp:113` (before the fix): `moveGlobal(parent, QPoint(parent->width() + 2, 0))` places
  the float 2 px past the window's right edge. That is right for a knob or a button. For a window it is outside the
  window, and off the screen when the window fills the screen. The RED run measured it: `report at 1241,2 294x200 is
  outside the window -2,-2 1241x804`.

## Fix (src/gui/widgets/TextFloat.cpp, identical to the accepted patch)

- `displayMessage` (`TextFloat.cpp:133-136`): when the parent is a window (`isWindow()`), the message goes in that
  window's lower-left area. This is the placement the no-parent path already used for the main window. A parent that
  is not a window (a control) still gets the float to its right, at the same offset as before.
- `onScreen` (`TextFloat.cpp:48`): every message's position is clamped to the available area of its anchor's screen.
  A position that already fits is unchanged. The test environment shows why this is needed: the offscreen screen is
  smaller than the main window's minimum size, so even "lower left of the window" ran below the screen's bottom edge.

Caller audit: `rg -n "displayMessage\("` over `src/`. Only `CommandRun.cpp:95` passes a parent. Its hosts are either
windows (menus of the main window or of dialogs), which now get the inside placement, or controls, which are placed as
before. Every other caller (MainWindow, PianoRoll, ClipView, LmmsStyle, DataFile, ...) passes no parent. Their
position is unchanged apart from the screen clamp. `SimpleTextFloat` and the knob and fader floats use their own
`moveGlobal` and are untouched. Nothing here touches the audio thread, undo, saved data or commands.

## Affected failure paths and supported-platform hazards

| Path / hazard | Behaviour |
| --- | --- |
| Window-parent reply (Help > Accessibility Check, any menu whose host is a window) | Inside the window's lower-left, then clamped to that screen's available area. This is the reported failure path. |
| Control-parent reply (context menu on a knob/button) | Still `control global pos + (width + 2, 0)`, exactly as before when it fits; clamped only when it would leave the anchor's screen. |
| No-parent reply (save, record, script, PianoRoll, ClipView, DataFile, ...) | Lower-left of the main window, exactly as before when it fits; now clamped to the main window's screen. |
| Screen is null (`anchor->screen()` returns nullptr) | `onScreen` returns the original point; no crash, no dereference. |
| Float larger than the screen's available area | Clamp uses `max(area.left(), area.right() + 1 - size.width())`, so it pins to the top/left instead of producing an inverted rectangle. It cannot shrink a float that genuinely does not fit. |
| Anchor on a secondary monitor | `anchor->screen()` is used, so the clamp stays on the monitor the anchor is on rather than the primary screen. |
| Offscreen/minimised window, high DPI, panel/taskbar/menu-bar/dock | `mapToGlobal` and `QScreen::availableGeometry` use the same device-independent coordinate space in Qt 5.15 and Qt 6; a panel or dock reduces the available area and the clamp honours it. |
| Wayland | `move()` on a tooltip/popup is still the positioning API the previous `moveGlobal` used; the clamp is best-effort under a compositor that constrains popups. No new platform assumption. |
| Qt 5.15 / Qt 6, MSVC, macOS, clang windows-arm64 | `QWidget::screen()` exists since Qt 5.14; `std::clamp` is C++17 and the project is C++20; no platform-specific headers, macros or APIs. Only Linux was built here; the other CI platforms are checked by reading the code, not by a local run. |
| Audio thread, undo, commands, saved data | Not touched: this is a GUI placement function called from GUI code; no allocation/lock/wait/logging was added to any audio path, no command or undo step changed, no save format changed. |

## Regression test

`tests/src/gui/GrooveMenuTest.cpp:176` `aMessageForTheMainWindowIsShownOverIt`. It runs the real application, finds
Help > Accessibility Check in the main window's menu bar, triggers it, and checks that the new `TextFloat` lies within
the main window's frame and on the screen. It also checks that a message whose parent is a control is still placed at
`control global pos + (width + 2, 0)`. The test is deterministic, with no sleeps or timing: the float exists
synchronously after `trigger()`.

Test matrix:
- Normal path: window-parent report is over the window and on the screen; control-parent message is at the control's right.
- Failure/exhaustion paths: null screen (guard returns unchanged), oversized float (clamped to top/left), offscreen or
  minimised anchor (global position is clamped back to the available area).
- Reconfiguration: anchor screen selection follows the anchor on a multi-monitor setup; available area changes
  (panel/taskbar) are read at call time.
- Audio-thread constraints: `displayMessage` is GUI-only; the test exercises the GUI path only.
- Supported-platform build requirements: Linux build here; Qt 5.15/Qt 6 API surface checked by reading the code.

## Commands and results (logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-help-accessibility-check.r2/logs)

The logged builds ran with `CCACHE_DISABLE=1` (the round-1 workaround); a control rebuild without it also passed
(log 15, EXIT=0), so ccache is not a blocker in this lane.

| Step | Command | EXIT |
| --- | --- | --- |
| Integration build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > logs/01-build-integration.log 2>&1` | 0 |
| Focused family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > logs/02-test-family.log 2>&1` (from `build/tests`) | 0 (2/2 passed) |
| 4b revert | `git diff HEAD -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > logs/03-build-reverted.log 2>&1` | 0 |
| 4b test (no fix) | `ctest -R '^GrooveMenuTest$' --output-on-failure > logs/04-test-reverted.log 2>&1` | 8 (`over.contains(shownAt)` returned FALSE: report at 1241,2 294x200 is outside the window -2,-2 1241x804) |
| 4b re-apply | `git apply logs/fix.patch` | 0 |
| 4b build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > logs/05-build-restored.log 2>&1` | 0 |
| 4b test (fix) | `ctest -R '^GrooveMenuTest$' --output-on-failure > logs/06-test-restored.log 2>&1` | 0 (1/1 passed) |
| 4b family (fix) | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > logs/06b-test-family-restored.log 2>&1` | 0 (2/2 passed) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/07-feature-check.log 2>&1` | 0: RESULT PASS (8/8 rows: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |
| integration equality | `diff -u logs/08-accepted-source-diff.patch logs/09-applied-diff.patch > logs/10-diff-comparison.log` | 0 (no difference) |
| ccache control build | `cmake --build build --target zene GrooveMenuTest -j4 > logs/15-build-ccache-check.log 2>&1` (no `CCACHE_DISABLE`) | 0 |
| ccache control family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > logs/16-test-after-ccache-check.log 2>&1` | 0 (2/2 passed) |

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt` (a new line for TextFloat.cpp with its reason),
`docs/qwen-fixes-2026-10/cluster-gui-1106-menu-help-accessibility-check.md`.

Not checked: other `ctest` families beyond GrooveMenuTest and PluginShortcutsTest. I did not visually check a real
(non-offscreen) display or a non-Linux build.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Yes. `git apply -R logs/fix.patch`,
  rebuild (EXIT=0) and `ctest -R '^GrooveMenuTest$'` gave EXIT=8 with the `over.contains(shownAt)` assertion
  (`report at 1241,2 294x200 is outside the window -2,-2 1241x804`, log 04). `git apply logs/fix.patch`, rebuild
  (EXIT=0) and the same ctest gave EXIT=0 (log 06); the focused family then passed 2/2 (log 06b).
- **The test is deterministic.** Yes. It uses no sleeps, timers or wall-clock time. `displayMessage` creates and
  places the float synchronously inside `QAction::trigger()`. The test then reads geometry: the new `TextFloat` among
  the main window's children, compared with the window's `frameGeometry()` and the screen's `availableGeometry()`.
  The window geometry is set explicitly. The float's timeout only closes it later and is not used.
- **The fix is at the cause and complete.** Yes. The cause is the placement rule in `TextFloat::displayMessage`
  (`TextFloat.cpp:133-136`), which is where the fix is. Every `displayMessage` caller was checked: only
  `CommandRun.cpp:95` passes a parent, and the rest pass none. Siblings: `SimpleTextFloat` and the knob/fader/clip
  `moveGlobal` floats anchor to controls, not windows, so they are out of scope and unchanged. The header
  (`include/TextFloat.h`) did not change.
- **No collateral damage.** Yes. The change is GUI placement only: no saved data, project format, command, undo or
  audio-thread code changed. The screen clamp moves a float only when it would extend past the screen's available
  area, so no caller that fits is affected. A control's float is still placed at its right (asserted in the test).
  The 2-test family passes (logs 02 and 06b).
- **ALREADY-FIXED or FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED, and the bug was reproduced in
  the RED run (log 04). This integration run adds no code change to the accepted patch; the equality check (log 10)
  shows the integrated product/test/registration diff is byte-identical to the accepted source diff.

Gates: log 07-feature-check.log has RESULT PASS, EXIT=0, with all 8 rows green and no new complexity or file-length
item.

## Round 3: QA note addressed (comment only)

Round-2 QA was ACCEPT-NOTE with no blocking items. Its one note: the `onScreen` doc comment
(`src/gui/widgets/TextFloat.cpp:47`) said the float "stays" on the screen's available area, but a float larger than that
area can't fit there. Its origin is pinned to the area's left/top edge, and it overflows right/bottom. I reworded the comment at
`TextFloat.cpp:47-48` to say exactly that. There is no code change: the clamp expressions at `TextFloat.cpp:54-55` are
byte-identical, so placement and the regression test behave as before.

Logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-help-accessibility-check.r3/logs`. Logs 01-06 are a
first attempt in which ccache could not create its temp file (`ccache: error: failed to create temporary file ...
Permission denied`, log 01). The build failed (EXIT=2), and ctest reported the tests as "Not Run" (EXIT=8). That run
says nothing about the fix. Every build below ran with `CCACHE_DISABLE=1`.

| Step | Command | EXIT |
| --- | --- | --- |
| build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > logs/07-build.log 2>&1` | 0 |
| family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > logs/08-test-family.log 2>&1` (from `build/tests`) | 0 (2/2 passed) |
| 4b revert | `git diff af010bc3cc -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b build | `cmake --build build --target zene GrooveMenuTest -j4 > logs/09-build-reverted.log 2>&1` | 0 |
| 4b test (no fix) | `ctest -R '^GrooveMenuTest$' --output-on-failure > logs/10-test-reverted.log 2>&1` | 8 (`'over.contains(shownAt)' returned FALSE. (report at 1241,2 294x200 is outside the window -2,-2 1241x804)`) |
| 4b re-apply | `git apply logs/fix.patch` | 0 |
| 4b build | `cmake --build build --target zene GrooveMenuTest -j4 > logs/11-build-restored.log 2>&1` | 0 |
| 4b family (fix) | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > logs/12-test-restored.log 2>&1` | 0 (2/2 passed) |
| gates (as given) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/13-feature-check.log 2>&1` | 1: `PermissionError` writing its per-gate logs to `<worktree>/../<name>-feature-check/` (`feature_check.py:70`), which is outside this lane's write-jail. No gate ran. |
| gates (log-path copy) | `python3 -I logs/fc-script/feature_check.py <worktree> > logs/15-feature-check.log 2>&1`. This copy is the original with two lines changed (`logs/14-feature-check-copy.diff`): `ROOT` is the real `~/.local/share/zene-bunny-fix/qwen` (same baselines and FIX-BASE), and the per-gate log folder is `logs/feature-check`. The gate commands, limits and baselines are unchanged. | 0: RESULT PASS, 8/8 (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Files committed in round 3: `src/gui/widgets/TextFloat.cpp` (comment only),
`docs/qwen-fixes-2026-10/cluster-gui-1106-menu-help-accessibility-check.md`.
