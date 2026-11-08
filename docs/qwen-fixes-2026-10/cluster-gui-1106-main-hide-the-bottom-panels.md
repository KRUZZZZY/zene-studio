FALSE-POSITIVE

# Cluster gui-1106-main-hide-the-bottom-panels — "Hide button click leaves the lower panel open"

Branch `bugs/qwen-gui-1106-main-hide-the-bottom-panels`, base `af010bc3c` (050/m0-train-26). No product code changed;
one pinning test added to `tests/src/gui/ShellHomesTest.cpp`.

## Verdict: the patrol's click missed the button

The GUI sweep never clicked "Hide the bottom panels". Evidence in
`~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-26`:

- `controls.jsonl` (inventory) puts the button at **x=1300, y=684, w=28** (it spans x 1300-1328), at the right end
  of the bottom region's tab strip.
- `results.jsonl` records the click at **x=1582, y=684**. That is 254 px to the right, past the bottom region and
  into the Browser panel on the right.
- `shots/0018-pair.png` shows the red click marker on the Browser list's "VeSTige" row, not on the bottom strip. The
  "after" frame shows the Browser row hovered/selected; nothing in the bottom region was touched.
- The offset is systematic, not specific to this button. A comparison of inventory centre against clicked x over the
  run's results gives **30 of 270** controls clicked more than a button-width away. They include the same strip's
  "Pin" (inv 1296 → clicked 1696) and "Pop out the … panel" (inv 1257 → clicked 1657), and the footer's
  Arrange/Mix/Edit/Browser (+198 px). The sibling FAIL verdicts in `judge-1.md` (the Automation/Pattern tabs and
  Pop out "do nothing") share this cause.

## The code path is correct (file:line)

- `src/gui/shell/ShellRegionTabs.cpp:122-124`: Hide toggles `currentId()`, the bar's current tab.
- `src/gui/shell/ShellRegionTabs.cpp:175-191` (`sync`): the current tab is set to `model().openIn(region)`. On a
  fresh layout that is `detail` (the Note Editor), as the existing `theStateReportsTheRegionsAndTheFooterFit`
  asserts.
- `src/gui/shell/ShellRegionTabs.cpp:194-203` (`toggle`): with a MainWindow, this dispatches `window.toggle`
  `{editor: "detail"}`. `detail` is in the schema enum (`src/gui/ControlCommandsWindow.cpp:84`, enum built from
  `kEditors` at `:220`), so validation cannot refuse it.
- The handler `src/gui/ControlCommandsWindow.cpp:132-144` calls `MainWindow::togglePanel` (`src/gui/MainWindow.cpp:1278`).
  That calls `Shell::togglePanel` (`src/gui/shell/Shell.cpp:397-409`), the same function the no-MainWindow branch
  calls directly. A docked, offered, non-centre panel is toggled closed and `apply()` hides the bottom host
  (`Shell.cpp:333-337`).

Luna's root cause ("the invocation is rejected") is therefore not supported. The only refusal paths are the
`requires` interface check (an interface exists in the GUI) and the schema (`detail` is valid). Neither applies.

## Regression / pinning test

`ShellHomesTest::theHideButtonClosesAFreshNoteEditorRegion` reproduces the finding's exact steps. It starts from the
fresh first-run layout with the Note Editor open in the bottom region. Without choosing a tab first, it left-clicks
"Hide the bottom panels" (`QTest::mouseClick`). It then asserts that:

- `detail` is not visible;
- `openIn(Bottom)` is empty;
- `shellBottom` is hidden;
- the footer's `detail` switch still exists, is enabled and is unchecked;
- the panel reopens through `Shell::togglePanel`, the function the footer switch's `window.toggle` reaches.

The existing `aTabOpensItsPanelAndTheHideButtonClosesTheRegion` covers Hide only after a tab was chosen. This adds the
fresh-project case from the repro.

There is no product fix, so there is no RED-without-fix step: the test passes on the unchanged product code, which
is the evidence for the verdict. Its first run did fail (log 04), but the cause was the test's own last step. It
triggered the footer QAction, which dispatches through the ControlRegistry and needs a MainWindow (none in this
test). All Hide assertions before that step had passed. The step was replaced with the direct `Shell::togglePanel`
call.

## Commands and exit codes (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-hide-the-bottom-panels.r1/logs/`)

| Step | Command | EXIT |
|---|---|---|
| build (01) | `cmake --build build --target zene ShellHomesTest -j4` | 2 — ccache could not write its temp dir (write-jail), not a compile error |
| build (03) | `CCACHE_DISABLE=1 cmake --build build --target zene ShellHomesTest -j4` | 0 |
| test (04) | `cd build/tests && ctest -R '^ShellHomesTest$' --output-on-failure` | 8 — new test's footer-QAction step (no MainWindow in test); Hide assertions passed |
| build (05) | `CCACHE_DISABLE=1 cmake --build build --target zene ShellHomesTest -j4` | 0 |
| test (06) | `cd build/tests && ctest -R '^ShellHomesTest$' --output-on-failure` | 0 — 1/1 ctest, 100% |
| test (09) | `cd build/tests && ctest -R '^ShellHomesTest$' -V` | 0 — `Totals: 18 passed, 0 failed` |
| gates (07) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 — PermissionError creating `<worktree>-feature-check` (outside write-jail) |
| gates (08) | `python3 <run>/feature_check_local.py <worktree>` (same script; only `ROOT` pinned to `qwen/` and the log dir moved into the LOG folder) | 0 — all 8 rows PASS |

## Self-check

- Verdict with evidence: FALSE-POSITIVE. The click coordinates (inventory 1300 vs clicked 1582) and the screenshot
  show the button was not clicked. The code path is cited above.
- Regression test: added. It passes on the unchanged code; with no product change there is no fix.patch to revert.
- Fix at cause / callers / siblings: no product change. The no-MainWindow and the `window.toggle` paths both end in
  `Shell::togglePanel` (cited above).
- Collateral: none. Only a test function was added (ShellHomesTest.cpp is now 474 lines, under 500).
- Realtime / undo / commands: untouched. No new control command.
- Registration: no new files besides this report. ShellHomesTest.cpp is already registered. Gates PASS (log 08).
- Not checked: the sweep harness's coordinate bug itself (terra-pilot) is outside this worktree. It should be fixed
  there so the other 29 offset controls are re-swept.
