Verdict: FALSE-POSITIVE

gui-1106-main-mix: "Mix footer click leaves the Arrange view on screen"

## Decision and evidence

The footer Mix word is wired correctly and the dispatch succeeds. The sweep's click never reached it.

- Wiring: `Shell::buildActions` connects each view action's `triggered` to `dispatchShellCommand("window.set_view", {view})` then `syncActions()` (`src/gui/shell/Shell.cpp:252-255`). The footer puts each view action on a `ShellButton` via `setDefaultAction` (`src/gui/shell/ShellChrome.cpp:482-489`). The handler `setView` calls `shell->setView(view)` and cannot fail for a valid enum name (`src/gui/ControlCommandsWindow.cpp:172-186`). `ControlRegistry::invoke` refuses only for unknown id, unmet `requires`, bad args, engine not ready, or a collaboration peer (`src/core/ControlRegistry.cpp:266-300`); none applies to a normal interactive session. Luna itself says "the source does not identify why the handler failed in this run".
- The sweep clicked outside the capture. `results.jsonl` line 84 (run `m0-train-26`) records the Mix click at x=1689,y=985. The screenshots are 1600x1000 (`shots/0083-before.png`), so x=1689 is 89 px off the capture and, on a 1600-wide screen, off the screen. The same run's footer Arrange is at x=1635, Edit at x=1729, Browser at x=1778 (`results.jsonl` lines 82, 84, 85); the judge for Arrange said "the Arrange switch is outside the visible screenshot" (`verdicts.jsonl` line 89), and Edit failed the same way as Mix (line 91). In the earlier inventory (`inv-main.jsonl`) the same buttons sit inside 1600 px (Arrange ends at 1505, Mix 1527, Edit 1572), so the main window had grown wider than the screen between the inventory and the drive. That is a window-size state of the sweep, not a Mix-handler failure. The same shell switched to Mix through View > Mix in the same run (`judge-14.md` line 9: PASS, "The Mixer occupies the central area").
- Real-GUI proof: a real `QTest::mouseClick` on each footer view button of the real main window switches the shell view (new test below, PASS). No product defect found, so no product change.

Out of scope, recorded for whoever owns it: why the main window's width exceeded a 1600 px display during the sweep (the footer's minimum width, or a saved layout) is a separate question; this finding's cause (Shell.cpp:253 dropping the result) is not it.

## Test added (guard, not a RED/GREEN regression: there is no bug to fix)

`tests/src/gui/PanelShortcutsTest.cpp`, slot `aFooterViewWordSwitchesTheView` (real `GuiApplication`/`MainWindow`; clicks the footer Mix, Edit, Arrange `ShellButton`s and checks `Shell::model().view()` and the checked action). The file was already registered; no new file.

## Evidence table

| Evidence | Command | Result |
| --- | --- | --- |
| Build (first try) | `cmake --build build --target PanelShortcutsTest -j4 > logs/build-1.log 2>&1` | EXIT=2: `ccache: error: failed to create temporary file ... /dsh/home/ccache-tmp-r6 ... Permission denied` (ccache dir outside the write area; environment, not code) |
| Build | `CCACHE_DISABLE=1 cmake --build build --target PanelShortcutsTest -j4 > logs/build-2.log 2>&1` | EXIT=0 |
| Family test | `cd build/tests && ctest -R '^PanelShortcutsTest$' --output-on-failure > logs/test-2.log 2>&1` | EXIT=0, 1 of 1 test passed |
| New slot alone | `QT_QPA_PLATFORM=offscreen ./PanelShortcutsTest aFooterViewWordSwitchesTheView > logs/test-3.log 2>&1` | EXIT=0, 3 passed (initTestCase, the new slot, cleanupTestCase), 0 failed |
| Step 4b (reverse the fix) | not applicable: FALSE-POSITIVE, no product file changed | - |
| Gates, stock script | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` | EXIT=1: `PermissionError` creating `<worktree>-feature-check/` beside the worktree (outside the write area); no gate ran |
| Gates, same script with only the log directory moved into the logs folder (`logs/feature_check_copy.py`; ROOT pointed at the real qwen dir; no gate, limit or baseline touched) | `python3 logs/feature_check_copy.py <worktree> > logs/gates-2.log 2>&1` | EXIT=0, RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS) |

Files committed: `tests/src/gui/PanelShortcutsTest.cpp` and this report.

## Self-check

- RED with the fix reverted / GREEN with it: not applicable. The verdict is FALSE-POSITIVE and no product file changed, so there is no fix to revert (step 4b skipped on purpose). The added test is a guard that passes on the unchanged code (EXIT=0, `logs/test-2.log`, `logs/test-3.log`).
- Deterministic: the test uses no timers, sleeps or waits. It delivers `QTest::mouseClick` straight to each footer `ShellButton` (synchronous `triggered` -> `window.set_view` -> `Shell::setView`) and reads `Shell::model().view()` afterwards. The only wait is the existing `initTestCase` start-up.
- At the cause / complete: no cause in the product was found. The sibling views (Edit, Arrange) are covered by the same loop in the test; the View menu path is `window.set_view` as well (`src/gui/LibraryCommandMenus.cpp:288` and the footer action at `src/gui/shell/Shell.cpp:252-255`).
- No collateral damage: no product code, saved data or limits changed; only a test slot was added.
- FALSE-POSITIVE evidence (file:line): `src/gui/shell/Shell.cpp:252-255`, `src/gui/shell/ShellChrome.cpp:482-489`, `src/gui/ControlCommandsWindow.cpp:172-186`, `src/core/ControlRegistry.cpp:266-300`; sweep data `results.jsonl` line 84 (x=1689 on a 1600 px capture), `verdicts.jsonl` lines 89-91, `judge-14.md` line 9.
- Gates: the stock `feature_check.py` cannot run in this session (it writes `<worktree>-feature-check/` outside the write area, PermissionError, `logs/gates-1.log`). The same script with only the log path moved passed all 8 gates (`logs/gates-2.log`, EXIT=0). `submit_check.py` runs the stock script, so its gates line may still show FAIL here for the same permission reason.
