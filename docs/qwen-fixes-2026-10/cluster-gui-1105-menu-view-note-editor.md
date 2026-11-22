FALSE-POSITIVE

# gui-1105-menu-view-note-editor: "View menu click leaves the docked Note Editor open"

## Verdict

FALSE-POSITIVE. The View > Note Editor click did exactly what a toggle must do. The sweep's
"expected" text assumed the Note Editor was already open before the click. The evidence shows it was
closed, so the click correctly **opened** it. No product code changed.

## Evidence

State before the click (the sweep's own screenshot, not a guess):

- `terra-pilot/buttons/runs/m0-train-25/results.jsonl`: the View-menu items are clicked in order
  Arranger, Clip Launcher, **Mixer** (shot 0220), **Note Editor** (shot 0221), Devices, ...
- `shots/0221-pair.png`, left (before): the lower region's tab strip shows Mixer as the open panel
  (mixer content: channel strip, "Effects chain"), the Note Editor tab is not selected.
- `judge-*.md`: the neighbouring rows say "Mixer is displayed in the lower region in place of the Note
  Editor", which confirms the Note Editor was closed after the Mixer click.
- `0221-pair.png`, right (after): the Note Editor tab is selected and its panel is open. This is the
  correct result of toggling a closed panel.

Code contract:

- The menu entry is the shell's panel action, `src/gui/shell/Shell.cpp:262-280`. It calls
  `dispatchWindowToggle(id)` (`src/gui/ControlCommandsWindow.cpp:198`), which runs `window.toggle`,
  whose handler (`ControlCommandsWindow.cpp:132-144`) calls `Shell::togglePanel`.
- `Shell::togglePanel` (`src/gui/shell/Shell.cpp:397-409`) calls `ShellLayout::toggle`
  (`src/gui/shell/ShellLayout.cpp:281-292`): visible then close, not visible then open.
- A lower region holds one panel (`ShellLayout::open`, `ShellLayout.cpp:252`), so opening the Note
  Editor replaces the Mixer. That matches the "after" shot.
- Luna's remark that the dispatch result is ignored is true but harmless. `syncActions()` runs right
  after the dispatch (`Shell.cpp:276`) and the checked state follows the layout. The toggle cannot
  fail for the `detail` panel in the Arrange view.
- The one case where a click does not close the Note Editor is by design: in the Edit view it is the
  view's main panel and cannot be hidden (`ShellLayout.cpp:256-262`, the `window.toggle` description in
  `ControlCommandsWindow.cpp:208-219`). That is not the reported situation (the sweep was in the
  Arrange view with the Mixer open).

## Regression test (characterization, passes before and after: no product change)

`tests/src/gui/ShellTest.cpp` `theViewMenuNoteEditorEntryTogglesTheLowerRegion`: with the Mixer open
in the lower region, toggling `detail` opens it and closes the Mixer; a second toggle closes it. This
pins the contract the sweep misjudged. It cannot be RED against a fix because there is no bug, so
step 4b (revert the fix) does not apply.

## Commands run (exact, with exit codes)

| Step | Command | Exit |
| --- | --- | --- |
| first build (ccache temp dir outside the write jail) | `cmake --build build --target ShellTest -j4 > $L/build-1.log 2>&1` | 2 (ccache "Permission denied" on `.../dsh/home/ccache-tmp-r6`, not a compile error) |
| build | `CCACHE_TEMPDIR=$L/ccache-tmp cmake --build build --target ShellTest -j4 > $L/build-2.log 2>&1` | 0 |
| family test (1 test run, "100% tests passed out of 1") | `cd build/tests && ctest -R '^ShellTest$' --output-on-failure > $L/test-1.log 2>&1` | 0 |
| the new function alone | `./ShellTest theViewMenuNoteEditorEntryTogglesTheLowerRegion` | PASS (3 passed, 0 failed) |
| gates, as shipped | `python3 qwen/feature_check.py <worktree> > $L/gates-1.log 2>&1` | 1 (PermissionError creating `<worktree>-feature-check`, a sibling directory outside the write jail) |
| gates, log path redirected | `python3 $L/feature_check_local.py <worktree> > $L/gates-2.log 2>&1` | 0 (8/8 PASS, RESULT: PASS) |

`$L` is the run's log folder. `feature_check_local.py` is a copy of `feature_check.py` that differs
only in `ROOT` (pinned to the real directory, so the baselines are the same ones) and in where the
per-gate logs go (the log folder). The diff is on the first lines of `main()` and is in the log folder.
No gate, limit or baseline was changed.

Test count: ShellTest, 1 ctest test run, 1 passed. Files committed: `tests/src/gui/ShellTest.cpp` and
this report.

## Self-check

- RED with the product change reverted: not applicable. There is no product change and no bug
  (FALSE-POSITIVE). The characterization test passes on the untouched code (`ctest -R '^ShellTest$'`
  EXIT=0).
- Deterministic: yes. It drives `Shell::togglePanel` on a fresh `World` and reads
  `model().isVisible`; no timers, sleeps, threads or clocks.
- Fix complete: nothing was changed. Siblings checked: the footer buttons, region tabs and
  `window.toggle` all go through `Shell::togglePanel` / `dispatchWindowToggle`
  (`ShellRegionTabs.cpp:202`, `ShellFloating.cpp:181`, `Shell.cpp:275`), so they behave the same.
- Collateral damage: none, tests only; no saved data or limit touched.
- FALSE-POSITIVE evidence: see "Evidence" above (`shots/0221-pair.png` before = Mixer open;
  `ShellLayout.cpp:281-292` toggle; `ShellLayout.cpp:252` one panel per region).
- `submit_check.py` gates item: it runs the shipped `feature_check.py`, which exits 1 here with
  PermissionError creating `<worktree>-feature-check` (outside the write jail). The same gates run via
  a log-path-redirected copy gave 8/8 PASS (`$L/gates-2.log`). This FAIL is environmental, not a gate
  result.
