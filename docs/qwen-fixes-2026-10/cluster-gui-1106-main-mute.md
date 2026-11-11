FALSE-POSITIVE

# cluster gui-1106-main-mute — "Mute tool selection is not visibly indicated"

Verdict: **FALSE-POSITIVE.** The sweep's click never reached the Mute tool button. An earlier step of the
same sweep had popped the Inspector out into its own floating window, and that window covered the main window's
top-left corner. The clicks for Draw, Paint, Delete, Mute and Slice all landed on that Inspector window. The
ShellButton painter and the tools' QActionGroup do not diverge: a guard test clicks the real Mute button in
a docked shell and checks both its state and its pixels.

No product code changed. This commit adds one guard test and this report.

## Evidence

### 1. The sweep's own record (evidence dir `~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-26`)

`results.jsonl`, in click order (name | x,y at click time):

| row | control | click at |
|---|---|---|
| 13 | **Pop out the Inspector panel** ("Float Inspector in its own window") | 179,99 |
| 21 | Pop out the Note Editor panel | 1539,712 |
| 22-26 | Draw, Paint, Delete, **Mute**, Slice | x = 150, 180, 210, **240**, 270 ; y = 742 |
| 27-28 | Select, Play | x = 542, 572 |

- `inv-main.jsonl` (the inventory, taken before those clicks) places the same buttons at x = 392, 422, 452,
  **482**, 512, 542, 572. Only the five controls the sweep re-resolved at x < 480 were clicked away from where
  the inventory placed them.
- `shots/0024-before.png` (1600x1000): the left 480 px show an **Inspector panel whose header reads "Dock | Pin"**.
  A panel shows "Dock" only while it is floating; ShellTest's `theCentreSaysWhereItsPanelWentAndTheButtonSaysDock`
  covers that contract. The main window's bottom panel title shows only "…Editor" at x = 483, the tail of
  "Note Editor". The fold buttons, NO SCALE and NOTES are visible. The clip title, the seven tools, the grid box
  and Quantise are hidden under the floating Inspector. The mouse cursor sits at (210-240, 742), inside the
  Inspector window.
- A click there goes to the floating Inspector, so the Note Editor toolbar cannot change. That is exactly what
  the sweep observed. The sweep's state caused it, not the button.

### 2. The code: the painter cannot disagree with the action group

- `src/gui/noteeditor/NoteEditorToolbar.cpp:113-119`: every tool is a `ShellButton` with
  `setDefaultAction(action)`. QToolButton keeps its checked state in sync with its default action, so the
  exclusive `QActionGroup` (`NoteEditorToolbar.cpp:164-176`) and the button share one checked state.
- `src/gui/shell/ShellChrome.cpp:141` (`fillColour`): a checked icon button fills with `pal.control`. The
  toolbar's background is `pal.panel` (`NoteEditorToolbar.cpp:137-139`). Those are 0x1c1c22 vs 0x08080a in the dark
  theme (`DesignTokens.cpp:52,54`).
- `ShellChrome.cpp:163-164`: the icon also paints in `QIcon::On` mode when checked.

### 3. The probe and the guard test

The test clicks the Mute button through the real docked shell. It is `ShellTest::aClickedNoteEditorToolButtonShowsItIsSelected`
(`tests/src/gui/ShellTest.cpp`). It mounts a real `NoteEditor` in the `detail` panel of a 1600x1000 `Shell`,
then checks four things:
- the Mute button lies inside its panel, and `childAt` its centre is the button itself, so nothing in the
  docked shell covers it;
- a real `QTest::mouseClick` checks both the button and its action;
- the button's rendered pixels differ from its unchecked image;
- the button's rendered pixels also differ from its hovered image.
It is deterministic: no timers or sleeps, only synchronous `grab()`.

This is a guard, not a regression proof. No product change exists to revert, so it passes on this tree, as a
FALSE-POSITIVE requires. The probe that came before it printed the geometry of every Note Editor toolbar button
in the docked shell (log `03-probe.log`). Mute was at (468,730) inside the detail frame at x = 242, matching the
sweep's inventory (482,742) once the window frame is allowed for.

## Commands run (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-mute.r1/logs`)

| step | command | EXIT |
|---|---|---|
| build (probe) | `cmake --build build --target ShellTest -j4 > 01-build-probe.log` | 2 (ccache could not create its temp dir in the write-jail; not a compile error) |
| build (probe) | `CCACHE_DISABLE=1 cmake --build build --target ShellTest -j4 > 02-build-probe.log` | 0 |
| probe run | `ctest -R '^ShellTest$' --output-on-failure -V > 03-probe.log` | 0 |
| build guard test | `CCACHE_DISABLE=1 cmake --build build --target ShellTest -j4 > 04-build-test.log` | 0 |
| family run | `ctest -R '^(ShellTest\|ShellHomesTest\|AccessibilityHelperTest)$' -V > 05-test.log` | 8: ShellTest 25/25 passed; the two others were *Not Run* (their executables were not built yet) |
| build family | `CCACHE_DISABLE=1 cmake --build build --target ShellHomesTest AccessibilityHelperTest -j4 > 06-build-family.log` | 0 |
| family run | `ctest -R '^(ShellTest\|ShellHomesTest\|AccessibilityHelperTest)$' --output-on-failure > 07-test-family.log` | **0**: 100% of 3 tests passed |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 08-gates.log` | 1: `PermissionError` creating `<worktree>/../zene-qwen-gui-1106-main-mute-feature-check` (outside this lane's write-jail), so no gate ran |
| gates | `python3 logs/feature_check_logs_in_run.py <worktree> > 09-gates.log`: the same script with only `logs = …` (and `ROOT`, since the copy lives elsewhere) pointed at the run folder; diff in the log folder | **0**: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS; RESULT: PASS |

Files committed: `tests/src/gui/ShellTest.cpp` and `docs/qwen-fixes-2026-10/cluster-gui-1106-main-mute.md`.
No new files need registering in fork-sources or upstream-modifications. ShellTest.cpp is an existing fork test,
and the report lives under docs/.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** There is no product
  change, so step 4b has nothing to revert. The verdict is FALSE-POSITIVE. The guard test passes on the current
  tree (`07-test-family.log`, EXIT 0) and pins the contract the finding questioned.
- **Deterministic.** `QTest::mouseClick` and `QWidget::grab()` are synchronous. The test uses no sleeps, no
  timers and no wall-clock time. `qWaitForWindowExposed` returns immediately on offscreen.
- **At the cause and complete.** The cause is the sweep's state: the Inspector was popped out at row 13 and
  covered x < 480. The same applies to the sibling tools Draw, Paint, Delete and Slice (rows 22-26, clicked at
  x 150-270, all under that window). Select and Play at x ≥ 542 were not covered.
- **No collateral damage.** No product file changed. Only an additive test was added.
- **FALSE-POSITIVE evidence (file:line / contract).** `NoteEditorToolbar.cpp:113-119` (`setDefaultAction`
  ties button and action), `NoteEditorToolbar.cpp:164-176` (exclusive group), `ShellChrome.cpp:141` and
  `ShellChrome.cpp:163-164` (the checked fill and the On icon). The sweep evidence is `results.jsonl` rows 13
  and 22-28, `inv-main.jsonl`, and `shots/0024-before.png` (the floating Inspector with "Dock").
- **submit_check.py.** It calls `feature_check.py` itself, which hits the same `PermissionError` in this lane's
  write-jail. Its gate row will read FAIL for that reason only. The same gates pass in `09-gates.log`.
