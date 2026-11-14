FALSE-POSITIVE

# cluster gui-1105-main-devices - "Devices tab click leaves the Note Editor panel open"

**Verdict: FALSE-POSITIVE.** The tab-to-panel path works. The sweep's click never reached the Devices tab: it
landed on the Inspector panel's own floating window. An earlier click in the same sweep had popped that panel out,
and the window covered the bottom tab strip. No product code changed. One guard test was added for the exact
sequence the sweep ran.

## Evidence

The evidence is in `~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-25`.

1. **The sweep popped out both side panels before clicking the tabs.** `results.jsonl` rows 8 and 12 (main view):
   `button "Pop out the Browser panel"` and `button "Pop out the Inspector panel"`. Both were clicked. Rows 13-17 are
   the bottom tabs Mixer, Note Editor, Devices, Automation and Pattern Editor, clicked in the same session with no
   restart.
2. **The tabs moved 242 px left, which is the width of the emptied left region.** The first inventory
   (`controls.jsonl:14-18`) put the tabs at x = 286 / 383 / 487 / 590 / 710. Before each click the sweep
   re-locates the control from the live accessibility tree (`terra-pilot/buttons/sweep.py:351-362`). The live
   positions it then clicked (`results.jsonl` rows 13-17) are x = 44 / 141 / 245 / 348, all shifted by -242.
   That shift is correct: with the Inspector floating, the left dock is empty, so the bottom region starts at the
   window's left edge.
3. **The floating Inspector window covered them.** In `shots/0015-before.png`, x = 0..480 is the Inspector window
   ("INSPECTOR ... Dock | Pin" at the top left). Of the tab strip at y ≈ 684, the only visible part is "Editor" at
   x ≈ 484, the tail of the "Pattern Editor" tab (live centre 710 - 242 = 468, width 126). The red click marker in
   `shots/0015-pair.png` is at (245, 684), inside the Inspector window's empty body. The Devices tab under it never
   received the press.
4. **The same tab passes in every view where nothing covers it.** The sweep restarted for each of these views, so
   nothing was popped out. `verdicts.jsonl:105, 127, 134, 141` all PASS for "page tab Devices" in tab-mixer,
   tab-devices, tab-automation and tab-pattern ("The Devices tab is selected and its panel appears below the
   workspace"). Each was clicked at x = 487 (`results.jsonl` rows 98, 121, 128, 151). The other main-view
   tabs fail the same way (Mixer, Automation, Pattern Editor FAIL; `verdicts.jsonl` main rows). Only Note Editor
   "passes", because it was already open. The cause is the overlap, not anything specific to Devices.
5. **The code path is sound.** `src/gui/shell/ShellRegionTabs.cpp:109` connects `QTabBar::currentChanged` to
   `onCurrentChanged` (`:213-217`). That calls `choose()` (`:205-211`), which opens the panel through `toggle()`
   (`:194-203`) and then `sync()`s. `sync()` blocks the bar's signals and sets `m_syncing`
   (`:179-190`), so only a real user change reaches `choose()`. Detaching a side panel rebuilds no bottom tab:
   `panelsIn()` (`:141-151`) lists only the region's own panels.

Whether a popped-out panel window should open over the main window's controls is a window-placement question
(`Shell::detachPanel`), not this finding. The sweep harness should re-take its inventory, or close floating windows,
after a pop-out click. That belongs to the harness, not the product.

## Guard test (added)

`tests/src/gui/ShellHomesTest.cpp`, `aMouseClickOnATabOpensItsPanelAfterTheSidePanelsPopOut`, replays the sweep's
sequence on a 1600x1000 shell:

- pop out `browser` and `inspector`;
- assert the Devices tab moved left (where the sweep's click went);
- send a real `QTest::mouseClick` on the Devices tab rect;
- assert `devices` is current, visible and mounted, and that `detail` (the Note Editor) closed.

It is deterministic: synchronous `QTest::mouseClick` and `processEvents`, with no sleeps or timers.

There is no product fix, so there is no fix to revert for a red/green run. Instead, a **mutation check** shows
the test catches a broken click path. The check commented out the `currentChanged` connection
(`ShellRegionTabs.cpp:109`), rebuilt, ran the test (FAIL), restored the line (`git apply -R`), rebuilt and ran it
again (PASS).

## Commands and exit codes

Logs are in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-devices.r1/logs/`. `L` is that folder.

| Step | Command | EXIT |
| --- | --- | --- |
| build (1st try) | `cmake --build build --target zene ShellHomesTest -j4 > $L/01-build.log` | 2 (ccache could not write its temp dir in the sandbox; not a compile error) |
| build | `CCACHE_DISABLE=1 cmake --build build --target zene ShellHomesTest -j4 > $L/03-build.log` | 0 |
| family test, unchanged product | `cd build/tests && ctest -R '^ShellHomesTest$' --output-on-failure > $L/04-test.log` | 0 (1/1 test executed, 100% passed) |
| new function alone | `build/tests/ShellHomesTest aMouseClickOnATabOpensItsPanelAfterTheSidePanelsPopOut > $L/05-single.log` | 0 (`PASS ... aMouseClick...`) |
| mutant build (connect commented out, `$L/mutation.patch`) | `CCACHE_DISABLE=1 cmake --build build --target ShellHomesTest -j4 > $L/06-build-mutant.log` | 0 |
| **RED** on mutant | `build/tests/ShellHomesTest aMouseClickOnATabOpensItsPanelAfterTheSidePanelsPopOut > $L/07-mutant.log` | **1**: `FAIL! 'world.shell.model().isVisible(QStringLiteral("devices"))' returned FALSE` |
| restore | `git apply -R $L/mutation.patch` (then `git diff -- src` is empty) | 0 |
| rebuild restored | `CCACHE_DISABLE=1 cmake --build build --target zene ShellHomesTest -j4 > $L/08-build-restored.log` | 0 |
| **GREEN** restored | `cd build/tests && ctest -R '^ShellHomesTest$' --output-on-failure > $L/09-test-restored.log` | **0** (1/1 executed, passed) |
| gates (direct) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1: `PermissionError` creating `<worktree>-feature-check` (a sibling directory outside this lane's write-jail); no gate ran |
| gates | `python3 $L/run_feature_check.py <worktree> $L/feature-check > $L/11-gates.log` (the same script text, only its `logs =` line pointed at `$L/feature-check`) | **0**: all 8 rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Files committed: `tests/src/gui/ShellHomesTest.cpp` and `docs/qwen-fixes-2026-10/cluster-gui-1105-main-devices.md`.

## Self-check

- **The regression test fails with the product change reverted and passes with it (exit codes).** There is no
  product change, because the verdict is FALSE-POSITIVE. Instead, the guard test was mutation-checked: with the
  `currentChanged` connection cut, it exits 1 (`07-mutant.log`); with the source restored, it exits 0
  (`09-test-restored.log`).
- **The test is deterministic.** It uses a synchronous `QTest::mouseClick` on `QTabBar::tabRect()` and
  `QCoreApplication::processEvents()`, with no sleeps, timers or wall clock. Geometry comes from the shell's fixed
  1600x1000 size under `QT_QPA_PLATFORM=offscreen`.
- **The fix is at the cause and complete; callers and siblings were checked.** No product code changed. Other
  routes to the same function: `choose()` is also reached from `ShellRegionTabs::fillList`'s menu entries
  (`:234-237`) and the Hide button (`:122-124`). Both are covered by the existing
  `aTabOpensItsPanelAndTheHideButtonClosesTheRegion`, which still passes. The failure was not specific to Devices:
  the sibling tabs failed the same way for the same reason (Evidence item 4).
- **No collateral damage.** Only a test was added. No product, saved-data or project-load path changed.
- **FALSE-POSITIVE evidence.** The code path is `src/gui/shell/ShellRegionTabs.cpp:109, 205-217`. The run
  artifacts are `results.jsonl` rows 8, 12 and 13-17, `controls.jsonl:14-18`, `verdicts.jsonl:105, 127, 134, 141`,
  `shots/0015-before.png` and `shots/0015-pair.png`, and the harness re-location is
  `terra-pilot/buttons/sweep.py:351-362`.
- **Note on `submit_check.py`.** Its "gates" row runs `feature_check.py` directly, which hits the same sandbox
  `PermissionError`. The equivalent run with the log directory redirected exits 0 (above).
