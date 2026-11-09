FIXED

# cluster gui-1106-menu-navigate-previous-marker: Navigate refusal notices are placed outside the main window

## Integration round 3 (2026-11-09, Claude lane)

Source `integration/gui-1106-menu-navigate-previous-marker-af010bc3cc` @ `a2bd6c038e` onto target `050/m0-train-26` @
`af010bc3cc`. The source is this branch's own tip, whose parent is the pinned target, so there is nothing new to merge.
Round-2 QA (`runs/...r2/qa-session-2.log`) returned `"verdict":"ACCEPT","blocking":[]` — no rework points existed; this
round re-verifies and re-records evidence only. No product or test change was made in round 3.

- `git merge-base --is-ancestor af010bc3cc HEAD` EXIT=0; `git merge-tree --write-tree af010bc3cc a2bd6c038e` EXIT=0 with
  tree `d1d3fa6449b1`, identical to `git rev-parse HEAD^{tree}` — clean merge, no conflict in `TextFloat.cpp`, and no
  semantic difference from the accepted patch (`git diff af010bc3cc HEAD -- src` is exactly the accepted 7-line hunk at
  `src/gui/widgets/TextFloat.cpp:111-121`).

Evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-previous-marker.r3/logs`):

| Step | Command | EXIT |
| --- | --- | --- |
| product patch | `git diff af010bc3cc HEAD -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| RED build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (05-build-red.log) | 0 |
| RED test | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure` from `build/tests` (06-test-red.log) | **8** — `GrooveMenuTest::aRefusalFromTheMainMenuIsPlacedInsideTheMainWindow` FAIL: `refusal at (1604,2), window spans 2,2 1600x1000`; Totals 4 passed 1 failed; PluginShortcutsTest passed |
| reapply | `git apply logs/fix.patch` | 0 |
| GREEN build | same build command (07-build-green.log) | 0 |
| GREEN test | same ctest command (08-test-green.log) | **0** — 100% tests passed out of 2 |
| gates | `feature_check.py <worktree>` (10-feature-check.log) | **0** — RESULT: PASS, all 8 rows PASS |

Environment notes (honest, not patch issues): (1) the first build attempt (01-/03-build-*.log, EXIT=2) failed because the
inherited `CCACHE_TEMPDIR` (`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not writable from this sandbox;
those runs' test results (02-/04-) are discarded. The rerun set `CCACHE_TEMPDIR` to a scratch folder inside the log folder
(deleted afterwards). (2) `feature_check.py:70` writes its logs to a sibling of the worktree, outside this lane's write
jail (09-feature-check.log: PermissionError, EXIT=1, no gate ran). The gate was rerun by exec'ing the unchanged script
source with only that one line's log path replaced by `logs/feature-check`; gate commands and limits were not touched.

## Integration (round 2)

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
- **Round 3 re-verification:** the bullets above cite round-2 logs. Round 3 reproduced them at -j4 in
  `runs/gui-1106-menu-navigate-previous-marker.r3/logs`: RED 06-test-red.log EXIT=8 (same assertion, same geometry), GREEN
  08-test-green.log EXIT=0 (2/2 tests executed), gates 10-feature-check.log EXIT=0 (log path redirected, see "Integration
  round 3"). Committed files this round: this report only; the product/test/registration files are unchanged from `a2bd6c038e`.
