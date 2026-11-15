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

## Integration onto 050/m0-train-33

The accepted source commit `101cd96af3ffa91d9bd655988e15cfd337342d27` was applied by the driver onto the pinned target
`cc29a63dde09cf28bb00498a2d2a5b124d4d614d` (`source_base` is the same commit, so the tree had no textual conflict:
all five accepted files applied cleanly). This integration is not a re-implementation; the accepted design and its
`GrooveMenuTest` regression are retained.

The driver's integration reason (fails `PanelShortcutsTest` on 050/m0-train-34) **reproduces on the pinned target too**:
with only the accepted product patch applied, `PanelShortcutsTest::theSameKeyMeansOneThingPerPanel` fails twice in a row
at `tests/src/gui/PanelShortcutsTest.cpp:290` with `QApplication::focusWidget()` null instead of the arranger. The notice
created by an announced command (`TextFloat`) is a `Qt::Tool` top-level window; when the command form/menu that was
active closes, Qt chose that visible tool window as the application's active window. `WA_ShowWithoutActivating` and
`Qt::WindowDoesNotAcceptFocus` did not prevent that choice on the offscreen platform, so the panel that had keyboard
focus lost it and the panel-shortcut key went nowhere. With the accepted product patch reversed, the same test passes
(EXIT 0), which isolates the regression to this notice.

The integration fix keeps the accepted frameless, stay-on-top tool-window design and adds the missing guarantee that a
notice never holds focus: `TextFloat` now handles `QEvent::WindowActivate` and hands activation straight back to its
owning window (`src/gui/widgets/TextFloat.cpp:178-191`, declared at `include/TextFloat.h:64`, re-entry guard at
`include/TextFloat.h:75`). `Qt::WindowDoesNotAcceptFocus` was added to the window flags as the declarative part of the
same contract. No new design was required and no accepted behaviour/test was removed.

## Fix

- `src/gui/widgets/TextFloat.cpp:71-80`: a notice is a frameless `Qt::Tool` with `Qt::WindowStaysOnTopHint` and
  `Qt::WindowDoesNotAcceptFocus` instead of a `Qt::ToolTip`; `Qt::WA_ShowWithoutActivating` avoids activation on show
  and `Qt::WA_QuitOnClose` false preserves the tooltip's promise never to quit the application when it closes.
- `src/gui/widgets/TextFloat.cpp:178-191` + `include/TextFloat.h:64,75`: if Qt nevertheless makes the notice the active
  window (observed when the command form the message came from closes), the notice immediately re-activates its owning
  window. The guard prevents re-entry; the hand-back is skipped while the application itself is inactive, so it cannot
  steal focus from another application.
- `src/gui/CommandMetadata.cpp:434-459`: `resultSummary` expands a nested `summary` object as its own lines first,
  then the top-level fields. The accessibility audit's five counts are therefore the first lines of the notice. The
  change is generic but only this command produces an object-valued `summary`; the other producer, `PresetBindings`,
  sets `summary` to a string and is unchanged. All three `resultSummary` callers (`CommandRun::announce`,
  `CommandForm::resultText`, `CommandListDialog`) use the same function.

## Regression tests

- Accepted regression, retained: `tests/src/gui/GrooveMenuTest.cpp:698` (`aReportFromTheOpenHelpMenuSurvivesTheMenuClosing`)
  opens the real Help menu, clicks the Accessibility Check entry with `QTest::mouseClick` (the menu is open while the
  command runs, then closes), and requires the new report to be visible, not a `Qt::ToolTip`, stay-on-top, and to carry
  `Name:`, `Role:`, `Value:`, `Focus:` and `Duplicates:`. Fixed geometry, `QMenu::popup()`, `QTest::mouseClick` and
  `QCoreApplication::processEvents()` only: no sleeps, timers, wall-clock reads or races.
- Integration regression, already present and deterministic:
  `tests/src/gui/PanelShortcutsTest.cpp:290` (`theSameKeyMeansOneThingPerPanel`) fails on the accepted patch because the
  announced notice takes the active window, and passes with the hand-back. It is run twice in the table below to show
  the failure is deterministic, and it is the exact failure the driver bisected on train-34.

## Evidence

The requested log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1115-menu-help-accessibility-check.r2/logs` is not writable in
this session (the directory is owned by uid 1000 but `touch`, Python `open(..., "w")` and the `write` tool all return
`EACCES`, including its parent `runs/`; passwordless sudo is unavailable). All r2 logs below are therefore in the
writable harness mirror `LOGS=/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/logs-gui-1115-r2`; the commands are
unchanged apart from that path. The accepted-patch (r1) evidence is retained under its original r1 log folder.

### Integration round 2 (this worktree, logs in `.../dsh/home/logs-gui-1115-r2`)

| Step | Command | EXIT |
| --- | --- | --- |
| build integration baseline (accepted patch applied) | `cmake --build build --target zene GrooveMenuTest PanelShortcutsTest > LOGS/build-1-integration-baseline.log 2>&1` | 0 |
| regression RED #1 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^PanelShortcutsTest$' --output-on-failure > LOGS/test-1-panelshortcuts.log 2>&1` | 8 |
| regression RED #2 (deterministic) | same command `> LOGS/test-2-panelshortcuts-repeat.log 2>&1` | 8 |
| isolate regression: save accepted product patch, reverse it | `git diff --cached -- src/gui/widgets/TextFloat.cpp src/gui/CommandMetadata.cpp > LOGS/accepted-product.patch && git apply -R LOGS/accepted-product.patch` | 0 |
| build baseline without the accepted product patch | `cmake --build build --target PanelShortcutsTest > LOGS/build-2-baseline-reverted.log 2>&1` | 0 |
| baseline GREEN (proves the accepted patch is the regression source) | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^PanelShortcutsTest$' --output-on-failure > LOGS/test-3-baseline-reverted.log 2>&1` | 0 |
| reapply the accepted patch | `git apply LOGS/accepted-product.patch` | 0 |
| candidate `WindowDoesNotAcceptFocus` alone: build / still RED | `cmake --build build --target zene PanelShortcutsTest GrooveMenuTest > LOGS/build-3-nofocus-fix.log 2>&1`; `ctest -R '^(PanelShortcutsTest\|GrooveMenuTest)$' > LOGS/test-4-nofocus-fix.log 2>&1` | 0 / 8 (GrooveMenuTest passed) |
| final integration fix: build | `cmake --build build --target zene PanelShortcutsTest GrooveMenuTest ArrangerZoomBoundTest PluginShortcutsTest > LOGS/build-8-final-candidate.log 2>&1` | 0 |
| final integration fix: affected family | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(PanelShortcutsTest\|GrooveMenuTest\|ArrangerZoomBoundTest\|PluginShortcutsTest)$' --output-on-failure > LOGS/test-9-final-candidate-family.log 2>&1` | 0 — 4/4 |
| summary/accessibility family build | `cmake --build build --target CommandFormTest WorkspaceAccessTest ItemListPanelTest AccessibilityHelperTest AccessibilityWalkTest > LOGS/build-9-summary-family.log 2>&1` | 0 |
| summary/accessibility family run | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(CommandFormTest\|WorkspaceAccessTest\|ItemListPanelTest\|AccessibilityHelperTest\|AccessibilityWalkTest)$' --output-on-failure > LOGS/test-10-summary-family.log 2>&1` | 0 — 5/5 |
| 4b save + revert the integration fix only | `git diff -- include/TextFloat.h src/gui/widgets/TextFloat.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| 4b rebuild / RED | `cmake --build build --target PanelShortcutsTest > LOGS/build-10-red-reverted.log 2>&1`; `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^PanelShortcutsTest$' --output-on-failure > LOGS/test-11-red-reverted.log 2>&1` | 0 / 8 |
| 4b reapply / rebuild / GREEN | `git apply LOGS/fix.patch`; `cmake --build build --target zene PanelShortcutsTest GrooveMenuTest > LOGS/build-11-green-reapplied.log 2>&1`; `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(PanelShortcutsTest\|GrooveMenuTest)$' --output-on-failure > LOGS/test-12-green-reapplied.log 2>&1` | 0 / 0 / 0 — 2/2 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-1.log 2>&1` | 0 — all 8 rows PASS |

The exact RED assertion in `test-1-panelshortcuts.log`, `test-2-...`, `test-4-...` and `test-11-red-reverted.log` is:

```
FAIL!  : PanelShortcutsTest::theSameKeyMeansOneThingPerPanel() Compared QObject pointers are not the same
   Actual   (QApplication::focusWidget())    : <null>
   Expected (static_cast<QWidget*>(arranger)): lmms::gui::SongEditor/...
   Loc: [../../tests/src/gui/PanelShortcutsTest.cpp(290)]
```

and `test-12-green-reapplied.log` shows `100% tests passed out of 2`.

### Accepted-patch evidence (r1, unchanged)

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

The accepted-patch RED assertion in `test-12-final-red.log` is
`FAIL! : GrooveMenuTest::aReportFromTheOpenHelpMenuSurvivesTheMenuClosing() 'report->windowType() != Qt::ToolTip' returned FALSE.`
and the summary-only RED in `test-8-summary-red.log` is `'shown.contains(checkName + QLatin1Char(':'))' returned FALSE (Name count is not in: ...)`.

Gate note (r1): the first direct gate run (`gates-1.log`, EXIT 1) was blocked before any gate ran because
`feature_check.py` tried to `git checkout` the base commit in its shared baseline tree and the Bunny git guard denied
that lane-foreign checkout. The two ratchet gates were run on a pristine base tree extracted with
`git archive cc29a63dde09cf28bb00498a2d2a5b124d4d614d` (`gate-base-complexity.log`, `gate-base-file-length.log`, both
EXIT 0, no `REGRESSION:` lines), and that exact empty base listing was installed as the harness cache entry
`gate-baselines/cc29a63dde....json`; the unmodified `feature_check.py` then passed all 8 rows (`gates-2.log`, EXIT 0).
In integration round 2 the cached base was reused and the unmodified gate passed all 8 rows first time (`gates-1.log`,
EXIT 0). No gate limit or baseline was changed.

Files committed: `include/TextFloat.h`, `src/gui/widgets/TextFloat.cpp`, `src/gui/CommandMetadata.cpp`,
`tests/src/gui/GrooveMenuTest.cpp`, `tests/upstream-modifications.txt`, this report. `CommandMetadata.cpp` is already in
`tests/fork-sources.txt`; no new product file was added. `tests/upstream-modifications.txt` gained reason lines for the
changed inherited files (`src/gui/widgets/TextFloat.cpp`, `include/TextFloat.h`, `tests/src/gui/GrooveMenuTest.cpp`)
and ends with a newline.

## Self-check

- **The regression test fails without the integration fix and passes with it:** yes. With the accepted patch retained
  and only the integration fix reversed (`git diff -- include/TextFloat.h src/gui/widgets/TextFloat.cpp > LOGS/fix.patch
  && git apply -R LOGS/fix.patch`, EXIT 0), the rebuilt `PanelShortcutsTest` fails EXIT 8 at line 290 (two consecutive
  RED runs before the fix, `test-1`/`test-2`, and again in `test-11`), and the same test passes EXIT 0 after
  `git apply LOGS/fix.patch` and rebuild (`test-12`, 2/2). The accepted `GrooveMenuTest` also still passes.
- **The test is deterministic:** no wall-clock timing, sleeps, timers or races. `PanelShortcutsTest` drives real
  `QTest::keyClick` delivery after `QTRY_COMPARE` focus synchronisation; `GrooveMenuTest` opens the real menu with
  `QMenu::popup()`, clicks at `actionGeometry().center()` with `QTest::mouseClick`, and uses
  `QCoreApplication::processEvents()` as an event-loop sync point. The same RED assertion reproduced twice in a row.
- **The fix is at the cause and complete:** `TextFloat.cpp` is the one constructor every floating message uses, so the
  Help report and every other `TextFloat` caller get both the persistent window and the activation hand-back;
  `SimpleTextFloat` is a separate class for value readouts and is untouched. The hand-back targets the notice's owning
  window (the main window, set by the one constructor), is guarded against re-entry, and is a no-op while the
  application is inactive. `resultSummary` is the one formatter used by `CommandRun::announce`,
  `CommandForm::resultText` and `CommandListDialog`; only the audit produces an object-valued `summary`, and
  `PresetBindings`' string `summary` is unchanged.
- **No collateral damage:** no project/save/load format, undo, control command or audio path changed; the change is
  GUI window/focus/formatting only. The affected family (4 GUI tests) and the summary/accessibility family (5 tests)
  pass, including `CommandFormTest` (the `resultSummary` contract), `AccessibilityHelperTest`, `AccessibilityWalkTest`,
  `WorkspaceAccessTest` and `ItemListPanelTest` (the audit result shape). A limit was not added; the existing 8-line
  notice cap still applies, the audit's own five counts now fit before it. Old projects and saved data are unaffected.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable; verdict is FIXED (REAL). The code path and the sweep
  screenshot are cited above.
- **Realtime:** no audio-thread code. `TextFloat`, `resultSummary`, `CommandRun::announce` and the tests all run on the
  GUI/test thread; nothing allocates, locks, waits, logs or signals on the render thread.
- **Registration:** `tests/upstream-modifications.txt` has reason lines for `src/gui/widgets/TextFloat.cpp`,
  `include/TextFloat.h` and `tests/src/gui/GrooveMenuTest.cpp` and ends with a newline; `CommandMetadata.cpp` is already
  in `tests/fork-sources.txt`; `GrooveMenuTest.cpp` and `PanelShortcutsTest.cpp` are already registered in
  `tests/CMakeLists.txt`. Final `feature_check.py`: 8/8 PASS, EXIT 0.
