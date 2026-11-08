FALSE-POSITIVE

# cluster gui-1106-main-mixer - "Mixer tab click leaves the Note Editor panel open"

## Verdict: FALSE-POSITIVE (the sweep's click never reached the tab)

The tab handler works. The sweep clicked at x=44, y=684, which is not on the Mixer tab. No product code is
changed. One characterisation test is added for the path that had no test.

## Evidence

1. The click missed the tab. The sweep's own accessibility inventory
   (`terra-pilot/buttons/runs/m0-train-26/inv-main.jsonl`) lists the page tab "Mixer" (showing, enabled) at
   x=286 y=684 w=80 h=26, so its centre is about (326, 697). `results.jsonl` line 14 records the click at
   x=44 y=684. 44 is the tab's centre in the tab bar's own coordinates (about 4 + 80/2), mixed with the
   absolute y. That is my inference from the numbers; I did not find the harness line that does it.
   In `shots/0013-pair.png` the red box and the pointer sit in the blank area of the Inspector on the left,
   not on the bottom tab strip. The footer and the Note Editor panel are identical before and after.
   That matches "zene log: []": the application received no command and refused nothing.
2. The code path is correct. `ShellRegionTabs::onCurrentChanged` (src/gui/shell/ShellRegionTabs.cpp:213) calls
   `choose` (:205), which calls `toggle` (:194). When the shell is the main window's, `toggle` dispatches
   `window.toggle` (ControlCommandsWindow.cpp:198), whose handler (:132) calls `MainWindow::togglePanel`
   (MainWindow.cpp:1278) and then `Shell::togglePanel` (Shell.cpp:397). That runs `ShellLayout::toggle`
   (ShellLayout.cpp:281), which opens "mixer" in the Bottom region and replaces "detail". `choose` then calls
   `sync()` (:209), so the tab bar follows the real state. The Arrange view offers mixer.
   Nothing in that chain can refuse a Mixer request from the Arrange view.
3. Run on the real application. `MainWindowTest::pickingTheMixerTabOpensTheMixerPanel` starts the real
   GuiApplication (`guitest::startGui`) and sets `ControlRegistry::setReady(true)`. It reveals the Note Editor
   and then selects the Mixer tab with `QTabBar::setCurrentIndex`, the same signal a click emits. It checks that
   the mixer is visible, that the Bottom region holds "mixer", and that the selected tab is "mixer". It passes
   on the unmodified product code, so the dispatch path (`main->shell() == m_shell`, the case the existing
   `ShellHomesTest` never reaches because it has no main window) is fine.
   (The earlier `tab-mixer` judge in the same run, judge-7.md, also PASSed for the Mixer panel.)

There is no failing test and no fix, so the "RED then GREEN" steps do not apply. The test is a guard, not
proof of a fix, and I say so plainly.

## Commands and results (logs in `.../runs/gui-1106-main-mixer.r1/logs`)

| Step | Command | Exit |
| --- | --- | --- |
| first attempt: new test target `ShellTabClickTest` | `cmake build` / `cmake --build build --target ShellTabClickTest` | 97 / 2 (bunny-guard refuses configuring; reverted, moved the test into MainWindowTest) |
| build, ccache temp dir was outside the jail | `cmake --build build --target MainWindowTest -j4` (build-2.log) | 2 (ccache permission denied) |
| build with `CCACHE_TEMPDIR` in `$TMPDIR` | `cmake --build build --target MainWindowTest -j4` (build-3.log) | 0 |
| test, unmodified product code | `cd build/tests && ctest -R '^MainWindowTest$' --output-on-failure` (test-2.log) | 0, 1 of 1 passed |
| test, per-slot | `QT_QPA_PLATFORM=offscreen ./MainWindowTest -v1` (test-3.log) | 0, `Totals: 5 passed, 0 failed` |
| gates fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures | run individually, `gate-<name>.log` | all 0 |
| gate complexity `--check` | `bash tests/complexity-gate.sh --check` | 1 (red on the train on purpose; no item in a file I changed) |
| gate file-length `--check` | `bash tests/file-length-gate.sh --check` | 1 (red on the train on purpose; the item is `ArrangerTimelineTest.cpp`, not mine) |

`feature_check.py` itself could not run: it creates `<worktree>-feature-check/` next to the worktree, outside the
write-jail (PermissionError). I ran its eight gate commands by hand with the same arguments, logged in the log
folder, and compared the ratchet output by file name only. I did not compare against the cached base listings.

Registration: no new files and no new commands; `tests/CMakeLists.txt` and the source lists are unchanged.

## Files committed
- `tests/src/gui/MainWindowTest.cpp` (new slot `pickingTheMixerTabOpensTheMixerPanel`, two includes)
- `docs/qwen-fixes-2026-10/cluster-gui-1106-main-mixer.md`

## Note for the sweep harness (not changed here)
Clicks on a QTabBar tab should use the tab's screen rect (the inventory x/y), not the bar-local rect. Other
"page tab" findings from the same run (`main|page tab|...`) are likely the same artifact.
