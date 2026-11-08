FALSE-POSITIVE

# Cluster gui-1106-main-delete — "Delete tool selection is not visibly indicated"

Verdict: **FALSE-POSITIVE (an artefact of the GUI sweep).** The Delete button works and shows its selection. The
sweep's click never reached it, because a floating Inspector window that an earlier sweep step had popped out
covered the left part of the Note Editor. No product code changed. I added one guard test that pins the contract
the finding expected.

## What the sweep actually saw (evidence: `terra-pilot/buttons/runs/m0-train-26`)

- `controls.jsonl` line 24 (shot index 0023): Luna's inventory put "Delete" at x=452, y=742 (28x24). It put the
  fold controls at x=741/771/801 and Ghost notes at x=975.
- `shots/0023-before.png` and `shots/0023-after.png` (1600x1000) are identical where the toolbar is. The
  Note Editor toolbar is visible only from x≈480: the fold glyphs are at ≈499/529/559 and Ghost at ≈733. That is
  a constant 242 px left of the inventory. The ruler starts at bar 10. The clip label, all seven tool buttons, the
  grid box and Quantise are **not on screen at all**. The arranger toolbar above is cut off at x≈480 in the same way.
- The panel covering x 0–480 is the **Inspector, floating**: its header shows a text button "Dock". That button
  reads "Dock" only while the panel is detached (`src/gui/shell/ShellFloating.cpp:434-441`). The control the
  sweep clicked just before (control 12, "Pop out the Inspector panel") is what detached it.
- With the Inspector out of the Left region, the docked regions start at x≈0 (that accounts for the 242 px shift:
  the 240 px Inspector plus its 2 px handle). The floating window sits over the first ~480 px of the main window.
  Delete's real position is therefore ≈(210, 754). The pointer in `0023-after.png` is at ≈(210, 752), on the
  floating Inspector and not on the button. A click on another window cannot change the button.
- The same judge batch (`judge-1.md`) reports the Mixer/Devices/Automation/Pattern tabs, "Hide the bottom panels",
  "Pop out" and the Paint/Mute/Slice tools all doing "nothing". Those siblings fit the same covered-or-moved
  layout, not a ShellButton paint defect.

## Why the code is not at fault (the path the finding names)

- `src/gui/noteeditor/NoteEditorToolbar.cpp:113-119`: every tool button is a `ShellButton` with
  `setDefaultAction(action)`. Qt keeps the button's `checked` in sync with the action's, so the "diverging
  states" the finding speculates about cannot occur.
- `src/gui/noteeditor/NoteEditorToolbar.cpp:161-180`: the seven checkable actions sit in one exclusive
  `QActionGroup`. `triggered` calls `NoteEditor::setTool` (`src/gui/noteeditor/NoteEditor.cpp:278-287`), which checks
  the matching action.
- `src/gui/shell/ShellChrome.cpp:136-144` (the finding's line 141): a checked icon-only button fills with
  `pal.control`, where an unchecked one is transparent. `src/gui/shell/ShellChrome.cpp:161-164` paints the icon in
  `QIcon::On`, and `src/gui/shell/ShellIcons.cpp:313` draws `On` in the accent colour. So the checked state shows in
  both the fill and the glyph colour.

## Guard test (the contract the finding expected)

`tests/src/gui/NoteEditorFlTest.cpp:129` `aClickOnTheDeleteButtonShowsItSelected` finds the toolbar's Draw and
Delete `ShellButton`s by their actions' tooltips and checks that Delete is visible and enabled. It clicks Delete with
`QTest::mouseClick` and asserts:
- the tool is `NoteTool::Erase`;
- the Delete button and its action are checked, and Draw is unchecked;
- the Delete button's grabbed pixels differ from before the click (line 151), and so do Draw's.

The test is deterministic: no timers, sleeps or waits beyond the existing `qWaitForWindowExposed` in `init()`, and
the pixel comparison uses `QWidget::grab()`. There is no product fix to revert. To prove the guard catches the
reported symptom, I ran it against a **mutant**: `ShellButton` made to paint checked exactly like unchecked
(`isChecked()` ignored in `fillColour()`, icon always `QIcon::Off`). The mutant is
`logs/mutation.patch` in the run folder, reverted with `git apply -R`.

## Commands and results (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-delete.r1/logs/`)

ccache's temp dir is outside this lane's write-jail, so every build ran with `CCACHE_DISABLE=1`.

| Step | Command | Result |
| --- | --- | --- |
| Build guard + zene | `CCACHE_DISABLE=1 cmake --build build --target zene NoteEditorFlTest ShellHomesTest -j4 > 07-build.log` | EXIT=0 |
| Guard on current tree (ctest) | `ctest -R '^NoteEditorFlTest$' --output-on-failure > 08-test-noteeditorfl.log` (from build/tests) | EXIT=0, 1/1 passed |
| Guard alone, current tree | `./NoteEditorFlTest aClickOnTheDeleteButtonShowsItSelected > 09-guard-direct.log` | EXIT=0, PASS |
| Build mutant | `CCACHE_DISABLE=1 cmake --build build --target NoteEditorFlTest -j4 > 10-build-mutant.log` | EXIT=0 |
| Guard on mutant (RED) | `./NoteEditorFlTest aClickOnTheDeleteButtonShowsItSelected > 11-guard-mutant.log` | EXIT=1, `FAIL! ... 'erase->grab().toImage() != eraseBefore' returned FALSE. (the Delete button looks the same once selected)` at NoteEditorFlTest.cpp(151) |
| Revert mutant, rebuild | `git apply -R logs/mutation.patch` (EXIT=0); `CCACHE_DISABLE=1 cmake --build build --target zene NoteEditorFlTest ShellTest ShellHomesTest AccessibilityHelperTest -j4 > 12-build-restored.log` | EXIT=0 |
| Build NoteEditorTest | `CCACHE_DISABLE=1 cmake --build build --target NoteEditorTest -j4 > 14-build-noteeditortest.log` | EXIT=0 |
| Family (GREEN) | `ctest -R '^(NoteEditorFlTest\|NoteEditorTest\|ShellTest\|ShellHomesTest\|AccessibilityHelperTest)$' --output-on-failure > 15-test-family.log` | EXIT=0, 5/5 passed |
| Gates | `python3 logs/run_feature_check.py <worktree> > 17-feature-check.log` | EXIT=0, RESULT: PASS (8/8 rows PASS) |

About the gates run: `feature_check.py` as shipped writes its logs to `<worktree>-feature-check`, a sibling of
the worktree and outside this lane's write-jail. Run directly, it died with `PermissionError` (EXIT=1,
`16-feature-check.log`) before any gate ran. `run_feature_check.py` executes the unmodified script source, with
the one `logs = ...` line pointed at `logs/feature-check/`. All gate commands, the baseline and the ratchet logic
are unchanged.

An earlier intermediate run (`13-test-family.log`, EXIT=8) showed NoteEditorTest as "Not Run" only because I had not
built that target yet. It passed once built (row "Family").

Before settling on the verdict, I added a throw-away probe to ShellHomesTest (`01`–`06` logs) to check a
splitter-overlap theory. It did not reproduce any overlap, and I reverted it with `git restore`; it is not
committed.

## Self-check

- **Regression test FAILS without the fix, PASSES with it:** there is no product fix (FALSE-POSITIVE). The guard
  passes on the current tree (EXIT=0, logs 08/09) and fails on a mutant that reproduces the reported symptom
  (EXIT=1, log 11). It passes again after the revert (EXIT=0, log 15).
- **Deterministic:** yes. It uses a synchronous `QTest::mouseClick` and `QWidget::grab()` pixel comparison, with no
  timers, sleeps or wall-clock time.
- **Fix at the cause, siblings consistent:** no product change. The siblings in the same batch (Paint, Mute, Slice
  tools) take the same path (`NoteEditorToolbar.cpp:161-180`). The guard's mechanism (default action plus an
  exclusive group plus `ShellButton` paint) is shared by all seven tools.
- **No collateral damage:** only a test was added. No saved data, project loading, commands or audio-thread code
  were touched.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** `src/gui/shell/ShellFloating.cpp:434-441` ("Dock" only while
  floating) with shot 0023 shows the floating Inspector over the editor. `NoteEditorToolbar.cpp:113-119` and
  `:161-180`, `NoteEditor.cpp:278-287`, `ShellChrome.cpp:136-144,161-164` and `ShellIcons.cpp:313` show that the
  checked state is synced and painted.
- **Not checked:** I did not replay the full sweep against a live instance. The floating-window explanation rests on
  the screenshots, the inventory geometry and the "Dock" label contract cited above.

Files committed: `tests/src/gui/NoteEditorFlTest.cpp`, `docs/qwen-fixes-2026-10/cluster-gui-1106-main-delete.md`.
