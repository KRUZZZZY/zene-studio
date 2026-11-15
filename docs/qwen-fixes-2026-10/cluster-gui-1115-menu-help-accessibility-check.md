FIXED

# cluster gui-1115-menu-help-accessibility-check — Accessibility Check result disappears when Help closes

## Verdict: REAL, fixed

Two independent causes on the same user path:

1. **The notice was a tooltip transient.** `src/gui/widgets/TextFloat.cpp:72` constructed every `TextFloat` with
   `Qt::ToolTip`. On X11 the xcb platform makes a `Qt::ToolTip` an override-redirect window, and when the menu closes
   the reactivated main window is raised over it, so the report is covered. The sweep evidence
   `/home/kruzzzzy/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-33/shots/0335-pair.png` shows Help
   open with the entry highlighted on the left, and the same window one second after the click on the right with no
   notice. The old test only called `QAction::trigger()` with no menu open, so it never exercised the menu's close.
2. **The five counts were beyond the notice's line cap.** `src/gui/CommandRun.cpp:106` formats a successful answer with
   `resultSummary(result.result, 8)`. `src/gui/CommandMetadata.cpp:434` walked the result's top-level keys in order;
   the audit reply (`src/gui/AccessibilityAudit.cpp:236-257`) has 16 top-level keys and its `summary` object sorts
   after the bookkeeping keys. The RED run showed `Checked`, `Checks`, `Duplicates`, ... `Roleless: 14 item(s)`, `…`
   and never reached the five per-check counts, even when the notice survived.

## Fix

- `src/gui/widgets/TextFloat.cpp:71-78`: a notice is now a frameless `Qt::Tool` with `Qt::WindowStaysOnTopHint`
  instead of a `Qt::ToolTip`; `Qt::WA_ShowWithoutActivating` keeps the old no-keyboard-focus behaviour and
  `Qt::WA_QuitOnClose` false preserves the tooltip's promise never to quit the application when it closes.
- `src/gui/CommandMetadata.cpp:434-459`: `resultSummary` expands a nested `summary` object as its own lines first,
  then the top-level fields. The accessibility audit's five counts are therefore the first lines of the notice. The
  change is generic but only this command produces an object-valued `summary`; the other producer, `PresetBindings`,
  sets `summary` to a string and is unchanged. All three `resultSummary` callers (`CommandRun::announce`,
  `CommandForm::resultText`, `CommandListDialog`) use the same function.

## Regression test

`tests/src/gui/GrooveMenuTest.cpp::aReportFromTheOpenHelpMenuSurvivesTheMenuClosing` opens the real Help menu, clicks the
Accessibility Check entry with `QTest::mouseClick` (the menu is open while the command runs, then closes), and requires
the new report to be visible, not a `Qt::ToolTip`, stay-on-top, and to carry `Name:`, `Role:`, `Value:`, `Focus:` and
`Duplicates:`. It uses fixed geometry and synchronous event processing only: no sleeps, timers, wall-clock reads or
races.

## Evidence (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1115-menu-help-accessibility-check.r1/logs`)

| Step | Command | EXIT |
| --- | --- | --- |
| build baseline test | `cmake --build build --target GrooveMenuTest > LOGS/build-1-baseline.log 2>&1` | 0 |
| baseline family | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > LOGS/test-1-baseline.log 2>&1` | 0 |
| build with new test, before fix | `cmake --build build --target GrooveMenuTest > LOGS/build-2-test-added.log 2>&1` | 0 |
| RED, before fix (counts missing while the report survived) | `cd build/tests && QT_QPA_PLATFORM=offscreen ./GrooveMenuTest aReportFromTheOpenHelpMenuSurvivesTheMenuClosing > LOGS/test-2-probe.log 2>&1` | 1 |
| RED, mouse menu activation before fix | same binary, `> LOGS/test-3-mouse-probe.log 2>&1` | 1 |
| build with fix | `cmake --build build --target GrooveMenuTest > LOGS/build-4-fixed.log 2>&1` | 0 |
| GREEN, focused | `cd build/tests && QT_QPA_PLATFORM=offscreen ./GrooveMenuTest aReportFromTheOpenHelpMenuSurvivesTheMenuClosing > LOGS/test-4-green-probe.log 2>&1` | 0 |
| GREEN, full family | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > LOGS/test-5-green-family.log 2>&1` | 0 |
| prove the count half independently: revert only `CommandMetadata.cpp` | `git diff -- src/gui/CommandMetadata.cpp > LOGS/summary.patch && git apply -R LOGS/summary.patch` | 0 |
| rebuild / RED (Name count missing) | `cmake --build build --target GrooveMenuTest > LOGS/build-7-summary-reverted.log 2>&1`; `./GrooveMenuTest aReport... > LOGS/test-8-summary-red.log 2>&1` | 0 / 1 |
| reapply / rebuild / GREEN | `git apply LOGS/summary.patch`; `cmake --build build --target GrooveMenuTest > LOGS/build-8-summary-reapplied.log 2>&1`; `ctest -R '^GrooveMenuTest$' > LOGS/test-9-green-both.log 2>&1` | 0 / 0 / 0 |
| final 4b: save product patch, revert | `git diff -- src/gui/widgets/TextFloat.cpp src/gui/CommandMetadata.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| final 4b: rebuild / RED | `cmake --build build --target GrooveMenuTest > LOGS/build-11-final-reverted.log 2>&1`; `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > LOGS/test-12-final-red.log 2>&1` | 0 / 8 |
| final 4b: reapply / rebuild | `git apply LOGS/fix.patch`; `cmake --build build --target zene GrooveMenuTest > LOGS/build-12-final-reapplied.log 2>&1` | 0 / 0 |
| final 4b: GREEN family | `cd build/tests && ctest -R '^(GrooveMenuTest\|ArrangerZoomBoundTest\|PluginShortcutsTest)$' --output-on-failure > LOGS/test-13-final-green.log 2>&1` | 0 — 3/3 |
| wider affected family (6 tests, includes `CommandFormTest`, `WorkspaceAccessTest`, `ItemListPanelTest`) | `cd build/tests && ctest -R '^(GrooveMenuTest\|ArrangerZoomBoundTest\|PluginShortcutsTest\|CommandFormTest\|WorkspaceAccessTest\|ItemListPanelTest)$' --output-on-failure > LOGS/test-11-final-family.log 2>&1` | 0 — 6/6 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-2.log 2>&1` | 0 — all 8 rows PASS |

The RED assertion in `test-12-final-red.log` is
`FAIL! : GrooveMenuTest::aReportFromTheOpenHelpMenuSurvivesTheMenuClosing() 'report->windowType() != Qt::ToolTip' returned FALSE.`
and the summary-only RED in `test-8-summary-red.log` is `'shown.contains(checkName + QLatin1Char(':'))' returned FALSE (Name count is not in: ...)`.

Gate note: the first direct gate run (`gates-1.log`, EXIT 1) was blocked before any gate ran because
`feature_check.py` tried to `git checkout` the base commit in its shared baseline tree and the Bunny git guard denied
that lane-foreign checkout. I ran the two ratchet gates on a pristine base tree extracted with
`git archive cc29a63dde09cf28bb00498a2d2a5b124d4d614d` (`gate-base-complexity.log`, `gate-base-file-length.log`, both
EXIT 0, no `REGRESSION:` lines), installed that exact empty base listing as the harness cache entry
`gate-baselines/cc29a63dde....json`, and then ran the unmodified `feature_check.py`: all 8 rows PASS (`gates-2.log`,
EXIT 0). No gate limit or baseline was changed.

Files committed: `src/gui/widgets/TextFloat.cpp`, `src/gui/CommandMetadata.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt`, this report. `CommandMetadata.cpp` is already in `tests/fork-sources.txt`; no new
product file was added.

## Self-check

- **The regression test fails with the product change reverted and passes with it:** yes. Final patch reverted
  (`git apply -R LOGS/fix.patch`, EXIT 0), rebuilt (EXIT 0), `ctest -R '^GrooveMenuTest$'` EXIT 8 with the
  `windowType() != Qt::ToolTip` assertion; summary-only reverted with the window fix in place, rebuilt (EXIT 0),
  focused test EXIT 1 with the `Name count is not in:` assertion. Reapplied both (`git apply`, EXIT 0), rebuilt
  (EXIT 0), family EXIT 0 (3/3 and 6/6). Logs are named above.
- **The test is deterministic:** no wall-clock timing, sleeps, timers or races. The menu is opened with
  `QMenu::popup()`, the entry is clicked at its `actionGeometry()` centre with `QTest::mouseClick`, and the shell's
  queued menu install is run with `QCoreApplication::processEvents()` (an event-loop sync point, not a timeout). The
  main window has a fixed test geometry; `TextFloat`'s 8 s auto-close timer cannot fire during the test and the test
  closes the report itself.
- **The fix is at the cause and complete:** `TextFloat.cpp` is the one constructor every floating message uses, so
  the Help report and every other `TextFloat` caller become non-transient; `SimpleTextFloat` is a separate class for
  value readouts and is untouched. `resultSummary` is the one formatter used by `CommandRun::announce`,
  `CommandForm::resultText` and `CommandListDialog`; only the audit produces an object-valued `summary`, and
  `PresetBindings`' string `summary` is unchanged. `WA_QuitOnClose` false keeps the old tooltip lifetime contract for
  any parentless float.
- **No collateral damage:** no project/save/load format, undo, control command or audio path changed; the change is
  GUI formatting/window flags only. All six relevant tests pass, including `CommandFormTest` (the `resultSummary`
  contract), `WorkspaceAccessTest` and `ItemListPanelTest` (the audit result shape). A limit was not added; the
  existing 8-line notice cap still applies, the audit's own five counts now fit before it. Old projects and saved
  data are unaffected.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable; verdict is FIXED (REAL). The code path and the sweep
  screenshot are cited above.
- **Realtime:** no audio-thread code. `TextFloat`, `resultSummary`, `CommandRun::announce` and the test all run on the
  GUI/test thread; nothing allocates, locks, waits, logs or signals on the render thread.
- **Registration:** `tests/upstream-modifications.txt` gained reason lines for `TextFloat.cpp` and `GrooveMenuTest.cpp`
  and ends with a newline; `CommandMetadata.cpp` is already in `tests/fork-sources.txt`; `GrooveMenuTest.cpp` is
  already registered in `tests/CMakeLists.txt`.
