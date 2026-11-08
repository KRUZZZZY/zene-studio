FALSE-POSITIVE

# cluster gui-1106-main-pattern-editor - "Pattern Editor tab click leaves the Note Editor selected"

Verdict: **FALSE-POSITIVE.** The sweep's click never reached the tab, and the product opens the Pattern Editor correctly
from the tab, through the very `window.toggle` path the finding doubts. No product file is changed. One guard test is added.

## Evidence

1. **The click missed the tab.** `terra-pilot/buttons/runs/m0-train-26/results.jsonl` records the click on the `main`-view
   "Pattern Editor" tab at `x=468, y=684`. The same run's inventory (`inv-main.jsonl`) places that tab at
   `x=710..836, y=684` (the same tab clicked in the mixer view was at `x=711`). In the sweep screenshot
   (`shots/0017-after.png`) the shell was in a distorted layout - the Inspector region ran to x=480, the centre was clipped, and
   no tab strip was drawn - and the pointer in that image sits at (468,694), inside the Inspector, left of the bottom region
   (which starts at x=480). A click there is on no tab, so "Note Editor remains selected" is the expected result.
   The same tab click in the other three views (`tab-mixer`, `tab-devices`, `tab-automation`) is PASS in `verdicts.jsonl`.
2. **The path Luna doubted works at runtime.** `ShellRegionTabs::toggle` (`src/gui/shell/ShellRegionTabs.cpp:194-203`) calls
   `dispatchWindowToggle` (`src/gui/ControlCommandsWindow.cpp:198-202`) -> `window.toggle` handler `toggle`
   (`ControlCommandsWindow.cpp:132-144`) -> `Shell::togglePanel` (`src/gui/shell/Shell.cpp:397-409`) -> `ShellLayout::open`
   (`src/gui/shell/ShellLayout.cpp:239-254`). `window.toggle` is `requiresEngine=false` (`ControlGuiSupport.cpp:41`), so it is
   not refused while the engine starts. I launched the real `build/zene` headless on a fresh project (probe in the log folder):
   before, `regions.bottom.open == "detail"`; `window.toggle {"editor":"pattern"}` -> `ok:true, visible:true, panel:"pattern"`;
   after, `regions.bottom.open == "pattern"`. A screenshot of that fresh instance shows the strip with "Pattern Editor" at
   about x=710, y=683 - the inventory's coordinates.
3. `ShellRegionTabs::choose` re-syncs the strip after the toggle (`ShellRegionTabs.cpp:205-211`); that is the intended
   contract ("a refused or unreachable command leaves the tabs saying what is really open"), not a swallowed error - and here
   nothing was refused.

## Test added (guard, not a regression: there is no defect to fail on)

`tests/src/gui/ShellHomesTest.cpp`, `aMouseClickOnThePatternEditorTabOpensIt`: a real `QTest::mouseClick` on the Pattern Editor tab's
own `tabRect` in a fresh shell (Note Editor open) opens the panel, shows its content and selects the tab. The existing tab tests
only called `choose()` / `setCurrentIndex()`. It passes on the unchanged product code, as it must for a false positive.

## Commands and exit codes

| Step | Command | Exit |
| --- | --- | --- |
| Live probe (toggle) | `python3 -I probe1.py build/zene tests` (log folder, `probe1.out`) | 0 |
| Live probe (screenshot) | `python3 -I probe2.py build/zene tests shot1.png` | 0 |
| Build | `CCACHE_TEMPDIR=<logs>/ccache-tmp cmake --build build --target ShellHomesTest -j4 > build-2.log` | 0 |
| Test, from `build/tests` | `ctest -R '^ShellHomesTest$' --output-on-failure > test-1.log` (1 test run, 1 passed; 3 test functions in the binary incl. the new one) | 0 |
| Gates | `python3 feature_check_copy.py <worktree> > gates-2.log` (see note) | 0, RESULT: PASS (8/8) |

Not run, and why: there is no RED/GREEN pair and no fix-revert step (4b), because there is no product change to revert -
the test passes on the original code by design. The first build (`build-1.log`) failed only because ccache could not write its
temp directory outside the sandbox; the rebuild with `CCACHE_TEMPDIR` in the log folder succeeded. The stock
`feature_check.py` (`gates-1.log`, exit 1) could not create its sibling log directory outside the sandbox; I ran an unchanged copy
with only that log path moved into the log folder (`feature_check_copy.py`), so its gate commands and ratchet logic are identical.

Files committed: `tests/src/gui/ShellHomesTest.cpp`, this report. `tests/upstream-modifications.txt` / `fork-sources.txt`
need no entry (the test file is already registered or untracked by those lists - the `fork-sources` and `all-sources` gates pass).

## Open point (out of scope)

The sweep screenshot shows the shell in a layout with the Inspector and centre misplaced after earlier splitter drags in the same
run. If that distorted layout is reproducible from a normal user's drag, it is a separate layout bug; this run did not reproduce
it from a fresh project and the finding's repro (fresh project, left-click the tab) works.

## Self-check

- **Test fails with the change reverted and passes with it:** not applicable - the verdict is FALSE-POSITIVE, there is no product
  change. The guard test passes on the original code (`ctest -R '^ShellHomesTest$'` from `build/tests`, EXIT=0, 1 test run, 1 passed).
- **Deterministic:** yes. No timers or sleeps; one `QTest::mouseClick` at the tab's own `tabRect(index).center()` in a fresh `World`.
  `qWaitForWindowExposed` is the synchronisation point, and the assertions read model and widget state synchronously.
- **Fix at the cause, callers and siblings checked:** no fix made. The callers of `dispatchWindowToggle` were all read
  (`ShellRegionTabs.cpp:202`, `Shell.cpp:275`, `ShellFloating.cpp:181`, `MainWindow.cpp:155,1375`); they share one handler
  (`ControlCommandsWindow.cpp:132-144`), which opens the panel in a live instance (evidence 2 above).
- **No collateral damage:** nothing outside one test function changed; no saved data, command, limit or audio-thread code touched.
- **FALSE-POSITIVE contract and code:** see Evidence 1-3 above - the click point (x=468) is outside the bottom region (starts at x=480)
  while the tab is at x=710 (`inv-main.jsonl`); `ShellRegionTabs.cpp:194-211`, `ControlCommandsWindow.cpp:132-144`,
  `Shell.cpp:397-409`.
- **Gates:** the stock `feature_check.py` cannot create its log directory in this sandbox (`gates-1.log`, exit 1, PermissionError),
  so `submit_check`'s own gate row fails here for that environmental reason. The unchanged copy with the log path moved passes 8/8
  (`gates-2.log`, EXIT=0).
