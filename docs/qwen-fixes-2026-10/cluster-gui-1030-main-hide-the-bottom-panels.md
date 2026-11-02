FALSE-POSITIVE

# cluster gui-1030-main-hide-the-bottom-panels - "Hide button leaves the bottom panel open"

Finding (Luna GUI sweep, 050/m0-train-24): clicking the bottom strip's "Hide the bottom panels" button
(`src/gui/shell/ShellRegionTabs.cpp:120-125`) leaves the Note Editor open. Luna's own root cause says the handler
is not the explanation ("needs investigation at click delivery or command dispatch").

## Decision: FALSE-POSITIVE (the button closes the region; the sweep's click did not land on it)

No product file changed. A regression test was added that guards the behaviour; it passes before and after (there
is no fix, so the mutation run below plays the RED role).

### Code path (read, then proven by the test)

- Click: `ShellRegionTabs.cpp:122-124` -> `toggle(currentId())`. `currentId()` is the strip's current tab
  (`:160-163`); `sync()` points it at the region's open panel (`:177-188`).
- In the real main window the shell is the main window's (`ShellRegionTabs.cpp:194-203`), so `toggle()` calls
  `dispatchWindowToggle(id)` (`src/gui/ControlCommandsWindow.cpp:198-202`) -> command `window.toggle` -> `toggle()`
  (`:132-144`) -> `MainWindow::togglePanel` (`src/gui/MainWindow.cpp:1274-1277`) -> `Shell::togglePanel`
  (`src/gui/shell/Shell.cpp:396-408`) -> `ShellLayout::toggle` -> `close()` (`ShellLayout.cpp:256-278`), which
  removes the region's open panel; `apply()` then hides the region. The Arrange view's main panel is the arranger, so
  the Note Editor (`detail`, home Bottom) is not the protected main panel (`ShellLayout.cpp:258-262`).
- The existing `ShellHomesTest::aTabOpensItsPanelAndTheHideButtonClosesTheRegion` only covers a bare `Shell`
  (`controlMainWindow()` is null there, so `toggle()` takes the `m_shell->togglePanel` branch at `:199`). The
  main-window branch (`:202`) had no test.

### Runtime proof on the real main-window path

New slot `PanelShortcutsTest::theBottomStripHideButtonClosesTheRegion` (`tests/src/gui/PanelShortcutsTest.cpp`;
that test already boots the real `GuiApplication`/`MainWindow` and sets `ControlRegistry::setReady(true)`). It
switches to Arrange, reveals the Note Editor, finds the strip's real "Hide ..." button, delivers a real mouse click
(`QTest::mouseClick`), and checks: `detail` no longer visible, bottom region has no open panel, `shellBottom` hidden;
then the footer's "Note Editor" switch is still there and a click on it reopens the panel. All pass on the
UNCHANGED code, so the finding's expected behaviour ("the lower area collapses while its footer switch remains
visible", tooltip "Closes this area; its switch in the footer opens it again.") is what the product does.

Mutation check (the test is not vacuous): `ShellRegionTabs.cpp:123` was temporarily replaced by `(void) this;`;
the test then FAILED at `PanelShortcutsTest.cpp(438)`: `'!shell->model().isVisible("detail")' returned FALSE.
(the Note Editor stayed open after Hide was clicked)`. The change was reverted with `git checkout --`; no product
file differs from the branch base.

### Why the sweep saw "still open" (what the evidence in the sweep run shows)

Evidence folder `.../terra-pilot/buttons/runs/m0-train-24`:

- The inventory (`inv-main.jsonl`, `controls.jsonl`) puts the bottom Hide button at x=1300,y=684 (28x24) - the same
  place the test measures (global centre (1301,685) at 1600x1000, right end of the bottom strip).
- `results.jsonl` records the click as delivered at **x=1582,y=684** (`sweep.py:362-377` re-finds the control
  live and clicks the live position), 280 px to the right of that button. In `shots/0018-pair.png` that point is
  inside the Browser panel's instrument list, and the "after" frame shows the Browser's "VeSTige" row highlighted:
  the click landed on the Browser, not on Hide. The "before" frame also shows the bottom strip unpainted, i.e. the
  frame was taken with a stale/unsettled paint of the bottom region.
- So the bottom panel "stayed open" because the Hide button was never clicked.

UNVERIFIED: why the live inventory reported x=1582 for that button in the sweep session (I could not run the sweep
harness here - it needs its own X display/AT-SPI session outside this jail). I did not find a product cause: the
strip is 1076 px wide at x=244..1320 in my 1600x1000 run, and the button is where the first inventory put it.
If the sweep reproduces it with a click on the button's own rectangle (1300,684), please send the log and
screenshot; I would treat that as a new finding (NEEDS-CONTEXT), not this one.

## Commands run (worktree; logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-main-hide-the-bottom-panels.r1/logs/`)

The inherited ccache temp dir is not writable from this jail, so builds ran with `CCACHE_TEMPDIR=$TMPDIR`
(`build-2.log` EXIT=2 is that failure before the variable was set on the command).

| step | command | EXIT |
| --- | --- | --- |
| build test (with new slot) | `cmake --build build --target PanelShortcutsTest -j4 > build-1.log 2>&1` | 0 |
| test on ORIGINAL product code | `cd build/tests && ctest -R '^PanelShortcutsTest$' --output-on-failure > test-1-red.log 2>&1` | 0 (1/1; slot PASS - guard, not RED) |
| direct run of the slot incl. footer-switch check | `QT_QPA_PLATFORM=offscreen ./PanelShortcutsTest theBottomStripHideButtonClosesTheRegion > test-5.log 2>&1` | 0 (PASS) |
| MUTANT build (`ShellRegionTabs.cpp:123` no-op) | `cmake --build build --target PanelShortcutsTest -j4 > build-5-mutant.log 2>&1` | 0 |
| MUTANT run | `./PanelShortcutsTest theBottomStripHideButtonClosesTheRegion > test-6-mutant.log 2>&1` | 1 (FAIL at `PanelShortcutsTest.cpp(438)`) |
| revert mutant, rebuild both | `git checkout -- src/gui/shell/ShellRegionTabs.cpp`; `cmake --build build --target PanelShortcutsTest ShellHomesTest -j4 > build-6.log 2>&1` | 0 |
| family on original code | `ctest -R '^(PanelShortcutsTest|ShellHomesTest)$' --output-on-failure > test-7-green.log 2>&1` | 0 (2/2 tests passed) |

### Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` could NOT run (EXIT=1, `gates-1.log`):
it does `mkdir <worktree>-feature-check` beside the worktree and this write jail refuses
(`PermissionError: [Errno 13]`). I ran the same gate scripts directly, in the worktree, logging to the log folder:

| gate | command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (red on the train on purpose) | `bash tests/complexity-gate.sh --check` | 1 - 7 REGRESSION lines, none in `PanelShortcutsTest.cpp` |
| file-length (red on the train on purpose) | `bash tests/file-length-gate.sh --check` | 1 - 26 REGRESSION lines, none in `PanelShortcutsTest.cpp` (469 lines) |

The two red gates were not compared against the base listing (that is what `feature_check.py` does); I only checked
that no listed item names my changed file. Please run `feature_check.py` outside the jail for the official table.

## Files committed

`tests/src/gui/PanelShortcutsTest.cpp` (new slot + includes; the file is already in `tests/fork-sources.txt`, so no
registration change), `docs/qwen-fixes-2026-10/cluster-gui-1030-main-hide-the-bottom-panels.md`.
