FIXED

# cluster gui-1106-main-punch-in-out-arm-a-range-at-the-pla — Punch arm shows no discernible range on the ruler

## Verdict: REAL

`TimeLineWidget::punchBandRect()` (`src/gui/editors/TimeLineWidget.cpp:318`) returns the band's
*row*: x from `m_xOffset` to `width()`. That row is right for layout (it reserves room under the loop
brace, `:163-165`) and for hit-testing (`getPunchAction`, which then checks the handles and the
interval by x). But `paintEvent` filled, outlined and labelled that whole row, so an armed punch range
painted as one uniform strip across the entire ruler. The only marks of the actual interval were the
two 5 px handles, and when a fresh project arms at tick 0 the in handle sat centred on `m_xOffset`,
half under the paint clip (`:157`). So in the finding's repro (fresh project, Arrange view, click Punch
in/out once) you could not see the one-bar range.

## Fix (`src/gui/editors/TimeLineWidget.cpp`)

- `punchIntervalRect()` (`:62`): the part of the band row between `markerX(punchBegin)` and
  `markerX(punchEnd)`, cut to the band. `paintEvent` (`:257`) now fills, outlines and labels only
  this rect. The row is still reserved across the ruler, so the loop-brace layout is unchanged.
- `punchHandleRect()` (`:72`): a handle whose bound is in view is clamped inside the band, so the in
  handle at the content edge is drawn whole. A bound scrolled out of view keeps its handle out of view,
  as before. Both `punchBeginHandleRect()` and `punchEndHandleRect()` (`:325`, `:332`) use it.
- Unchanged: `punchBandRect()` and therefore `getPunchAction()` hit-testing (which uses `markerX`, not
  the handle rects), the drag/commit path, the context menu, saved data. Nothing here touches the audio
  thread, undo or the command surface. No header change.

## Regression test

`ArrangerTakePunchTest::theArmedPunchRangeIsPaintedOnlyBetweenItsBounds`
(`tests/src/gui/ArrangerTakePunchTest.cpp:247`). It moves the loop out of the sampled columns, grabs
the ruler, arms the punch at play position 0 (giving the range 0..1 bar), then grabs again. It checks
that a pixel inside the interval changed and that a pixel in the band row past the range's end
(5 bars + 20 px) did not (`:278`). It also checks that the in handle starts at or after
`markerX(0)` and that the out handle is centred on `markerX(1 bar)`. The test is deterministic, with
no timing.

## Evidence (LOG = `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-punch-in-out-arm-a-range-at-the-pla.r1/logs`)

Build note: `01-build-red.log` failed (EXIT=2) because ccache could not create its temp file
(`Permission denied` under `dsh/home/ccache-tmp-r6`, a sandbox limit). Every later build ran with
`CCACHE_DISABLE=1`.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| RED build | `cmake --build build --target ArrangerTakePunchTest -j4 > LOG/03-build-red.log` | 0 | |
| RED test | `ctest -R '^ArrangerTakePunchTest$' --output-on-failure > LOG/04-test-red.log` (build/tests) | 8 | 4 passed, 1 failed: `:278` "the punch band is painted past the punch range's end" |
| GREEN build | `cmake --build build --target zene ArrangerTakePunchTest -j4 > LOG/05-build-green.log` | 0 | |
| GREEN test | `ctest -R '^ArrangerTakePunchTest$' --output-on-failure > LOG/06-test-green.log` | 0 | 100% of 1 |
| Family build | `cmake --build build --target VideoClipsGuiTest ArrangerOverlaysVideoLaneTest ArrangerTimelineTest -j4 > LOG/09-build-family.log` | 0 | |
| Family test | `ctest -R '^(ArrangerTakePunchTest\|VideoClipsGuiTest\|ArrangerOverlaysVideoLaneTest\|ArrangerTimelineTest)$' > LOG/10-test-family.log` | 0 | 4/4 tests passed |
| 4b revert | `git diff -- src/gui/editors/TimeLineWidget.cpp > LOG/fix.patch && git apply -R LOG/fix.patch` | 0 | |
| 4b RED | build `LOG/11-build-4b-red.log` EXIT=0; `ctest -R '^ArrangerTakePunchTest$' > LOG/12-test-4b-red.log` | 8 | same failure at `:278` |
| 4b reapply | `git apply LOG/fix.patch` | 0 | |
| 4b GREEN | build `zene ArrangerTakePunchTest` `LOG/13-build-4b-green.log` EXIT=0; `ctest -R '^ArrangerTakePunchTest$' > LOG/14-test-4b-green.log` | 0 | 100% of 1 |
| Gates (as given) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOG/15-feature-check.log` | 1 | crashed: `PermissionError` creating `<worktree>-feature-check` (outside the write jail), before running any gate |
| Gates (log dir redirected) | the same script exec'd unchanged except `logs = LOG/feature-check` (`LOG/16-feature-check.log`) | 0 | 8/8 PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item), file-length (no new item) |

Registration: `tests/upstream-modifications.txt` gets a new line for `src/gui/editors/TimeLineWidget.cpp`
(it ends with a newline). The test file is already in `tests/fork-sources.txt:2955`.

Files committed: `src/gui/editors/TimeLineWidget.cpp`, `tests/src/gui/ArrangerTakePunchTest.cpp`,
`tests/upstream-modifications.txt`, this report.

## Self-check

- Callers/siblings: `punchBandRect()` is used by `paintEvent` (layout reservation and paint),
  `punchBegin/EndHandleRect()` and `getPunchAction()`. Only the paint and the handle rects changed.
  The hit test is unchanged on purpose: a press anywhere in the row near a handle, or between the
  bounds, behaves exactly as before.
- No collateral damage: the existing `thePunchRangeHasItsOwnHandlesAndArmControl` (handle centres at
  3 and 4 bars, and the drags) still passes. The disarmed state keeps its outlined look, now limited
  to the interval.
- Realtime/undo/commands: GUI paint only. No new command and no undo change.
