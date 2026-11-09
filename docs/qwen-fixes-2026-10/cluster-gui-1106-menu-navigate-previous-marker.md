FIXED

# cluster gui-1106-menu-navigate-previous-marker: Navigate refusal notices are placed outside the main window

## Integration (this round)

Source `bugs/qwen-gui-1106-menu-navigate-previous-marker` @ `1400f46e7bd106f2d9f9574810cec3a7c57a33da` onto target
`050/m0-train-26` @ `af010bc3cc055d477045a54e2d866f59a95017d7`. The accepted fix was written on top of this same target
(`git merge-base HEAD 1400f46e` = `af010bc3cc`), so base == ours and the three-way merge has no content to conflict. The
driver's merge fell back to direct application and reported each file applied cleanly
(`integration-apply.log`: "Falling back to direct application... Applied patch to ... cleanly"). Reproduced independently:
`git merge-file -p --diff3` on `ours=af010bc3cc:src/gui/widgets/TextFloat.cpp`, `base=af010bc3cc:...`,
`theirs=1400f46e:...` gave EXIT=0, zero conflict markers, and a result byte-identical to the accepted file (`diff` EXIT=0,
`logs/merge-check/`). `git diff 1400f46e --stat` for the working tree (06-tree-vs-accepted.log) and the index
(07-index-vs-accepted.log) is empty: the integrated tree is exactly the accepted tip. No conflict resolution or redesign was
needed; the accepted design and its regression test are retained verbatim. Round-1 QA on the accepted patch returned ACCEPT
with no blocking entries, so there were no rework points to address.

## Verdict and evidence (REAL)

- `src/gui/MenuBarExtras.cpp:96`: Navigate > Previous Marker is `addCommandAction(menu, ..., "marker.jump", ...)`.
- `src/gui/CommandRun.cpp:128`: the action runs `runCommand(hostOf(menu), ...)`. `hostOf` (`CommandRun.cpp:49-53`) climbs past the
  QMenu parents, so it reaches the menu's parent: the main window (`MenuBarExtras.cpp:67`, `bar->window()`).
- `src/gui/CommandRun.cpp:95-96`: `announce` passes that parent to `TextFloat::displayMessage` (the only 5-argument caller in
  `src/`; every other call passes no parent).
- `src/gui/widgets/TextFloat.cpp:111-114` (before the fix): any non-null parent is placed at `QPoint(parent->width() + 2, 0)`,
  which is right of the parent. That suits a knob or button, but for a whole window it is past the right edge and cannot be seen.
  The RED run measured this: `refusal at (1604,2), window spans 2,2 1600x1000`.

## Affected failure paths (all callers that hand `displayMessage` a window)

- `src/gui/CommandRun.cpp:95-96` is the only caller that passes a parent. It is reached from `CommandRun.cpp:128`
  (`addCommandAction`: every menu command, host = `hostOf(menu)` = the menu's window) and `CommandRun.cpp:150`
  (`addCommandChoices`: submenu items, host = `hostOf(sub)`).
- `runCommand` callers that pass a window: `src/gui/shell/InspectorPanel.cpp:74` (`from->window()`),
  `src/gui/shell/InspectorChannelRouting.cpp:87,96` (`parent->window()`, `button->window()`),
  `src/gui/AafImportMenu.cpp:60`, `src/gui/LayoutMenu.cpp:100,120`, `src/gui/WorkspaceMenus.cpp:104`
  (`QApplication::activeWindow()`), `src/gui/LibraryCommandMenus.cpp:140` (`hostOf(menu)`),
  `src/gui/WorkflowRecordingIndicator.cpp:74` (`window()`), and the dialogs that pass `this` (a top-level window):
  `CommandListDialog.cpp:359`, `CrossfadeEditorDialog.cpp:301`, `SampleEditorDialog.cpp:351,371`,
  `WorkflowRunReport.cpp:143`, `FollowActionEditor.cpp:253`.
- Every other `displayMessage` call passes no parent (4 arguments) and keeps the unchanged no-parent path. Every caller that
  passes a child widget (`PianoRoll`, `ClipView`, `TimeLineWidget`, `DeviceCard`, ...) keeps the old "to its right" placement,
  because `isWindow()` is false for them.

## Supported-platform hazards

`QWidget::isWindow()` exists in Qt 5.15 and Qt 6; the change adds no include and no new API, so it compiles on MSVC, macOS and
clang exactly as the accepted patch did. Nothing is allocated, nothing runs on the audio thread, no command is added, and no
saved-data format changes.

## Fix (at the cause, `src/gui/widgets/TextFloat.cpp:111-121`)

When the parent is a whole window (`parent->isWindow()`), the message now uses the same lower-left placement that the no-parent
path already used, applied to that window. A parent that is a normal child widget still gets the old "to its right" placement,
so knobs and buttons keep their behaviour. The no-parent path is unchanged. Nothing is saved, nothing runs on the audio thread,
no undo or command change, and no new command is added.

Registration: the existing `src/gui/widgets/TextFloat.cpp` line in `tests/upstream-modifications.txt` (one line per file) now
also gives this reason, and `tests/src/gui/GrooveMenuTest.cpp` is registered with its reason.

## Regression test

`tests/src/gui/GrooveMenuTest.cpp`, `aRefusalFromTheMainMenuIsPlacedInsideTheMainWindow`: it runs the real application, finds the
real `navigateMenu` on the main window and triggers Previous Marker in a fresh project with no markers. It then checks that the
new TextFloat is visible and that its top-left corner is inside the main window's global rectangle. It has no sleeps and no
timing. `qWaitForWindowExposed` only waits for the window to be shown, the same as `PluginShortcutsTest`.

## Commands and EXIT codes (logs in ~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-previous-marker.r2/logs)

| Step | Command | EXIT |
| --- | --- | --- |
| product patch | `git diff --cached -- src/gui/widgets/TextFloat.cpp > logs/fix.patch` (the driver staged the patch, so `--cached`) | 0 |
| 4b revert | `git apply -R logs/fix.patch` | 0 |
| RED build | `cmake --build build --target zene GrooveMenuTest -j8 > 01-build-red.log` | 0 |
| RED test | `ctest -R '^GrooveMenuTest$' --output-on-failure > 02-test-red.log` (from build/tests) | 8: new test FAIL `refusal at (1604,2), window spans 2,2 1600x1000`; 4 other functions PASS |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| GREEN build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j8 > 03-build-green.log` | 0 |
| GREEN family | `ctest -R '^(GrooveMenuTest|PluginShortcutsTest)$' --output-on-failure > 04-test-green.log` | 0 (100% of 2 tests passed) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 05-gates.log` | 0: RESULT: PASS, all 8 rows (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |
| tree vs accepted | `git diff 1400f46e --stat > 06-tree-vs-accepted.log` | 0: empty |
| index vs accepted | `git diff --cached 1400f46e --stat > 07-index-vs-accepted.log` | 0: empty |
| 3-way merge check | `git merge-file -p --diff3 ours base theirs > merged.cpp`; `diff theirs merged.cpp` | 0; 0 (identical, no conflict markers) |

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`, `tests/upstream-modifications.txt`,
`docs/qwen-fixes-2026-10/cluster-gui-1106-menu-navigate-previous-marker.md`.

Not checked: the full ctest suite (only the GrooveMenuTest/PluginShortcutsTest family named for this file was run); whether the
message is visible on a real display (offscreen platform only).

## Self-check

- **Fails without the fix, passes with it:** with `git apply -R logs/fix.patch` and a rebuild, `ctest -R '^GrooveMenuTest$'` gave
  EXIT=8 (`refusal at (1604,2), window spans 2,2 1600x1000`, 02-test-red.log). With `git apply logs/fix.patch` and a rebuild it
  gave EXIT=0 (04-test-green.log, 2/2).
- **Deterministic:** there is no wall-clock time, no sleep and no race. `QAction::trigger()` runs `marker.jump` and `displayMessage`
  synchronously. The test picks out the new TextFloat as the one missing from the `findChildren` list taken before the trigger,
  and checks its geometry right away. The geometry comes from the fixed offscreen-platform layout (window at 2,2, 1600x1000).
  The only wait is `qWaitForWindowExposed`, a show-synchronisation hook, not a timeout the assertion depends on.
- **At the cause and complete:** the placement rule in `TextFloat::displayMessage` (`TextFloat.cpp:111-121`) is the only shared
  place where a parent's position is turned into the message's position, so every caller gets the fix at once: all
  `addCommandAction`/`addCommandChoices` menu items (`CommandRun.cpp:128,150`), the inspector and active-window callers listed
  above, and any dialog or detached editor window. Callers that pass a child widget (`PianoRoll`, `ClipView`, `TimeLineWidget`,
  `DeviceCard`, ...) still get the old "to the right of the widget" placement, because `isWindow()` is false for them. The
  no-parent path is byte-identical. Siblings: `FloatModelEditorBase.cpp:137`, `Fader.cpp:544` and `SongEditor.cpp:568-627` call
  `moveGlobal` on knobs and sliders, which are child widgets, not windows, so they do not have this problem and were left as they
  are. `SimpleTextFloat` has no `displayMessage`.
- **No collateral damage:** only where a floating message appears on screen changes, and only when its parent is a top-level
  window. No project or saved data, setting, command, undo step or audio path is touched, and no limit or check was added.
  GrooveMenuTest (5 functions) and PluginShortcutsTest both pass (04-test-green.log, EXIT=0).
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** does not apply. The verdict is FIXED, and the bug was reproduced as RED first.
- **Gates:** the original `feature_check.py` ran to completion this round (EXIT=0, RESULT: PASS, 05-gates.log); the round-1
  write-jail issue creating `<worktree>-feature-check/` did not recur.
