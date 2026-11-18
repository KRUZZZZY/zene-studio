FALSE-POSITIVE

# cluster gui-1105-main-automation - "Automation tab click leaves the Note Editor panel open"

Verdict: **FALSE-POSITIVE.** The sweep's click never reached the tab. No product code changed. A pin test was
added (`ShellHomesTest::aMouseClickOnTheAutomationTabOpensIt`).

## Why the click missed (evidence from the sweep run `terra-pilot/buttons/runs/m0-train-25`)

1. The main view's controls run in one app session (`sweep.py:343`, `RESTART_EVERY = 25` at `sweep.py:56`). In
   `results.jsonl`, rows 8 and 12 of that session are "Pop out the Browser panel" and "Pop out the Inspector panel".
   Both ran before the tab rows 13-17, with no restart between them.
2. After the Inspector was detached, the left region collapsed. The bottom tabs moved left by about 242 px:
   `inv-main.jsonl` (fresh start) puts the Automation tab's centre at x=590, but the live re-locate before the click
   (`sweep.py:352-362`) found it at x=348, y=684.
3. The detached Inspector is a separate window, about 480 px wide, at the screen's top-left. In `shots/0013-before.png`
   and `shots/0016-before.png` it has a "Dock" button, and it covers the whole left strip, including where the
   bottom tabs now sit. The red click box in `shots/0016-pair.png` lands on that floating Inspector window's empty
   body, not on a tab.
4. All the clicks at x ≤ 468 were swallowed the same way: Mixer 44, Devices 245, Automation 348 and Pattern Editor 468.
   Each one was judged FAIL, while the "Note Editor" click (already the selected tab) was judged PASS. The sibling
   clusters gui-1105-main-mixer, -devices and -pattern-editor have this same harness cause.
5. In other views of the same run, clicking these tabs works. The `tab-automation` and `tab-devices` views open by
   clicking the tab (`sweep.py:65-66`), and those views opened. For example, `shots/0128-before.png` shows Devices
   selected and its panel open after the click on row 127.

## The product path (current tree)

- `src/gui/shell/ShellRegionTabs.cpp:109`: `QTabBar::currentChanged` is connected to `onCurrentChanged`.
- `src/gui/shell/ShellRegionTabs.cpp:213-217`: `onCurrentChanged` → `choose(id)` → `toggle(id)` (`:194-203`), which calls
  either `window.toggle` or `Shell::togglePanel`.
- The existing test `tests/src/gui/ShellHomesTest.cpp` (`aTabOpensItsPanelAndTheHideButtonClosesTheRegion`) already
  drives `setCurrentIndex`. The new test drives a real `QTest::mouseClick` on the Automation tab's rect, starting from
  the first-run layout with the Note Editor ("detail") open. It checks the same thing again with the Inspector
  detached, the sweep's exact state.

## Commands and results (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-automation.r1/logs/`)

| step | command | EXIT |
| --- | --- | --- |
| build (ccache temp dir is outside the write-jail) | `cmake --build build --target zene ShellHomesTest -j4 > build-1.log` | 2 (ccache: Permission denied) |
| build | `CCACHE_DISABLE=1 cmake --build build --target zene ShellHomesTest -j4 > build-2.log` | 0 |
| pin test, current code | `ctest -R '^ShellHomesTest$' --output-on-failure > test-2.log` (from build/tests) | 0 (1/1; `-V` in test-3.log: Totals 18 passed, 0 failed, including aMouseClickOnTheAutomationTabOpensIt) |
| mutation: `onCurrentChanged` returns early (`\|\| true`), temporary | `CCACHE_DISABLE=1 cmake --build build --target ShellHomesTest -j4 > build-3-mutant.log`; `ctest -R '^ShellHomesTest$' > test-4-mutant.log` | build 0, test **8** (FAIL `aMouseClickOnTheAutomationTabOpensIt`: `isVisible(automation)` returned FALSE; 16 passed, 2 failed) |
| restored (`git checkout -- src/gui/shell/ShellRegionTabs.cpp`) | `CCACHE_DISABLE=1 cmake --build build --target zene ShellHomesTest -j4 > build-4.log`; `ctest -R '^ShellHomesTest$' > test-5.log` | 0 / 0 (1/1) |
| gates | `python3 -I logs/feature_check_logpath.py <worktree> > gates-2.log` | 0 (8/8 PASS) |

The original `feature_check.py` exited 1 with `PermissionError` (`gates-1.log`). It writes its logs to
`<worktree>/../<name>-feature-check`, which is outside this lane's write-jail. `feature_check_logpath.py` is a
copy that changes only two lines: `ROOT` (pinned to the original folder) and the log directory (set to the LOG
FOLDER). Every gate and baseline is unchanged; `diff` was printed and checked.

## Self-check

- **Regression test fails without the fix and passes with it:** there is no product fix, because the verdict is
  FALSE-POSITIVE. To show the pin test can catch a broken click path, I cut `onCurrentChanged` off with a temporary
  mutation: the test went RED (exit 8). With the code restored it was GREEN (exit 0). See the table above.
- **Deterministic:** yes. It uses `QTest::mouseClick` at `QTabBar::tabRect(i).center()`, and the shell's
  `currentChanged` → `choose` path runs synchronously. There are no sleeps or timers. The only wait is
  `qWaitForWindowExposed`, the same as the neighbouring tests.
- **Fix at the cause, siblings checked:** the cause is in the sweep harness, not the product. Uncleared pop-out
  state from earlier controls in the same session puts a floating Inspector over the bottom tabs. The sibling
  clusters main-mixer, main-devices and main-pattern-editor share this cause (point 4). The harness was not
  changed; it is outside this worktree.
- **No collateral damage:** only `tests/src/gui/ShellHomesTest.cpp` and this report changed. No product,
  saved-data or limit change.
- **Realtime / undo / commands:** nothing was touched.
- **Gates:** 8/8 PASS through the log-path copy. The stock `feature_check.py`, also called by `submit_check.py`,
  cannot write its log folder from this lane (PermissionError, write-jail), so that row may show FAIL for a reason
  that has nothing to do with this change.
