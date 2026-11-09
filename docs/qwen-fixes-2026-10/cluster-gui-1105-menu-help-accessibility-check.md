FIXED

# Cluster gui-1105-menu-help-accessibility-check: Accessibility Check result is positioned beyond the main window

Integration of the already accepted patch (`bugs/qwen-gui-1105-menu-help-accessibility-check` @ `09817c599`) onto
`050/m0-train-26` @ `af010bc3cc`. Round-1 fix evidence is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r1/logs/`; the
integration evidence below is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/`.

## Integration

- Source diff: 3 files plus this report (`src/gui/CommandRun.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
  `tests/upstream-modifications.txt`). The driver's three-way merge conflicted in `src/gui/CommandRun.cpp`; its
  fallback direct application (`integration-apply.log`) applied all three files cleanly. No conflict marker is left
  (`rg '<<<<<<<|>>>>>>>'` finds none in the changed files).
- Semantic difference from the accepted patch: none. The integrated `announce` (`CommandRun.cpp:88-101`) is the
  accepted design: `QMenuBar` parent -> no parent for `TextFloat::displayMessage`; everything else unchanged. The
  `<QMenuBar>` include is added next to the other Qt includes. `TextFloat.cpp` is unchanged on the train
  (`TextFloat.cpp:111-120` is the same code the fix relies on).
- `tests/upstream-modifications.txt` gained one line for `tests/src/gui/GrooveMenuTest.cpp` (the file ends with a
  newline), after the train's newer lines.

## Verdict: REAL

The path, traced through the current tree:

1. `src/gui/WorkspaceMenus.cpp:165`: Help > Accessibility Check is
   `run(help, ..., "window.accessibility_report", {}, false, true)`. That is a show-result call that runs right away.
2. `src/gui/WorkspaceMenus.cpp:59` → `addCommandAction` (`src/gui/CommandRun.cpp:123-131`), which runs it with
   `runCommand(hostOf(menu), ...)`.
3. `hostOf` (`src/gui/CommandRun.cpp:50-54`) only climbs past `QMenu`s. The Help menu's parent is the main window's
   `QMenuBar`, so the host is the **menu bar**.
4. `runCommand` → `announce(parent, ...)` → `TextFloat::displayMessage(..., parent)` (`src/gui/CommandRun.cpp:96-100`).
5. `TextFloat::displayMessage` (`src/gui/widgets/TextFloat.cpp:111-114`) places a message with a parent at
   `parent->width() + 2` (beside the widget). The menu bar is as wide as the window, so the report lands just past
   the main window's right edge. Measured in the RED run: the report was at x=1604 and the window was 2..1602 wide.

This fixes the `addCommandAction` / `addCommandChoices` main-menu path: show-result runs and refusals pass through
`CommandRun::announce`, and a form opened from one of those entries later calls `announce(parentWidget())`
(`CommandForm.cpp:371`), with the menu bar as that parent.

## Fix (at the cause, `src/gui/CommandRun.cpp` `announce`)

When the widget the message was asked from is a `QMenuBar`, `announce` passes no parent. `TextFloat`'s own
no-parent placement then puts the message over the main window's lower left (`TextFloat.cpp:115-120`). The only
menu bar in the product is the main window's (`Shell.cpp:148-161`). Nothing else changes:
- context menus still float beside the widget they belong to;
- the dialogs that announce with `this` (CommandListDialog, CommandForm) are not menu bars, so they keep their placement;
- `TextFloat::displayMessage` is untouched for its 30 other callers;
- `hostOf` is untouched, so a form opened from a main-menu entry keeps the same parent.

The change is GUI-thread only: no audio-thread code, no undo, no new commands, nothing saved.

## Regression test

`tests/src/gui/GrooveMenuTest.cpp` `aMenuBarCommandsAnswerIsShownOverTheMainWindow`. It shows the real main window,
adds a menu to its `menuBar()` and builds the entry with the same `addCommandAction` call and the same
`CommandCall{"window.accessibility_report", {}, {}, "", false, true}` that `fillHelpMenu` uses. It triggers the
entry and requires the new `TextFloat`'s geometry to lie inside the main window's geometry. The test has no timers
and no sleeps; the only wait is `qWaitForWindowExposed`, as in PluginShortcutsTest. The Shell's real Help menu is
not installed in this test app, so the test builds the entry the same way `fillHelpMenu` does. Its first version,
which looked for the installed action, failed for that reason: `test-1-red.log`.

## Evidence

| Step | Command | EXIT |
| --- | --- | --- |
| build, integrated | `CCACHE_TEMPDIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/ccache-tmp cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/build-1.log 2>&1` | 0 |
| GREEN | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-help-accessibility-check-af010bc3cc/build/tests && ctest -R '^(GrooveMenuTest|PluginShortcutsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/test-1-green.log 2>&1` | 0 - 100% tests passed out of 2 |
| 4b revert | `git diff --cached -- src/gui/CommandRun.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/fix.patch` | 0 |
| 4b rebuild (reverted) | `cmake --build build --target zene GrooveMenuTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/build-2-reverted.log 2>&1` | 0 |
| RED | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-help-accessibility-check-af010bc3cc/build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/test-2-red.log 2>&1` | 8 - `FAIL! aMenuBarCommandsAnswerIsShownOverTheMainWindow() 'over.contains(shown->geometry())' (the report at (1604,2 290x196) is not over the main window (2,2 1600x1000))`, GrooveMenuTest.cpp:201 |
| 4b reapply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/fix.patch` | 0 |
| 4b rebuild + test (reapplied) | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/build-3-reapplied.log 2>&1` (EXIT 0); `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-help-accessibility-check-af010bc3cc/build/tests && ctest -R '^(GrooveMenuTest|PluginShortcutsTest)$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/test-3-reapplied.log 2>&1` | 0 - 100% passed out of 2 |
| gates | `python3 -I /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/feature_check_local.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-help-accessibility-check-af010bc3cc > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/gates-2.log 2>&1` | 0 - all 8 rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Independent QA rerun on the integrated tree:

| Step | Command | EXIT |
| --- | --- | --- |
| build | `CCACHE_TEMPDIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/ccache-qa cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/qa-build.log 2>&1` | 0 |
| focused tests | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-help-accessibility-check-af010bc3cc/build/tests && ctest -R '^(GrooveMenuTest|PluginShortcutsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/qa-ctest.log 2>&1` | 0 - 2/2 passed |
| direct feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-help-accessibility-check-af010bc3cc > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/qa-feature-check.log 2>&1` | 0 - all 8 rows PASS |

The wider 8-test command/menu family was run in round 1 (r1 `test-6-family.log`, 8/8); in this integration I ran the
two tests that cover the changed file (2/2, above), not the other six.

Environment workaround: `feature_check.py` exits 1 here (`gates-1.log`, PermissionError creating the sibling
directory `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-help-accessibility-check-af010bc3cc-feature-check`, outside the write-jail). The local `feature_check_local.py` in the r2 LOG FOLDER is the same script with two
lines changed: `ROOT` pinned to the original's directory (same FIX-BASE and baseline cache) and logs written to
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/gate-logs`. Gate commands and pass rules are unchanged. The direct feature gate also passed during independent QA above.

Files committed: `src/gui/CommandRun.cpp`, `tests/src/gui/GrooveMenuTest.cpp`, `tests/upstream-modifications.txt`, this
report. `CommandRun.cpp` is already in `tests/fork-sources.txt` (a fork file); no new product files.

## Self-check

- **The test fails with the fix reverted and passes with it:** yes. Reverted (`git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/fix.patch`, EXIT 0;
  rebuild EXIT 0): `ctest -R '^GrooveMenuTest$'` EXIT 8, failing on
  `over.contains(shown->geometry())` with the report at (1604,2). Reapplied (`git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r2/logs/fix.patch`, EXIT 0;
  rebuild EXIT 0): EXIT 0, 2/2 passed. Logs: `test-2-red.log`, `test-3-reapplied.log` (r2).
- **The test is deterministic:** no timers, sleeps or wall-clock reads. The entry is triggered synchronously
  (`QAction::trigger` → `runCommand` → `announce` → `TextFloat::displayMessage` all run in that call). The test then
  reads the new `TextFloat`'s geometry at once. `TextFloat::displayMessage` calls `show()` before it computes the
  position (`TextFloat.cpp:109`), so the geometry is final when the call returns. The one wait is
  `QTest::qWaitForWindowExposed`, an event-synchronisation hook, not a timeout race. The 8 s auto-close timer
  never fires during the test, and the test closes the float itself.
- **The fix is at the cause and complete:** the wrong anchor is chosen in `announce` (`CommandRun.cpp:96-100`), the
  one funnel every menu-run message goes through. Checked:
  - `addCommandAction` (`CommandRun.cpp:123`) and `addCommandChoices` (`CommandRun.cpp:150`) both pass `hostOf(menu)`,
    which is the `QMenuBar` for a main-menu entry. Both are fixed by the one change.
  - `CommandForm.cpp:371` announces with `parentWidget()`, which is the menu bar for a form opened from the main
    menu. Fixed by the same change.
  - Other command-run callers pass a dialog or a context-menu host: CommandListDialog `this`
    (`CommandListDialog.cpp:359`), CommandPalette `formParent`/`parentWidget()` (`CommandPalette.cpp:257,422`), and
    `WorkspaceMenus.cpp:102`, which passes `QApplication::activeWindow()`. Their behaviour is unchanged.
  - `RecordLaunchMenus.cpp:86-93` has a separate custom callback that calls `announce(menu->parentWidget())`; those
    menus are created with `bar->window()` as parent (`RecordLaunchMenus.cpp:226-231`), so this `QMenuBar` handling
    does not retarget those refusal messages. That adjacent path is outside the Help > Accessibility Check finding.
  - `list()` (`WorkspaceMenus.cpp:68-72`) already climbs past menus to its own host for dialogs and does not float
    a message.
  - I did not change `TextFloat::displayMessage`: its beside-the-parent placement is right for its 30 other callers,
    which pass the knob or editor a hint belongs to.
- **No collateral damage:** the change only picks the parent for a transient on-screen message. Nothing is loaded,
  saved, undone, limited or sent over the socket. No new control command, so the ControlCommandsSnapshot is
  unchanged. It runs on the GUI thread only. The GrooveMenuTest + PluginShortcutsTest family passed on the integrated
  tree (`test-3-reapplied.log`, EXIT 0, 2/2), and the gates passed (`gates-2.log`, EXIT 0).
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable, the verdict is FIXED (REAL). See "Verdict" above for
  the code path with file:line.
- **submit_check "gates" row:** `submit_check.py` runs `feature_check.py` unmodified. It cannot write its sibling
  log directory from this lane's write-jail (`gates-1.log`: PermissionError, EXIT 1). The same gates, run with
  only the log directory moved, all PASS (`gates-2.log`, EXIT 0).

## Round 3 re-verification (no code change)

The round-3 brief lists no new review point beyond the integration job text, so I re-verified the committed
integration on the same tree (HEAD `804aad43b8`, base `af010bc3cc`) and changed no product or test code.
Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r3/logs/`.

| Step | Command | EXIT |
| --- | --- | --- |
| build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r3/logs/build-1.log 2>&1` | 0 |
| GREEN | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r3/logs/test-1-green.log 2>&1` | 0 - 2/2 passed |
| 4b revert | `git diff af010bc3cc HEAD -- src/gui/CommandRun.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r3/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r3/logs/fix.patch`; rebuild `build-2-reverted.log` | 0; 0 |
| RED | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r3/logs/test-2-red.log 2>&1` | 8 - `'over.contains(shown->geometry())'` (the report at (1604,2 290x196) is not over the main window (2,2 1600x1000)), GrooveMenuTest.cpp:201 |
| 4b reapply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r3/logs/fix.patch`; rebuild `build-3-reapplied.log`; same ctest as GREEN, `test-3-reapplied.log` | 0; 0; 0 - 2/2 passed |
| gates, stock | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r3/logs/gates-1.log 2>&1` | 1 - PermissionError writing the sibling `-feature-check` log directory outside the write-jail (not a gate failure) |
| gates, log dir moved | `python3 -I /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r3/logs/feature_check_local.py <worktree> > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-accessibility-check.r3/logs/gates-2.log 2>&1` (same script, only `ROOT` and the log directory changed) | 0 - all 8 rows PASS |

Semantic difference from the accepted patch: none. `git status` clean after the 4b reapply.
