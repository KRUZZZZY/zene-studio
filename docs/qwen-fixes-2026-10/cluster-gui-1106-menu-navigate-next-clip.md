FIXED

# gui-1106-menu-navigate-next-clip: refusal notice placed outside the main window

## Integration (round 2): accepted patch on 050/m0-train-26
- Source: `bugs/qwen-gui-1106-menu-navigate-next-clip` @ `0ad06e851b`; target `050/m0-train-26` @ `af010bc3cc`.
- The driver's three-way merge reported a conflict in `src/gui/widgets/TextFloat.cpp`, then fell back to applying the patch directly. All three files applied cleanly (`integration-apply.log`: "Applied patch to 'src/gui/widgets/TextFloat.cpp' cleanly").
- Semantic check: the +/- lines of the staged diff were compared with `accepted.patch` (`diff` of the two change-line sets, logs/staged.patch). They are **identical**. The design and both regression tests are kept unchanged, and no extra resolution was needed.
- Target context checked: `displayMessage` in the target (`src/gui/widgets/TextFloat.cpp:101-136`) already has the 0.5.0 a11y announcement block after the placement code. The patch only touches the placement `if/else` (`:111-121`), so the announcement, timeout and delete-on-close paths are unchanged.

## Verdict: REAL
- Before the fix, `src/gui/widgets/TextFloat.cpp:111-114` put every notice that had a parent at `parent->width() + 2`, level with the parent's top, which is just right of the parent.
- `src/gui/CommandRun.cpp:95` (`announce`) passes its parent straight to `TextFloat::displayMessage`. A menu command's host is the top-level main window, so a refusal notice landed past the window's right edge. Measured: the notice was at x=1602 for a 1600-wide window.

## Fix (src/gui/widgets/TextFloat.cpp:111-121, displayMessage)
When the parent is a top-level window (`parent->isWindow()`), the notice now goes in that window's lower-left area. This is the placement already used when there is no parent, now relative to the given window. A non-window parent keeps the old beside-it placement, and a null parent behaves exactly as before. There is no header, command, audio-thread or save-format change.

## Affected paths and platform hazards (checked together)
- **Callers** (`rg -n 'displayMessage\('`): `CommandRun.cpp:95` gets a top-level host, which is the bug, and is fixed. Callers that pass no parent (`ControllerTemplateMenu.cpp:71`, `MainWindow.cpp:2029`, etc.) are unchanged. Callers that pass a child widget (editor or clip views) keep the old placement, which `aNoticeBesideAWidgetStaysBesideIt` covers.
- **Failure paths:** null parent uses the main window, as before; window parent uses its lower-left area (new); child parent is placed beside it, as before. With `timeout > 0` the notice is still deleted on close.
- **Platforms:** `QWidget::isWindow()` and `moveGlobal` are portable Qt5/Qt6 API, with no platform-specific code. The test only reads geometry under offscreen QPA and does not depend on a window manager.
- **Realtime/undo/commands:** GUI thread only. No new commands, and the ControlCommandsSnapshot is untouched.

## Regression tests (tests/src/gui/GrooveMenuTest.cpp, unchanged from the accepted patch)
- `aNoticeOverTheMainWindowLandsInsideIt`: the notice rectangle must lie inside the main window's rectangle. The test is deterministic: it checks geometry only and has no timers.
- `aNoticeBesideAWidgetStaysBesideIt`: with a child widget as host, the notice keeps the old placement.

## Evidence (log folder: ~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-next-clip.r2/logs)
| step | command | exit |
|---|---|---|
| build (ccache) | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (build-1.log) | 2: ccache `Permission denied` on its temp dir (sandbox), not a compile error |
| build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (build-2.log) | 0 |
| GREEN + family | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure` (test-1-green.log) | 0: 2/2 tests passed |
| 4b revert fix | `git diff --cached -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` (exit 0); rebuild zene+GrooveMenuTest (build-3-reverted.log, exit 0); `ctest -R '^GrooveMenuTest$'` (test-2-red-reverted.log) | **8**: `FAIL! aNoticeOverTheMainWindowLandsInsideIt ... notice 1602,0 142x76 is outside the window 0,0 1600x1000` |
| 4b reapply | `git apply logs/fix.patch` (exit 0); rebuild (build-4-reapplied.log, exit 0); `ctest -R '^GrooveMenuTest$'` (test-3-green-reapplied.log) | 0 |
| gates (stock) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (gates-1.log) | 1: `PermissionError` creating `<worktree>-feature-check` outside the writable sandbox |
| gates (log dir redirected) | `python3 logs/feature_check_local.py <worktree>` (gates-3.log) | 0: all 8 rows PASS (complexity and file-length: no new item in my files) |

`feature_check_local.py` is the stock script with exactly two lines changed (diff in the log): `ROOT` points at the real qwen directory, and the log directory goes to `logs/fc`. The gate commands, baselines and limits are untouched.

Files committed: src/gui/widgets/TextFloat.cpp, tests/src/gui/GrooveMenuTest.cpp, tests/upstream-modifications.txt, this report.

## Self-check
- **RED/GREEN:** with the TextFloat.cpp change reverted, ctest exits 8 and the notice is at 1602,0, outside the 0,0 1600x1000 window. With the change applied, ctest exits 0. Both runs are in the evidence table above.
- **Deterministic:** the test reads widget geometry right after `displayMessage` returns. It uses no sleeps or timers, never waits on the 60 s timeout, and closes the notice itself.
- **Cause and callers:** `displayMessage` is the only code that places a notice at `parent->width() + 2` for a caller's parent. All of its callers are listed above. The other `moveGlobal(..., width()+2)` sites position their own TextFloat next to child widgets and are not affected.
- **Collateral:** placement with a non-window parent or a null parent is unchanged. Nothing is saved, loaded, or run on the audio thread.
- **Integration fidelity:** the change lines are identical to the accepted patch, with no new design.
- **Gates:** the stock script's gate row fails in this sandbox only because of the directory permission noted above. The same gates with the log dir redirected all pass.
