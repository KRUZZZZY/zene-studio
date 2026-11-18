FALSE-POSITIVE

# Cluster gui-1105-main-pattern-editor: "Pattern Editor tab click does not switch the lower panel"

Branch `bugs/qwen-gui-1105-main-pattern-editor`, base `af010bc3cc` (`050/m0-train-26`). No product code changed.
The commit adds one guard test to `tests/src/gui/ShellHomesTest.cpp` and this report.

## Verdict

The tab works. The sweep's click never reached it. Two clicks earlier (sweep step 12, "Pop out the Inspector
panel"), the sweep floated the Inspector into its own top-level window. That window opens where the docked panel was,
over the left part of the main window. With the left region empty, the bottom region and its tab strip moved to
x = 0. The Pattern Editor tab, centre x = 468, was then under the floating Inspector window. The click at
(468, 684) went to that window, not to the tab bar.

## Evidence

### Runtime (the sweep's own artifacts, `~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-25`)

| Fact | Where |
| --- | --- |
| Inventory, before any click: the bottom tabs sit at Mixer x=286, Note Editor 383, Devices 487, Automation 590, Pattern Editor 710 (strip starts after the docked Inspector, ~242 px) | `inv-main.jsonl`, the `page tab` rows with path `... > Note Editor` |
| Sweep step 12 clicks "Pop out the Inspector panel"; its after-shot shows the Inspector header with **Dock** (floating) covering x 0-~480 and the arranger shifted left | `results.jsonl` row 12, `shots/0012-pair.png` |
| The tab clicks after it use x shifted by -242, the old Inspector width: Mixer 44, Note Editor 141, Devices 245, Automation 348, Pattern Editor 468. These are the right positions once the left region is empty | `results.jsonl` rows 13-17 |
| In `shots/0017-before.png` and `-after.png` the only part of the strip still showing, right of the floating window's edge (~x 480), is the tail "Editor" of the Pattern Editor tab. The cursor in the after-shot is at ~(468, 694), on the Inspector window | `shots/0017-*.png` (cropped in the LOG FOLDER: `crop-row-a.png`, `crop-before.png`, `crop-after.png`) |
| The Mixer and Automation tab clicks failed the same way in the same run: the same occlusion | `verdicts.jsonl` lines 32, 35 |

### Code

| Fact | Where |
| --- | --- |
| A popped-out panel's window opens at the docked frame's on-screen position ("a pop-out never sends the panel far from the eye"), so it covers the main window's left area | `src/gui/shell/ShellFloating.cpp:246-261` |
| A floating panel leaves the layout. `apply()` hides a host with no panel, so the left splitter pane collapses and the bottom host moves to x = 0 | `src/gui/shell/Shell.cpp:319-336` (`apply`: a floating panel is skipped at :329, an unused host hidden at :336), `:190-201` (left host and bottom host in the splitters) |
| A tab click calls `choose()`, which toggles the panel open and re-syncs | `src/gui/shell/ShellRegionTabs.cpp:213-217` (`onCurrentChanged`), `:205-211` (`choose`) |

### Measured in this worktree (offscreen, 1600x1000 shell, Inspector detached)

The probe printed the bottom bar's tab rectangles: `Mixer@0 Note Editor@77 Devices@185 Automation@275 Pattern
Editor@383` on a 1008-px bar. Every tab is on the bar and none is scrolled off. A real `QTest::mouseClick` at the
Pattern Editor tab's centre opened `pattern` and made it the current tab (`test-2-probe.log`: `after click "pattern"
true`). Narrowing and widening the shell resets the tab scroll offset (`test-3-probe.log`). So a stale-scroll defect in
the strip is ruled out too.

## Guard test

`ShellHomesTest::aMouseClickOnATabOpensItsPanelWithTheInspectorFloating`
(`tests/src/gui/ShellHomesTest.cpp:180-198`): detach the Inspector, check that the Pattern Editor tab lies inside the
bar, mouse-click its centre, and check that `pattern` is visible, its content is visible and it is the current tab.
The existing test `aTabOpensItsPanelAndTheHideButtonClosesTheRegion` drives `setCurrentIndex`. The new one drives a
real mouse event in the finding's own state, with the left panel floating.

There is no product fix, so the test has no RED/GREEN pair. To show it is not a tautology, I sabotaged the click path
on purpose (`onCurrentChanged` no longer calls `choose`). The test then FAILS at its first behaviour assertion. With
the sabotage reverted, it PASSES.

## Commands and exit codes (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-pattern-editor.r1/logs`)

| Step | Command | EXIT |
| --- | --- | --- |
| Build (ccache temp dir not writable from this lane, so ccache disabled) | `CCACHE_DISABLE=1 cmake --build build --target ShellHomesTest -j4 > build-2.log` | 0 |
| Probe: real click on the tab in a bare shell | `ctest -R '^ShellHomesTest$' -V > test-2-probe.log` | 0 |
| Probe: narrow/widen scroll offsets | `./ShellHomesTest zzProbe > test-3-probe.log` | 0 |
| Guard test build | `CCACHE_DISABLE=1 cmake --build build --target ShellHomesTest -j4 > build-4.log` | 0 |
| Family, via ctest from build/tests | `ctest -R '^ShellHomesTest$' --output-on-failure > test-4.log` | 0 (1/1 ctest, 100%) |
| Guard test alone | `./ShellHomesTest aMouseClickOnATabOpensItsPanelWithTheInspectorFloating > test-5-single.log` | 0 |
| Every function in the family | `./ShellHomesTest > test-6-all.log` | 0 (`Totals: 18 passed, 0 failed`) |
| Sensitivity: click path sabotaged | `./ShellHomesTest aMouseClickOnATabOpensItsPanelWithTheInspectorFloating > test-7-sabotage.log` | 1 (`FAIL! ... 'world.shell.model().isVisible(QStringLiteral("pattern"))' returned FALSE`, ShellHomesTest.cpp:194) |
| Sabotage reverted (`git checkout -- src/gui/shell/ShellRegionTabs.cpp`), rebuilt | `ctest -R '^ShellHomesTest$' --output-on-failure > test-8-restored.log` | 0 (100% of 1) |
| Gates, as shipped | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-1.log` | 1: `PermissionError` on `mkdir <worktree>-feature-check` (`feature_check.py:71`). That directory is outside this lane's write-jail, so no gate ran |
| Gates, the same script with only its log directory moved to the LOG FOLDER (`fc/feature_check_local.py`; the diff from the original is two lines: `ROOT` pinned, `logs` path) | `python3 logs/fc/feature_check_local.py <worktree> > gates-2.log` | 0: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS. RESULT: PASS |

The probe function (`zzProbe`) was temporary. It is not in the commit. The committed test is the guard above.

## Not changed, noted

- The floating window's placement over the main window is by design (`ShellFloating.cpp:246`). The sweep's own
  verdict on the pop-out (`verdicts.jsonl` line 31) is a separate finding, not this cluster.
- The sweep driver: after a click that floats a window, it reuses positions it has re-read, without checking that the
  target is uppermost at that point. That is a harness fix (outside this repository), and it would also clear the
  Mixer and Automation tab failures in the same run.

## Self-check

Answers to `submit_check.py`'s ASK items (`submit-*.log` in the LOG FOLDER):

- **Regression FAILS without the product change, PASSES with it.** There is no product change: the verdict is
  FALSE-POSITIVE. Sensitivity was proved by sabotage instead. Disabling the click path
  (`ShellRegionTabs.cpp:216`) made the test FAIL, EXIT=1, at `ShellHomesTest.cpp:194` (`test-7-sabotage.log`). After
  `git checkout -- src/gui/shell/ShellRegionTabs.cpp` and a rebuild, it PASSES, EXIT=0 (`test-8-restored.log`).
- **Deterministic.** No sleeps, timers or wall-clock waits. `qWaitForWindowExposed` waits for the offscreen expose
  event. The click is a synchronous `QTest::mouseClick` at `QTabBar::tabRect(index).center()`. The shell size is
  fixed at 1600x1000 (`World`, ShellHomesTest.cpp:68-83).
- **Fix at the cause and complete.** No product code was changed. The cause is outside the product: the sweep clicked
  through a window it had floated itself. Siblings checked: the Mixer and Automation tab failures in the same run
  have the same cause (`verdicts.jsonl` lines 32, 35). The other ways into the tab (`choose()` at
  `ShellRegionTabs.cpp:205-211`, the list menu at `:219-239`, the footer switches) were not changed.
- **No collateral damage.** One test function was added. No product file, saved data, limit or command changed
  (`git diff --stat`: `tests/src/gui/ShellHomesTest.cpp`, this report). The whole family passes: 18/18 functions
  (`test-6-all.log`), ctest 1/1 (`test-4.log`, `test-8-restored.log`).
- **FALSE-POSITIVE evidence.** The pop-out places the floating window over the docked frame's old spot
  (`src/gui/shell/ShellFloating.cpp:246-261`). The emptied left host is hidden (`src/gui/shell/Shell.cpp:329`,
  `:336`), so the bottom strip moves under that window. The sweep's own coordinates show this: each tab click is
  shifted by -242 after step 12. So does its after-shot, `shots/0017-after.png`. The click path itself works under a
  real mouse event (guard test, `ShellHomesTest.cpp:180-198`).
- **Gates.** As shipped, `feature_check.py` cannot run from this lane: it creates `<worktree>-feature-check`, which
  is outside the write-jail. `submit_check.py` calls it, so its "gates" row reports FAIL for that reason only. With
  the log directory moved to the LOG FOLDER, the same gates exit 0, all 8 PASS (`gates-2.log`).
- **Build note.** ccache's temp directory (`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not writable
  from this lane (`build-1.log`). Every build after that one used `CCACHE_DISABLE=1`.
