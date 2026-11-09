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

## Integration onto 050/m0-train-26 (round 3) - verdict: FIXED, already integrated; no conflict against the pinned target

Round 3 re-queued the same cluster with reason `merge conflict with the newer line in: src/gui/widgets/TextFloat.cpp`.
I traced that reason to its source before touching anything:

- The pinned target is `050/m0-train-26 @ af010bc3cc`. Its merge base with the accepted tip `0ad06e851b` is `af010bc3cc`
  itself, so the target has no change of its own to `TextFloat.cpp`, `GrooveMenuTest.cpp` or `upstream-modifications.txt`.
- `git merge-tree --write-tree --name-only --no-messages af010bc3cc HEAD` (LOGS/01-merge-tree.log) exits 0 and names no conflict
  file; its only output is the merged tree hash `3b9cb0d20806b18f5e95d3906c962dbd83628b65`. There is no conflict against the pinned target.
- The integrated tree is byte-identical to the accepted tip for the product, test and registration files:
  `git diff --exit-code 0ad06e851b..HEAD -- src/gui/widgets/TextFloat.cpp tests/src/gui/GrooveMenuTest.cpp
  tests/upstream-modifications.txt` exits 0 (LOGS/02-product-identity.log). The only difference from the accepted tip is this report.
- The "newer line" is real, but it is not on the pinned target: `050/m0-train-27` carries a *different* fix for the same
  bug (gui-1105) - `keepOnScreen(QWidget*, QWidget*)` at `src/gui/widgets/TextFloat.cpp:50`, with `displayMessage` at
  `:123-144` routing a window parent to the window's lower-left area. Merging the accepted tip into train-27 conflicts
  there: `git merge-tree --write-tree --name-only --no-messages 050/m0-train-27 0ad06e851b` exits 1 and names
  `src/gui/widgets/TextFloat.cpp` and `tests/upstream-modifications.txt` (LOGS/13-merge-tree-train27.log). That is outside
  this job's pinned target (train-26) and outside the accepted design, which the job says to retain, so no conflict
  resolution was needed and none was made. `git merge-base --is-ancestor 0ad06e851b 050/m0-train-27` exits 1 (train-27 does
  not contain the accepted branch), and `git merge-base 050/m0-train-27 HEAD` is `af010bc3cc`.

The accepted design is unchanged: `displayMessage` at `src/gui/widgets/TextFloat.cpp:101-136` keeps the child-widget branch
at `:111-114` (`parent->width() + 2`) and routes a top-level host to its lower-left area at `:115-121`; the caller in the
finding is `src/gui/CommandRun.cpp:95` (`announce()`), reached from the menu through `CommandRun.cpp:111` and
`runCommand(hostOf(menu), ...)` at `:128`.

Failure paths and supported-platform hazards, all together (unchanged from the accepted patch, re-checked on this tree):
- parent = the main window a menu command passes (the finding): the notice lands in the window's lower-left area - proved RED/GREEN below.
- parent = small child widget (knob-like) with room to its right: the old beside-it placement is kept (guarded by `aNoticeBesideAWidgetStaysBesideIt`).
- no parent: the main window's lower-left area, exactly as before.
- timeout > 0: the notice is still deleted on close; the timeout path is untouched.
- Platforms: `QWidget::isWindow()` and `moveGlobal` are portable Qt5/Qt6 API; no platform-specific code. The test reads
  geometry only, under the offscreen QPA, with no window manager.
- Realtime/undo/commands: GUI thread only; no command or undo change, nothing on the audio thread.

Evidence (LOGS = `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-next-clip.r3/logs`):

| step | command | EXIT |
| --- | --- | --- |
| build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > LOGS/03-build.log 2>&1` | 0 |
| family tests | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > LOGS/04-test-family.log 2>&1` | 0 (2/2 tests passed) |
| merge-tree vs pinned target | `git merge-tree --write-tree --name-only --no-messages af010bc3cc HEAD > LOGS/01-merge-tree.log 2>&1` | 0 (no conflict file) |
| product identity | `git diff --exit-code 0ad06e851b..HEAD -- src/gui/widgets/TextFloat.cpp tests/src/gui/GrooveMenuTest.cpp tests/upstream-modifications.txt` | 0 |
| 4b save | `git diff af010bc3cc..HEAD -- src/gui/widgets/TextFloat.cpp > LOGS/05-fix.patch` | 0 |
| 4b revert | `git apply -R LOGS/05-fix.patch` | 0 |
| 4b reverted build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > LOGS/07-build-reverted.log 2>&1` | 0 |
| 4b RED | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > LOGS/08-test-red.log 2>&1` | **8** (`FAIL! : GrooveMenuTest::aNoticeOverTheMainWindowLandsInsideIt() 'hostRect.contains(noticeRect)' returned FALSE. (notice 1602,0 142x76 is outside the window 0,0 1600x1000)`; 1 test, 0% passed) |
| 4b reapply | `git apply LOGS/05-fix.patch` | 0 |
| 4b restored build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > LOGS/10-build-restored.log 2>&1` | 0 |
| 4b GREEN | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > LOGS/11-test-green.log 2>&1` | **0** (2/2 tests passed) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/12-gates.log 2>&1` | 0 (`RESULT: PASS`; fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS) |

The 4b patch is the product diff from the pinned base `af010bc3cc` (the fix is already committed, so `git diff` against
`HEAD` is empty; the base-relative diff is the honest reversal). Test changes were left in place for the RED run.
`CCACHE_DISABLE=1` is there because ccache's temp dir (`.../dsh/home/ccache-tmp-r6`) is outside this lane's write-jail;
the first r2 build failed with EXIT=2 for that reason, not because of the code.

Files committed this round: this report. The integrated patch (`src/gui/widgets/TextFloat.cpp`,
`tests/src/gui/GrooveMenuTest.cpp`, `tests/upstream-modifications.txt`) is already committed as `a1ef10388f`.

## Self-check (round 3, integration)

- **Test FAILS with the product change reverted and PASSES with it:** RED `LOGS/08-test-red.log` EXIT=8
  (`aNoticeOverTheMainWindowLandsInsideIt` FAIL, `notice 1602,0 142x76 is outside the window 0,0 1600x1000`); GREEN
  `LOGS/11-test-green.log` EXIT=0 (2/2). Family `LOGS/04-test-family.log` EXIT=0 (2/2).
- **Deterministic:** no timers, sleeps or event-loop waits. Seam: the test calls
  `TextFloat::displayMessage(..., 60000, window)`, reads `notice->pos()`/`size()` synchronously and closes the notice
  itself; the 60 s timeout is never waited on.
- **At the cause and complete:** the integrated code is identical to the accepted tip (`git diff --exit-code 0ad06e851b..HEAD`
  for the product/test/registration files exits 0). The only `displayMessage` caller in the finding's path is
  `src/gui/CommandRun.cpp:95`; menu commands pass the top-level main window as host (`CommandRun.cpp:128`). Sibling
  knob/fader floats (`moveGlobal(..., width()+2)`) are out of scope as argued in round 1.
- **No collateral damage:** GUI placement only; no saved data, project format, command, undo or audio-thread change; a
  non-window parent and a null parent are placed exactly as before.
- **Conflict:** none against the pinned target `050/m0-train-26` (`git merge-tree` EXIT=0, no conflict file). The newer line
  the driver names is on `050/m0-train-27`, which is not the pinned target and carries a different fix for the same bug.
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; verdict FIXED (RED reproduced on this target).
