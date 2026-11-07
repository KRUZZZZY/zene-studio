FALSE-POSITIVE

# cluster gui-1105-main-mixer - "Mixer tab click leaves the Note Editor panel open"

## Verdict
FALSE-POSITIVE: the tab path works; the sweep clicked in the wrong place. No product file changed.

## Evidence
1. The sweep's click points for the bottom-region page tabs in view `main` are all ~242 px left of where
   its own inventory puts those tabs (`terra-pilot/buttons/runs/m0-train-25/results.jsonl` vs `inv-main.jsonl`):

   | tab | click x (results.jsonl) | tab x (inventory) |
   | --- | --- | --- |
   | Mixer | 44 (line 14) | 286 |
   | Note Editor | 141 | 383 |
   | Devices | 245 | 487 |
   | Automation | 348 | 590 |

   The "before" screenshot `shots/0013-before.png` marks the click at the window's left edge, over the empty
   Inspector area, not on the tab strip. The click never touched `QTabBar`, so nothing was "delivered" to
   `onCurrentChanged` (`src/gui/shell/ShellRegionTabs.cpp:213`). The sibling findings (Devices, Automation tab
   clicks leaving the Note Editor open) have the same ~242 px offset.
2. Code path: `QTabBar::currentChanged` -> `ShellRegionTabs::onCurrentChanged` (`ShellRegionTabs.cpp:213`) ->
   `choose` (`:205`) -> `toggle` (`:194`) -> `dispatchWindowToggle` -> `window.toggle`
   (`src/gui/ControlCommandsWindow.cpp:132`, `togglePanel`). The same command serves the footer switch, which
   the same sweep reported as working (results.jsonl line 75, "clicked", no errors).
3. A real mouse click on a tab works. The existing test only called `setCurrentIndex`; I added
   `ShellHomesTest::aMouseClickOnATabOpensItsPanel` (`tests/src/gui/ShellHomesTest.cpp`), which starts from a
   fresh shell (Note Editor open), `QTest::mouseClick`s the Mixer, Devices and Note Editor tabs in turn and
   checks the model, the panel widget and `currentId()`. It passes on the unmodified code (so it is coverage,
   not a RED/GREEN regression: there is no bug to turn RED).

## Commands (all from /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1105-main-mixer)
| step | command | EXIT |
| --- | --- | --- |
| build (ccache temp dir not writable in this sandbox) | `cmake --build build --target ShellHomesTest -j4` -> build-1.log | 2 (ccache: Permission denied) |
| build, ccache disabled | `CCACHE_DISABLE=1 cmake --build build --target ShellHomesTest -j4` -> build-2.log | 0 |
| test (from build/tests) | `ctest -R '^ShellHomesTest$' --output-on-failure` -> test-1.log | 0 (1/1 test target passed; 3 slots incl. the new one, `PASS: aMouseClickOnATabOpensItsPanel`) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> gates-1.log | 1: PermissionError - the script writes `<worktree>-feature-check/` outside the write-jail before running any gate. NOT RUN. |

RED/GREEN (step 4b) does not apply: no product change was made.

## Limits
I did not reproduce in a full `MainWindow` (the test shell is standalone, so `toggle()` takes the
`m_shell->togglePanel` branch, not `dispatchWindowToggle`); the dispatch branch is covered by reading
`ControlCommandsWindow.cpp:132-144` and by the footer switch working in the same sweep. The fix belongs to the
sweep's click-coordinate source (the main-view layout shifted ~242 px between inventory and click), not to the shell.

## Self-check
- Regression FAILS with the product change reverted and PASSES with it: not applicable, no product change (FALSE-POSITIVE).
  The added test passes on the unmodified code (`ctest -R '^ShellHomesTest$'`, EXIT=0). It is extra coverage only.
- Deterministic: yes. It uses `QTest::mouseClick` at `tabRect(index).center()` on an exposed offscreen shell
  (`qWaitForWindowExposed`); no sleeps, timers or wall-clock values.
- Fix complete / siblings: nothing changed. The sibling findings (Devices, Automation tabs) share the same ~242 px
  click offset in `results.jsonl` (x 245/348 vs inventory 487/590); all go through `ShellRegionTabs::choose`.
- Collateral damage: none; only a test was added. Saved data and projects are untouched.
- FALSE-POSITIVE contract: `src/gui/shell/ShellRegionTabs.cpp:205-217` (tab change -> `choose` -> `toggle`),
  `src/gui/ControlCommandsWindow.cpp:132-144` (`window.toggle` -> `togglePanel`), plus the click-vs-inventory table
  above and `shots/0013-before.png` (click marker at the window's left edge).
- Gates: `feature_check.py` could not run here (EXIT=1, PermissionError creating
  `<worktree>-feature-check/` outside the write-jail). Not run, not claimed as passing. The change is a test only (no new
  product function), so the complexity and file-length ratchets cannot be hit by product code.
