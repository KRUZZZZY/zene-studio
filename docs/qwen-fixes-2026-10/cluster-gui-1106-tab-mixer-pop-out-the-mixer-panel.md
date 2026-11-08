FALSE-POSITIVE

# cluster gui-1106-tab-mixer-pop-out-the-mixer-panel — "Mixer pop-out control leaves the panel embedded"

Branch `bugs/qwen-gui-1106-tab-mixer-pop-out-the-mixer-panel` at base `af010bc3cc`. No product code changed.

## Verdict: FALSE-POSITIVE (the sweep's click missed the button: it landed off the screen)

1. **The click was outside the screen.** The sweep runs Zene in an Xvnc server with a **1600x1000** screen
   (`terra-pilot/buttons/sweep.py:59` `SIZE = "1600x1000"`, passed to `gui.py start --size` at `sweep.py:163`;
   `gui.py` starts `Xvnc -geometry 1600x1000`). There is no window manager. The control record for this button
   (`runs/m0-train-26/results.jsonl` line 103) puts it at **x=1717, y=712**, which is 117 px to the right of the
   last column of the screen. The VNC click at (1717, 712) could not reach the button. The shots confirm this:
   `shots/0102-before.png` and `0102-after.png` are both 1600x1000. The Mixer header's right end (where
   `panelFloat` sits) is cut off at the screen edge, and so is the "EFFECT" column. The before and after images
   are identical.
   The same inventory has other controls off the screen, and every one was judged FAIL or UNSURE: tab-mixer
   "Pin" x=1696, "Routing Matrix..." x=1714, "Add effect" (1655, 1133), "Pan Master" y=1002, "Fader Master" y=1089,
   footer "Mix" x=1689, "Edit" x=1729, "Browser" x=1778. So the main window was laid out larger than the
   1600x1000 screen (with no window manager, nothing clamps it), and this button lies in the part that is off
   the screen.
2. **The pop-out itself works, on the same path the button uses.**
   - The button: `src/gui/shell/ShellFloating.cpp:424` connects `clicked` to `dispatchFloat(id, !isDetached(id))`.
     `dispatchFloat` (`ShellFloating.cpp:220-231`) invokes `window.detach {panel}` on the live app, or calls
     `detachPanel` directly. `detachPanel` (`ShellFloating.cpp:237-265`) reparents the frame into a new
     `Qt::Window` and shows it.
   - Live app, current tree (`build/zene`, offscreen, Dummy audio): `window.set_view {view: mix}` then
     `window.detach {panel: "mixer"}` returned `{"changed": true, "floating": true, "panel": "mixer"}`.
     `window.get_state` then lists the mixer panel with `"floating": true, "visible": true` (it was
     `"floating": false` before). Log: `logs/02-live-probe.log`; script: `logs/probe_detach.py`.
   - Unit coverage that already exists and passes: `tests/src/gui/ShellTest.cpp:264`
     `aFloatingPanelDocksBackIntoItsZone`. It detaches **mixer** from its bottom home (this is the sweep's
     "tab-mixer" layout) and asserts that `floatingWindow("mixer")->isVisible()` and the mixer content are
     visible, then docks it back. `ShellTest.cpp:288` asserts that the panel's `panelFloat` button advertises
     `window.detach` and switches to "Dock" / `window.attach` after a detach.

The finding's own root-cause note says the same: "Source inspection does not explain why the click failed". The
click never reached the control.

## Out of scope, for the owner (not fixed here)
- **Main window larger than a 1600x1000 screen.** In the sweep the main window extends past the screen. The
  inventory has controls at x up to 1778 and y up to 1153. This is a separate question about the window's
  minimum size or its placement without a window manager. It is the likely common cause of the sibling clusters
  that hit off-screen controls: gui-1106-main-mix / -edit / -browser, gui-1106-tab-mixer-add-effect,
  gui-1106-tab-devices-/automation-/pattern-pop-out (x=1657). I did not measure the window's minimum size.
  **Unverified.**
- **On-screen pop-outs judged FAIL** (Note Editor x=1539, Arranger x=1257) are other clusters. Hypothesis, not
  checked: `detachPanel` opens the window exactly where the panel was (`ShellFloating.cpp:248`,
  `frame->mapToGlobal`). With no window manager there is no title bar, so the floating window can look like the
  docked panel in a screenshot.

## Commands run (EXIT codes as seen)
LOG = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-tab-mixer-pop-out-the-mixer-panel.r1/logs`

| Step | Command | EXIT |
| --- | --- | --- |
| Live probe | `QT_QPA_PLATFORM=offscreen timeout 120 python3 LOG/probe_detach.py $PWD > LOG/02-live-probe.log 2>&1` | 0 (`detach` → `floating: true`) |
| Build (ccache first) | `cmake --build build --target ShellTest -j4 > LOG/03-build-shelltest.log 2>&1` | 2 (ccache temp dir outside the write-jail: "Permission denied") |
| Build | `CCACHE_DISABLE=1 cmake --build build --target ShellTest -j4 > LOG/05-build-shelltest.log 2>&1` | 0 |
| Test family | `cd build/tests && ctest -R '^ShellTest$' --output-on-failure > LOG/06-ctest-shelltest.log 2>&1` | 0 (1 ctest, 24 QTest cases passed, 0 failed) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py $PWD > LOG/07-feature-check.log` | 1 (PermissionError: it creates `<worktree>-feature-check` beside the worktree, outside the write-jail) |
| Gates | `python3 LOG/run_feature_check.py $PWD > LOG/08-feature-check.log 2>&1` (the same script, with only its log folder moved into LOG) | 0 (all 8 rows PASS) |

## Self-check
- **The regression test fails with the product change reverted and passes with it:** not applicable. There is no
  product change, because the defect is not in the product. This is a claim-only closure. The evidence is the
  existing test `ShellTest::aFloatingPanelDocksBackIntoItsZone` (`tests/src/gui/ShellTest.cpp:264`, PASS, EXIT 0
  above) and the live `window.detach` probe (EXIT 0, `floating: true`).
- **Deterministic:** no new test. The existing ShellTest uses no timing. The probe uses only the harness's
  bounded waits.
- **Fix at the cause and complete, siblings checked:** nothing changed. The button (`ShellFloating.cpp:424`),
  the key action (`ShellFloating.cpp:210-216`) and the menu or tear-off entries (`ShellTearOff.cpp:405-418`,
  `ShellCommandMenus.cpp:570`) all go through `window.detach` → `detachPanel`, and the probe exercised that path.
- **No collateral damage:** no product files changed, so loading, saving and limits are unaffected.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** the click was at x=1717 on a 1600-wide screen (`results.jsonl`
  line 103; `sweep.py:59`; the shot PNGs are 1600x1000). The working detach path is `ShellFloating.cpp:237-265`
  plus the live probe and `ShellTest.cpp:264`.
