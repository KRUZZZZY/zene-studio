FALSE-POSITIVE

# Cluster gui-1106-main-automation — "Automation tab click leaves the Note Editor selected"

Finding: GUI sweep `050/m0-train-26`, control "page tab 'Automation' in view main", pointing at
`src/gui/shell/ShellRegionTabs.cpp:202` (`dispatchWindowToggle(id)` in `ShellRegionTabs::toggle`).

## Verdict: FALSE-POSITIVE — the sweep's click never reached the tab strip

The evidence folder (`~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-26`) shows that the
click missed the control. The tab code was not involved.

1. **The click point is not on the Automation tab.** `controls.jsonl:17` records the tab at centre x=590,
   y=684, w=113 (it spans x≈534–646). `results.jsonl:17` records the click at **x=348**, y=684. Every
   main-view tab click is shifted by the same −242 px from its recorded centre:

   | tab (view main) | inventory x (`controls.jsonl`) | clicked x (`results.jsonl`) | verdict |
   |---|---|---|---|
   | Mixer | 286 | 44 | FAIL |
   | Note Editor (already selected) | 383 | 141 | PASS (nothing to change) |
   | Devices | 487 | 245 | FAIL |
   | Automation | 590 | 348 | FAIL (this finding) |
   | Pattern Editor | 710 | 468 | FAIL |

2. **The screenshots show where the click went.** In `shots/0016-before.png` / `0016-after.png`, a pinned
   Inspector panel fills x≈0–480 on the left. The cursor in the "after" shot is at about (348, 692), in the
   Inspector's empty body. No region tab strip is drawn at that point. The "before" and "after" frames are
   identical. The click hit no control at all.

3. **The same tab path works everywhere else in the same run.** Each region tab dispatches through
   `ShellRegionTabs::onCurrentChanged` → `choose` → `toggle` → `dispatchWindowToggle`
   (`src/gui/shell/ShellRegionTabs.cpp:194-217`, `src/gui/ControlCommandsWindow.cpp:198-202`). The judge files
   for the views reached by clicking a tab (`tab-mixer`, `tab-devices`, `tab-automation`, `tab-pattern`)
   give **PASS for all 20 tab clicks**. That includes "Automation in Mixer", "Automation in Devices" and
   "Automation in Pattern Editor", and "Note Editor in Automation" (the reverse switch). Reaching the
   `tab-automation` view at all required this exact tab path to open Automation.
   `window.toggle` accepts `automation` as an editor name (`windowEditorNames()` /
   `src/gui/ControlCommandsWindow.cpp:209-217`, the panel list in the command description).

4. **Unit coverage of the same path.** `tests/src/gui/ShellHomesTest.cpp:156-178`
   (`aTabOpensItsPanelAndTheHideButtonClosesTheRegion`) drives `choose()` and a real tab-bar `setCurrentIndex`
   and checks that the chosen bottom panel opens and the tab follows. It passes on this branch (below).

The finding's own root-cause note says "The source does not show why this invocation is rejected". It is
not rejected: the click never reached the tab bar. The four sibling main-view tab FAILs (Mixer, Devices,
Pattern Editor) are the same harness misclick. The −242 px shift matches the pinned Inspector moving the
centre column (the inventory was taken in a different layout from the one the click ran in). This is a
problem in the sweep harness's coordinates, not in the product.

No product code changed, so there is no RED/GREEN regression pair and no fix.patch.

## Commands and exit codes (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-automation.r1/logs/`)

| step | command | EXIT |
|---|---|---|
| build (1st try) | `cmake --build build --target ShellHomesTest -j4 > logs/01-build-ShellHomesTest.log 2>&1` | 2 — `ccache: error: failed to create temporary file … ccache-tmp-r6 … Permission denied` (sandbox; not a code error) |
| test (stale, not built) | `ctest -R '^ShellHomesTest$' --output-on-failure > logs/02-test-ShellHomesTest.log` | 8 (Not Run — the build above failed) |
| build | `CCACHE_DISABLE=1 cmake --build build --target ShellHomesTest -j4 > logs/03-build-ShellHomesTest-noccache.log 2>&1` | 0 |
| family test (from build/tests) | `QT_QPA_PLATFORM=offscreen ctest -R '^ShellHomesTest$' --output-on-failure > logs/04-test-ShellHomesTest.log 2>&1` | 0 — 1/1 tests passed (`#429 ShellHomesTest Passed`) |
| gates (as given) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/05-feature_check.log` | 1 — `PermissionError` creating `<worktree>-feature-check` (sibling of the worktree, outside the write jail); no gate ran |
| gates (log dir redirected) | `python3 runs/gui-1106-main-automation.r1/feature_check_logs_redirected.py <worktree> > logs/06-feature_check.log` | 0 — RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures exit 0; complexity and file-length: no new item) |

The redirected copy differs from `feature_check.py` in two lines only: `ROOT` points to the original
`qwen/` folder, and `logs` points to the LOG FOLDER (the `diff` is printed at the top of the run). The gate
commands, baselines and limits are unchanged.

## Evidence summary

| item | value |
|---|---|
| verdict | FALSE-POSITIVE (harness click landed −242 px off, inside the pinned Inspector) |
| RED command / EXIT | n/a — no product defect, no fix |
| GREEN command / EXIT | n/a |
| affected family | `ctest -R '^ShellHomesTest$'`: 1 test executed, EXIT 0 |
| gates | as given: EXIT 1 (sandbox PermissionError, no gate ran); log-redirected copy: EXIT 0, all PASS |
| files committed | `docs/qwen-fixes-2026-10/cluster-gui-1106-main-automation.md` only |

## Self-check

- **Regression test fails with the product change reverted and passes with it:** not applicable. The verdict
  is FALSE-POSITIVE and no product file changed (`git diff --stat af010bc3cc..HEAD` lists only this report).
  There is nothing to revert. The existing tab-path test `ShellHomesTest`
  (`tests/src/gui/ShellHomesTest.cpp:156-178`) passes on this branch: `ctest -R '^ShellHomesTest$'` EXIT 0,
  1/1 executed (`logs/04-test-ShellHomesTest.log`).
- **Deterministic:** no test was added. The cited `ShellHomesTest` case drives `choose()` and
  `QTabBar::setCurrentIndex` directly, with no timers, sleeps or waits on its own path.
- **Fix at the cause and complete / callers and siblings:** no fix. The siblings were checked anyway: the
  other three main-view tab FAILs (Mixer, Devices, Pattern Editor) in the same run share the same −242 px
  click offset (table above). All 20 tab clicks in the four `tab-*` views PASS through the same
  `ShellRegionTabs::toggle` → `dispatchWindowToggle` path (`src/gui/shell/ShellRegionTabs.cpp:194-217`,
  `src/gui/ControlCommandsWindow.cpp:198-202`). The other callers of `dispatchWindowToggle`
  (`src/gui/shell/Shell.cpp:275`, `src/gui/shell/ShellFloating.cpp:181`, `src/gui/MainWindow.cpp:155,1375`)
  are untouched.
- **No collateral damage:** no product code, data format, limit or check changed. Projects and saved
  layouts load exactly as before.
- **Evidence for FALSE-POSITIVE:** the inventory places the Automation tab at x=590 (`controls.jsonl:17`) but
  the click ran at x=348 (`results.jsonl:17`). `shots/0016-after.png` shows the cursor inside the pinned
  Inspector's empty body, with no tab there. The contract that tab clicks open their bottom panel holds:
  same-run PASS verdicts for every `tab-*` view, and `ShellHomesTest.cpp:156-178` passes.
- **Gates row of `submit_check.py`:** it runs `feature_check.py`, which calls `mkdir` on
  `<worktree>/../zene-qwen-gui-1106-main-automation-feature-check`. That directory is outside this lane's
  write jail, so it fails with `PermissionError` before any gate runs (`logs/05-feature_check.log`). The
  identical gates, with only the log directory moved, give RESULT: PASS, EXIT 0 (`logs/06-feature_check.log`).
  When the loop runs `submit_check.py` outside the jail, this row should pass.
