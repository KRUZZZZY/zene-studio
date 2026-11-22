FALSE-POSITIVE

# gui-1105-main-hide-the-bottom-panels: "Hide the bottom panels" leaves the lower area open

Verdict: **FALSE-POSITIVE**. The button works. The sweep clicked a point covered by a floating Browser window that earlier trials in the same app session had popped out.

## Evidence

1. **The click path closes the panel.**
   - `ShellRegionTabs.cpp:122-124` sends `toggle(currentId())`, and `toggle` at `ShellRegionTabs.cpp:194-203` calls `dispatchWindowToggle` when the shell belongs to the main window.
   - `dispatchWindowToggle` is at `ControlCommandsWindow.cpp:198`. Its handler `toggle` (`ControlCommandsWindow.cpp:132-144`) calls `MainWindow::togglePanel` (`MainWindow.cpp:1278`), then `Shell::togglePanel` (`Shell.cpp:397-409`), then `ShellLayout::toggle`, which calls `close` (`ShellLayout.cpp:256-279`) and drops the bottom region's open panel.
   - The first-run Arrange layout opens "detail" below the arranger (`ShellLayout.cpp:57-58`). So the finding's scenario is covered by this path.

2. **A real MainWindow run confirms it.**
   - Probe: a temporary function in `PanelShortcutsTest`, which starts the real GUI through `guitest::startGui`. The probe is reverted and not committed.
   - It resized the window to 1600x1000 and clicked the real "Hide the bottom panels" button with `hide->click()`.
   - Before the click, `openIn(Bottom)` was "detail". After it, `openIn(Bottom)` was "" and `isVisible("detail")` was false (`logs/probe-2.log`).
   - Geometry probes at 1600x1000, 1100x700 and 1920x1080, and with the left splitter at 480, always put the button inside the bottom host (`logs/probe-3.log`, `logs/probe-4.log`).

3. **The sweep's own data shows where the click landed.**
   - The sweep's run (`terra-pilot/buttons/runs/m0-train-25/results.jsonl`) lists the earlier trials in the same app session. The session restarts only every 25 controls (`sweep.py:56`, `RESTART_EVERY`).
     - Trial 8, "Pop out the Browser panel": clicked.
     - Trial 12, "Pop out the Inspector panel": clicked.
     - Trial 18, "Hide the bottom panels": clicked.
   - With Inspector and Browser floating, the bottom host spans the whole window width. The live inventory then placed the button at x=1582, where the first inventory had put it at x=1300. The tab centres had moved too (Pattern Editor at 468 against 710 in the first inventory), and the Pop out button for the Note Editor moved from 1257 to 1539.
   - Those points are under the floating Inspector window (x<480) and the floating Browser window (x>1320).
   - In `shots/0018-pair.png` the Inspector shows its "Dock" text, which is set only when a panel is floating (`ShellFloating.cpp:436-438`). The "after" frame shows "VeSTige" selected in the Browser list, so the click went to the floating Browser window, not the strip.
   - Pattern Editor tab trial 17 (centre x=468 under the floating Inspector) has the same cause. Neither trial tests the strip.

4. **The ignored result Luna names is not what failed.** `dispatchWindowToggle` discards the result, but the probe shows the command succeeds. A refusal would need an interface-less process or a panel the view does not offer, and neither applies to a docked panel in the main window.

## Commands (all run from the worktree unless noted)

| Evidence | Command | Result |
| --- | --- | --- |
| Build of a bare-Shell probe in `ShellHomesTest` | `cmake --build build --target ShellHomesTest -j4 > logs/build-1.log` | EXIT=2: ccache could not write its temp dir (`Permission denied`) |
| Same build, ccache temp dir redirected | `CCACHE_TEMPDIR=<logs>/cctmp cmake --build . --target ShellHomesTest -j4 > logs/build-2.log` (run in `build/`) | EXIT=0 |
| Existing family before any edit | `ctest -R '^ShellHomesTest$' --output-on-failure > logs/test-1.log` (run in `build/tests`) | EXIT=0, 1/1 passed |
| Bare-Shell geometry probe | `./ShellHomesTest probeGeometry > logs/probe-1.log` | EXIT=0; Hide button at (1288,674), inside the host |
| Real-MainWindow probe, build | `CCACHE_TEMPDIR=<logs>/cctmp cmake --build . --target PanelShortcutsTest -j4 > logs/build-3.log` | EXIT=0 |
| Real-MainWindow click probe | `QT_QPA_PLATFORM=offscreen ./PanelShortcutsTest probeHide > logs/probe-2.log` | EXIT=0; after the click the bottom region has no open panel |
| Resize and splitter probes | `build-4.log`, `build-5.log`, then `probeHide` into `probe-3.log`, `probe-4.log` | EXIT=0 each; the button is always inside the bottom host |

Both probes were reverted with `git checkout` and no product or test file changed. No regression test was added, since there is no defect to catch, so there is no RED/GREEN pair and no step 4b patch. The only committed file is this report.

## Open follow-up (not a product defect)

The sweep harness should restart the app, or dock the floating panels, before it tests the bottom strip. As it stands, a click at the edge of a bottom host after Pop-out trials lands on the floating windows. The same applies to the sibling "Pattern Editor tab click does not switch the lower panel" finding from the same run.

## Self-check

- **Regression test fails reverted and passes with the fix:** not applicable. The verdict is FALSE-POSITIVE, so there is no product change and no test; the evidence is the cited code and the real-MainWindow probe (`logs/probe-2.log`, EXIT=0).
- **Deterministic test:** no test was added. The probes used no timing the result depends on; they sized the window, waited 100 ms for layout, and read model state.
- **Fix complete, callers and siblings checked:** no change. The sibling click paths are `choose()` (`ShellRegionTabs.cpp:205`), the list menu (`ShellRegionTabs.cpp:234`), and the footer action (`Shell.cpp:274`). All use `toggle()` or `dispatchWindowToggle`, and the same real-MainWindow path closed the panel.
- **No collateral damage:** nothing changed in product code, so saved layouts and old projects load exactly as before.
- **FALSE-POSITIVE evidence:** the code is `ShellRegionTabs.cpp:122-124`, `ControlCommandsWindow.cpp:132-144` and `ShellLayout.cpp:256-279`. The runtime contract is in the Evidence section above, from the `results.jsonl` trial order and `shots/0018-pair.png`.
- **Gates:** `python3 .../feature_check.py <worktree>` exited 1 (`logs/feature-check-1.log`). It could not start: it creates the sibling directory `zene-qwen-gui-1105-main-hide-the-bottom-panels-feature-check`, which my write jail refuses (`PermissionError`). So the gate was not run, not passed. The committed change is one Markdown file under `docs/`.
- **submit_check.py:** run before the commit (`logs/submit-check-1.log`, EXIT=1). Its failures were the missing commit, the staged-only tree, this Self-check section, and the gate that could not start. The first three are addressed by this commit.
