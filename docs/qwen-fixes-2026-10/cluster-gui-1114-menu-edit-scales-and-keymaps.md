FIXED

# Cluster gui-1114-menu-edit-scales-and-keymaps: "Microtuner opens too narrow for its keymap controls"

## Verdict: REAL. The initial-size contract is wrong, and the keymap captions were painted in a font their controls never reserved

The patrol is right that the Microtuner's lower-right keymap range is clipped. The cause is narrower
than "the window is 300 px wide": a temporary probe showed Qt actually displaying the widget at
`500x480` (the default `QWidget` geometry clamped by the dialog's `500x700` maximum), not at the
`300x400` hint. The clipping that remains at that size is inside the controls themselves: their
captions are painted in the interface caption font, but the controls reserved a different font and a
fixed 9 px line. Separately, the `300x400` size hint is still wrong: the layout's own minimum is
`331x298`, so `MainWindow::addWindowedWidget` sizes the subwindow from a hint below its content's
minimum. Both contracts are fixed here.

Measured before the fix (temporary `qInfo` probe, removed again; `test-6-debug.log`):

| Measurement | Value | Consequence |
| --- | --- | --- |
| `sizeHint()` / `minimumSize()` / `minimumSizeHint()` | `300x400` / `300x300` / `331x298` | window is created from a hint below the layout minimum |
| shown top-level / `contentsRect()` | `500x480` / `500x480` | Qt's default geometry clamped by `setMaximumSize(500, 700)`; the literal window is not 300 px |
| `BASE NOTE FREQ` | control `93x32`; caption `102` px wide in the old paint font, `85` px in `Caption` | painted 9 px wider than the control and on a line too short for the caption |
| `FIRST` / `LAST` / `MIDDLE` / `BASE N.` | controls `45x30` / `45x30` / `45x30` / `47x30`; caption line needs `31` px | captions are clipped at the bottom |

The regression's first RED assertion reports the hint problem directly:
`the Microtuner's size hint 300x400 is below its layout minimum 331x298`. Its second reports every
caption: e.g. `BASE NOTE FREQ needs 85x36, control is 640x32` and
`FIRST needs 28x31, control is 45x30` (`test-8-red.log`).

## Code evidence

| Step | Location | Before -> after |
| --- | --- | --- |
| The hint is what sizes the window | `src/gui/MainWindow.cpp:737-742` | `win->resize(delta + w->sizeHint())`; unchanged caller, now receives a hint at/above the layout minimum |
| The hint was a constant | `include/MicrotunerConfig.h:54` | `QSize(300,400)` -> `QSize sizeHint() const override;` |
| Hint and minimum follow the layout | `src/gui/MicrotunerConfig.cpp:202-205`, `:215-221` | `setMinimumSize(300,300)` -> `setMinimumSize(microtunerLayout->minimumSize().expandedTo(QSize(300,300)))`; `sizeHint()` returns `layout()->minimumSize().expandedTo(QSize(300,400))` |
| The keymap range itself | `src/gui/MicrotunerConfig.cpp:151-182` | five controls with captions `FIRST`, `LAST`, `MIDDLE`, `BASE N.`, `BASE NOTE FREQ`; unchanged layout |
| Float control reserves its painted caption | `src/gui/widgets/LcdFloatSpinBox.cpp:109-135` | old `setFixedHeight(target + md)` -> `m_fieldWidth` from the field layout plus `updateSize()` reserving `tokens::font(TextRole::Caption)` width and line height |
| Float control paints in the reserved font | `src/gui/widgets/LcdFloatSpinBox.cpp:298-309` | old 12 px adjusted widget font on the baseline at `height()` -> `tokens::font(TextRole::Caption)` bottom-aligned inside the reserved line |
| LCD captions sized for the paint font | `src/gui/widgets/LcdWidget.cpp:220-232` | old width from the widget font and fixed `+ 9` line -> max of the old measurement and `Caption` width, and `max(9, Caption height + 1)` line |

`LcdWidget::setLabel()` (`src/gui/widgets/LcdWidget.cpp:187-191`) already called `updateSize()`;
`LcdFloatSpinBox::setLabel()` is now out of line (`include/LcdFloatSpinBox.h:51`,
`src/gui/widgets/LcdFloatSpinBox.cpp:115-119`) so it also re-sizes after the caption is set.

## Regression tests

Both are in `tests/src/gui/MicrotunerScaleFileTest.cpp`:

- `keymapRangeControlsFitTheSizeHint()` (`:247-290`) requires the hint to be at least the grid's
  `minimumSize()`, resizes the dialog to that hint, and requires all five controls in the
  lower-right range cell (`grid->itemAtPosition(5, 2)`, five items) to lie inside `contentsRect()`.
- `keymapLabelsReserveTheirFontMetrics()` (`:292-326`) measures the caption with the same
  `tokens::font(tokens::TextRole::Caption)` the controls paint with and requires every control to be
  at least `caption width x (field height + caption height)`.

Test matrix:

| Path | Test | Result |
| --- | --- | --- |
| Initial size | `keymapRangeControlsFitTheSizeHint` | RED `EXIT=8` -> GREEN `EXIT=0` |
| Caption metrics | `keymapLabelsReserveTheirFontMetrics` | RED `EXIT=8` -> GREEN `EXIT=0` |
| Existing scale-file normal/refusal paths | the other 7 cases in `MicrotunerScaleFileTest` | pass in both runs |
| GUI siblings that use LCD/controls | `AccessibilityWalkTest`, `KnobKeyboardTest`, `SamplerEnvelopeTest`, `SfzPlayerTest`, `MixerModesTest`, `MainWindowTest` | 7/7 pass |
| Audio thread | no audio code touched; all changes run in Qt widget construction/sizing on the GUI thread | not applicable |
| Platform/build | Qt 6.12 offscreen, `zene` and all test targets built | pass |

## Commands run and exit codes (copied from the runs)

| # | Command (from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `git diff -- include/MicrotunerConfig.h src/gui/MicrotunerConfig.cpp include/LcdFloatSpinBox.h src/gui/widgets/LcdFloatSpinBox.cpp src/gui/widgets/LcdWidget.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1114-menu-edit-scales-and-keymaps.r2/logs/fix.patch` | 0 | `fix.patch` |
| 2 | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1114-menu-edit-scales-and-keymaps.r2/logs/fix.patch` (revert the product fix, keep the tests) | 0 | (tool output) |
| 3 | `cmake --build build --target zene MicrotunerScaleFileTest` (RED) | 0 | `build-8-red.log` |
| 4 | `cd build/tests && ctest -R '^MicrotunerScaleFileTest$' --output-on-failure` (RED) | 8 | `test-8-red.log` |
| 5 | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1114-menu-edit-scales-and-keymaps.r2/logs/fix.patch` (restore) | 0 | (tool output) |
| 6 | `cmake --build build --target zene MicrotunerScaleFileTest AccessibilityWalkTest` (GREEN) | 0 | `build-9-green.log` |
| 7 | `cd build/tests && ctest -R '^(MicrotunerScaleFileTest\|AccessibilityWalkTest)$' --output-on-failure` | 0 (2/2) | `test-9-green.log` |
| 8 | `cmake --build build --target zene MicrotunerScaleFileTest AccessibilityWalkTest KnobKeyboardTest SamplerEnvelopeTest SfzPlayerTest MixerModesTest MainWindowTest` | 0 | `build-14-final.log` |
| 9 | `cd build/tests && ctest -R '^MicrotunerScaleFileTest$' -V` (both new names in the run) | 0 (1/1, 9 cases) | `test-15-final-verbose.log` |
| 10 | `cd build/tests && ctest -R '^(MicrotunerScaleFileTest\|AccessibilityWalkTest\|KnobKeyboardTest\|SamplerEnvelopeTest\|SfzPlayerTest\|MixerModesTest\|MainWindowTest)$' --output-on-failure` | 0 (7/7) | `test-17-family-final.log` |
| 11 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-menu-edit-scales-and-keymaps` | 0 (all 8 rows PASS) | `gate-1.log` |
| 11b | same `feature_check.py` after this report was written (8 changed files) | 0 (all 8 rows PASS) | `gate-2.log` |
| 12 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-menu-edit-scales-and-keymaps gui-1114-menu-edit-scales-and-keymaps` (before the report) | 1 (expected: nothing committed, report missing) | `submit-1.log` |

The RED run failed both new cases while the existing seven cases passed (`Totals: 7 passed, 2 failed`);
the GREEN run passed 2/2 and the final family run 7/7.

## Registration

`tests/upstream-modifications.txt:5934-5939` registers all six inherited product/test files changed by
this cluster, each with its reason. No new product file was added, so `tests/fork-sources.txt` is
unchanged.

## Files committed

- `include/MicrotunerConfig.h`
- `src/gui/MicrotunerConfig.cpp`
- `include/LcdFloatSpinBox.h`
- `src/gui/widgets/LcdFloatSpinBox.cpp`
- `src/gui/widgets/LcdWidget.cpp`
- `tests/src/gui/MicrotunerScaleFileTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-gui-1114-menu-edit-scales-and-keymaps.md` (this report)

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
  With the five product files reverted (`git apply -R logs/fix.patch`, `EXIT=0`), the test build
  succeeded (`EXIT=0`) and `ctest -R '^MicrotunerScaleFileTest$'` exited `8`, failing
  `keymapRangeControlsFitTheSizeHint` with
  `the Microtuner's size hint 300x400 is below its layout minimum 331x298` and
  `keymapLabelsReserveTheirFontMetrics` with every caption too wide/tall (`test-8-red.log`). With the
  fix restored (`git apply logs/fix.patch`, `EXIT=0`), the same build exited `0` and the same test
  exited `0` (2/2 in the run, `test-9-green.log`). The final verbose run names both new cases and
  passes all nine cases (`test-15-final-verbose.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
  values it uses).** Both tests are synchronous Qt geometry checks: they call `sizeHint()`,
  `QGridLayout::minimumSize()`, `layout()->activate()`, `mapTo()`/`contentsRect()` and
  `QFontMetrics::horizontalAdvance()/height()` on the already-created widget. There is no timer, no
  `QTest::qWait`, no event loop wait and no file/network input; the only environment dependency is
  the suite's existing `QT_QPA_PLATFORM=offscreen` test property. Font metrics and layout minima are
  computed from the same `tokens` font the product paints with, so the measurement is the contract,
  not a timing sample.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked
  and stays consistent.** `MicrotunerConfig::sizeHint()` has one product caller,
  `MainWindow::addWindowedWidget` (`src/gui/MainWindow.cpp:737-742`), which is exactly the window
  creation path in the finding; its `resize(delta + hint)` now gets a hint at/above the layout
  minimum. `MicrotunerConfig` is the only `LcdFloatSpinBox` instantiation in the product
  (`src/gui/MicrotunerConfig.cpp:178`), and `LcdFloatSpinBox::setLabel()` has only that caller
  (`:179`); its constructor initializes `m_fieldWidth` from the field layout before `updateSize()`.
  `LcdWidget::updateSize()` is called by `setLabel()` (`:187-191`), `setMarginWidth()` (`:196-201`)
  and `initUi()` (`:257`); all captions are painted with `tokens::font(TextRole::Caption)`
  (`src/gui/widgets/LcdWidget.cpp:175-181`), so the new reservation is the same font in every path.
  The old widget-font width and the old 9 px line are kept as lower bounds (`max(...)`), so no
  existing control shrinks. The layout minimum (`331` wide before the caption line grew; its height
  grows by the same 4 px as the controls) remains below the unchanged `500x700` maximum.
- **No collateral damage: old projects and saved data still load as before, and any limit or check you
  added does not hit other callers that legitimately exceed it.** No serialization, model, undo,
  command registration or audio-thread path changed: the diff is widget geometry and caption painting
  only. `sizeHint()` and `setMinimumSize()` affect only how the dialog is first placed/resized; old
  projects' saved scale/keymap data is untouched, and the existing normal/refusal scale-file cases in
  the same test binary still pass. The caption reservation only grows a control when its painted
  caption genuinely needs the space; `BASE NOTE FREQ`'s field stays `93` px wide (the caption is
  `85` px plus `2 * xxs` padding), and every other caption is narrower than its existing control.
  The family run (`7/7`) includes the other LCD/control users (`KnobKeyboardTest`,
  `SamplerEnvelopeTest`, `SfzPlayerTest`, `MixerModesTest`, `MainWindowTest`, `AccessibilityWalkTest`).
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract
  that shows it.** Not applicable: the verdict is `FIXED`. The contract that was violated is visible
  in `include/MicrotunerConfig.h:54` (old constant `QSize(300,400)`) versus the layout minimum
  `331x298` measured in the RED run, and in `LcdWidget::paintEvent()`/`LcdFloatSpinBox::paintEvent()`
  painting `Caption` text on controls whose `updateSize()` did not reserve its metrics.

The log folder for this run is
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1114-menu-edit-scales-and-keymaps.r2/logs`.
The pre-fix geometry probe is `test-6-debug.log`; the final RED/GREEN/family/gate logs are named in
the command table above.
