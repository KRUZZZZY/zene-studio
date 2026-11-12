FALSE-POSITIVE

# gui-1030-menu-view-note-editor - "Note Editor stays visible after its View toggle"

## Verdict and evidence

**The View > Note Editor switch works as designed; the sweep's expectation was wrong.** The sweep (`terra-pilot/buttons/runs/m0-train-24`, id `menu-view|menu item|Note Editor`, shots `0221-pair.png`) expected "The Note Editor panel closes", but the Note Editor was **not open** before the click:

- The "before" frame of `shots/0221-pair.png` shows the **Mixer** in the lower region (Compact/Expanded, "Show: All Channels", Master strip, EFFECTS CHAIN) and the tab strip `Mixer | Note Editor | Devices | Automation | Pattern Editor` with the Note Editor tab not selected. The bottom region holds one panel at a time (`src/gui/shell/ShellLayout.cpp:239-253` `open()` inserts into `openPanel[regionKey(home)]`, replacing the previous one; `ShellLayout.cpp:209` `isVisible()` compares that slot to the id), so with the Mixer there the Note Editor was not visible.
- The View menu item is the shell's own checkable QAction (`src/gui/shell/Shell.cpp:262-279`, `src/gui/MainWindow.cpp:1385`) that dispatches `window.toggle` (`src/gui/ControlCommandsWindow.cpp:198`, handler `toggle` at `:132`) -> `MainWindow::togglePanel` (`MainWindow.cpp:1274`) -> `Shell::togglePanel` (`src/gui/shell/Shell.cpp:396-408`) -> `ShellLayout::toggle` (`ShellLayout.cpp:281`): visible -> `close()`, otherwise `open()`. One click on a non-visible panel therefore **opens** it.
- The "after" frame shows exactly that: the Note Editor tab is selected and the piano roll is shown in the lower region, i.e. the toggle did its job. (The Clip Launcher appearing in the centre came from the earlier sweep step on the Launcher item.)
- The only case where a click leaves the Note Editor visible is the Edit view, where it is the non-hideable centre (`ShellLayout.cpp:256-262`, documented in the `window.toggle` description); the finding's own text says this does not match the observed lower-region state.
- Luna's root-cause text already says no source-defined defect matches and a trace is needed. The trace is the "before" screenshot above.

No product code changed. Intended behaviour is unchanged.

## Regression guard (characterization, passes before and after - nothing to fix)

`tests/src/gui/ShellTest.cpp` `theNoteEditorSwitchOpensWhenAnotherPanelHoldsTheLowerRegionAndClosesWhenItDoesNot`: Mixer takes the lower region -> Note Editor switch is unchecked / not visible -> toggle opens it (checked, mixer parked) -> second toggle closes it and collapses the region -> in the Edit view the centre Note Editor stays visible. This pins the contract so a real regression in the toggle would be caught. Since there is no product bug, there is no RED state and step 4b (revert-the-fix) does not apply; it is a guard, not proof of a fix.

## Commands and results

| What | Command | EXIT |
| --- | --- | --- |
| Build (first try) | `cmake --build build --target ShellTest -j4 > logs/build-1.log` | 2 - ccache cannot write its temp dir under `~/.local/share/zene-bunny-fix/dsh/home` (Permission denied; environment, not code) |
| Build | `CCACHE_DISABLE=1 cmake --build build --target ShellTest -j4 > logs/build-2.log` | 0 |
| Test via ctest (from build/tests) | `ctest -R '^ShellTest$' --output-on-failure > logs/test-1.log` | 0 (1/1 passed) |
| Whole family binary | `QT_QPA_PLATFORM=offscreen ./ShellTest > logs/test-2.log` | 0 (25 passed, 0 failed) |
| RED / 4b | not applicable: no product change, the guard passes on the unmodified code (FALSE-POSITIVE) | - |
| Gates | `feature_check.py` copy with logs redirected into the log folder (the original writes `<worktree>-feature-check/` outside the write jail: PermissionError, exit 1; first run in `logs/gates-1.log`) -> `logs/gates-2.log` | 0, all 8 rows PASS |

`ShellHomesTest` and `AccessibilityHelperTest` were not run: they were not touched and no product file changed.

## Files committed

- `tests/src/gui/ShellTest.cpp` (one new test)
- `docs/qwen-fixes-2026-10/cluster-gui-1030-menu-view-note-editor.md`

## Self-check

- **Regression FAILS with the fix reverted / PASSES with it:** not applicable, there is no product change (FALSE-POSITIVE). The guard test passes on the unmodified code (ctest exit 0, `ShellTest` 25 passed). It is a characterization test and is not claimed as proof of a fix.
- **Deterministic:** yes. It drives `Shell::togglePanel` and reads `ShellLayout` state directly; no timers, sleeps, waits or threads, and no wall-clock input.
- **Fix at the cause / siblings:** nothing changed in product code. The other paths into the same toggle (footer buttons and region tabs `ShellRegionTabs.cpp:202`, Alt+E shortcut, `window.toggle`) all dispatch the same `window.toggle` -> `Shell::togglePanel`, so they share the behaviour shown above.
- **Collateral damage:** none, only a test was added. Saved layouts and projects are untouched.
- **FALSE-POSITIVE evidence:** the sweep's "before" frame (`terra-pilot/buttons/runs/m0-train-24/shots/0221-pair.png`) shows the Mixer in the lower region, so the Note Editor was not open; `ShellLayout::toggle` (`ShellLayout.cpp:281`) opens a non-visible panel, and the "after" frame shows it open. The contract is the `window.toggle` description in `ControlCommandsWindow.cpp` ("showing it hides the region's previous panel").
- **Gates note:** `submit_check.py` calls `feature_check.py`, which writes to `<worktree>-feature-check/` outside this lane's write area (PermissionError), so its own run reports FAIL for that reason only. The same gates run through a copy with logs redirected: all 8 rows PASS, exit 0 (`logs/gates-2.log`).
