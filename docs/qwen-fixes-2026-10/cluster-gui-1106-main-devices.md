FALSE-POSITIVE

# cluster gui-1106-main-devices - "Devices tab click leaves the Note Editor panel open"

Verdict: **FALSE-POSITIVE**. No product file changed. The sweep's click never reached the bottom tab bar. It landed
on the floating Inspector window that the sweep itself had popped out three clicks earlier, and that window covers the
left part of the main window, the tab bar included.

## Evidence (sweep run `terra-pilot/buttons/runs/m0-train-26`)

1. **Click order in the same session** (`results.jsonl` rows 0-17; the sweep restarts only every 25 clicks, `sweep.py:56`):
   - row 8 `Pop out the Browser panel`, row 12 `Pop out the Inspector panel`, then rows 13-17 are the bottom page tabs
     Mixer / Note Editor / **Devices** / Automation / Pattern Editor, clicked at x = 44 / 141 / **245** / 348 / 468, y = 684.
2. **The Inspector window covers those coordinates.** `shots/0012-pair.png`: before the pop-out the main window shows the
   Arranger with the bottom tabs (Mixer ... Pattern Editor) at y ~ 684. Afterwards a floating Inspector window, with
   a **Dock** button (the detached-panel header), covers roughly x 0-480 and y 85-890 of the screen.
   `shots/0015-before.png` (the Devices click) still shows that window over x 0-480. Of the tab bar, only the tail of
   "Pattern Editor" shows at its right edge (the "Editor" text at x ~ 483, y 682). The click at (245, 684) is inside the
   floating Inspector, in empty space (`shots/0015-pair.png`, red box). Before and after are identical.
3. **AT-SPI geometry vs. what is on screen.** `sweep.py:351-362` finds the control again by role/name/description and
   clicks the centre that AT-SPI reports. It does not check whether another top-level window covers that point. So it
   "clicked" a tab that was hidden under another window.
4. **The same handler works in the same build.** `verdicts.jsonl`: the Devices page tab (and every other bottom tab)
   is **PASS** in views `tab-mixer`, `tab-automation` and `tab-pattern` of the same m0-train-26 run, where nothing
   covered the tab bar (`shots/0098-before.png` shows it unobstructed at y 682). In the main view, all four failing
   tab clicks (Mixer, Devices, Automation, Pattern Editor; clusters gui-1106-main-mixer/-automation/-pattern-editor) fail
   the same way. "Note Editor" "passes" only because it was already selected.
5. **Code contract:** `src/gui/shell/ShellRegionTabs.cpp:213-217` (`onCurrentChanged` -> `choose`) and `:205-211`
   (`choose` toggles the panel if it is not visible, then `sync()`s the tabs to the real state). `:194-203` (`toggle`)
   dispatches `window.toggle` through `dispatchWindowToggle` (`src/gui/ControlCommandsWindow.cpp:198`) for the main
   window. The tab-click contract is pinned by `tests/src/gui/ShellHomesTest.cpp:166-167` (`aTabOpensItsPanelAndTheHideButtonClosesTheRegion`:
   `bar()->setCurrentIndex(devices)` -> `model().isVisible("devices")`), and that test passes on this tree (below).

Possible follow-up for the sweep tool (not a product change, outside this worktree): before clicking, check that the
control's centre is not covered by another top-level window (or dock the popped-out panels / restart after a pop-out
click). Otherwise every main-view control under the floating Inspector gets a false FAIL.

## Commands run (worktree root unless noted; logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-devices.r1/logs`)

| Step | Command | EXIT |
| --- | --- | --- |
| build (1st try) | `cmake --build build --target zene ShellHomesTest -j4 > logs/build-1.log 2>&1` | 2 (ccache could not create its temp file outside the write jail: `build-1.log:35`) |
| build | `CCACHE_DISABLE=1 cmake --build build --target zene ShellHomesTest -j4 > logs/build-2.log 2>&1` | 0 |
| test (build/tests) | `ctest -R '^ShellHomesTest$' --output-on-failure > logs/test-2.log 2>&1` | 0 (1/1 test; QtTest totals 17 passed, 0 failed, incl. `aTabOpensItsPanelAndTheHideButtonClosesTheRegion`) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 (PermissionError: the script creates `<worktree>-feature-check` next to the worktree, outside the write jail: `logs/gates-1.log`) |
| gates | same script, unchanged except its log directory points at `logs/feature-check` (run with `exec` of the script source and that one path replaced) `> logs/gates-3.log` | 0 (all 8 rows PASS, RESULT: PASS) |

No product file and no test file changed, so there is no RED/GREEN pair. The FALSE-POSITIVE verdict rests on the run
artefacts and the code/test cited above.

## Self-check

- **Regression test FAILS without the fix / PASSES with it:** not applicable. No product change. The existing
  regression `ShellHomesTest::aTabOpensItsPanelAndTheHideButtonClosesTheRegion` (`ShellHomesTest.cpp:166-167`) already
  pins "a click on the Devices tab opens Devices" and passes (`test-2.log`, EXIT 0, 17/17).
- **Deterministic:** the cited test drives `QTabBar::setCurrentIndex` directly. No timers, no sleeps, no wall clock.
- **Fix at the cause and complete / siblings:** nothing to fix in the product. The sibling clusters gui-1106-main-mixer,
  gui-1106-main-automation and gui-1106-main-pattern-editor are the same occlusion artefact (rows 13, 16, 17 at
  x 44/348/468, all under the floating Inspector at x 0-480). Hiding the bottom panels (row 18, x 1582) is a separate
  control that was not looked at here.
- **No collateral damage:** no source, data or save-format change. Old projects load exactly as before.
- **Code / contract for FALSE-POSITIVE:** `src/gui/shell/ShellRegionTabs.cpp:205-217`, `ShellHomesTest.cpp:166-167`,
  the PASS verdicts for the same tab in three other views of the same run, and `shots/0012-pair.png` / `shots/0015-before.png`
  showing the floating Inspector over the clicked point.
