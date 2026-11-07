FIXED

# Cluster gui-1030-main-punch-in-out-arm-a-range-at-the-pla: punch band spans the ruler

## Decision: REAL

`TimeLineWidget::punchBandRect()` returned a rectangle whose left edge was the ruler's left offset and whose
width was the whole remaining ruler (`src/gui/editors/TimeLineWidget.cpp:291` before the fix):

```cpp
return QRect(m_xOffset, height() - bandHeight, std::max(0, width() - m_xOffset), bandHeight);
```

The punch in/out ticks were only used for the handles (`punchBeginHandleRect()` / `punchEndHandleRect()`),
so arming a one-bar punch range drew a full-width band instead of one that bounds that bar. The band is now
clamped to the punch markers and to the visible ruler
(`src/gui/editors/TimeLineWidget.cpp:284-297`):

```cpp
const int visibleRight = std::max(m_xOffset, width());
const int beginX = std::clamp(markerX(TimePos(m_timeline->punchBegin())), m_xOffset, visibleRight);
const int endX = std::clamp(markerX(TimePos(m_timeline->punchEnd())), m_xOffset, visibleRight);
return QRect(beginX, height() - bandHeight, std::max(0, endX - beginX), bandHeight);
```

The sibling path had to follow: `getPunchAction()` used `band.contains()` for its horizontal gate, and after
narrowing, a press on the out handle (whose centre is the band's right edge) fell outside the half-open
rectangle and stopped dragging the range. It now requires the band's row, then applies the existing
nearest-handle rules (`src/gui/editors/TimeLineWidget.cpp:357-375`). The handle rectangles themselves are
unchanged and still centred on the markers. Rendering callers (`paintEvent`, which also reserves the loop
brace's room) need no change: they use the returned rectangle directly.

The regression is in `ArrangerTakePunchTest::thePunchRangeHasItsOwnHandlesAndArmControl`
(`tests/src/gui/ArrangerTakePunchTest.cpp:189-192`, `:204-205`, `:220-221`, `:238-239`, `:246`). It asserts
the band's left/right map to the in/out markers after arming, after each handle drag, after undo and after a
`transport.punch_set`, and that a range entirely beyond the visible ruler has no band. The drag assertions
also prove the out/in handles still grab the range.

## RED/GREEN proof

Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-main-punch-in-out-arm-a-range-at-the-pla.r1/logs/`.

| Step | Command | Exit |
|------|---------|------|
| Build regression on original code | `cmake --build build --target ArrangerTakePunchTest` (`build-1-red.log`) | 0 |
| RED on original code | `cd build/tests && ctest -R '^ArrangerTakePunchTest$' --output-on-failure` (`test-1-red.log`) | 8 |
| Build fixed product + test | `cmake --build build --target zene ArrangerTakePunchTest` (`build-4-green.log`) | 0 |
| GREEN with fix | `cd build/tests && ctest -R '^ArrangerTakePunchTest$' --output-on-failure` (`test-4-green.log`) | 0 |
| Build related tests | `cmake --build build --target ArrangerTimelineTest VideoClipsGuiTest ArrangerOverlaysVideoLaneTest` (`build-5-family.log`) | 0 |
| Related family | `cd build/tests && ctest -R '^(ArrangerTakePunchTest\|ArrangerTimelineTest\|VideoClipsGuiTest\|ArrangerOverlaysVideoLaneTest)$' --output-on-failure` (`test-5-family.log`) | 0 (4/4 passed) |
| Product patch | `git diff -- src/gui/editors/TimeLineWidget.cpp > $LOG/fix.patch` | 0 |
| Reverse it | `git apply -R $LOG/fix.patch` | 0 |
| Rebuild without fix | `cmake --build build --target zene ArrangerTakePunchTest` (`build-6-without-fix.log`) | 0 |
| RED without fix (proves the test catches it) | `cd build/tests && ctest -R '^ArrangerTakePunchTest$' --output-on-failure` (`test-6-without-fix.log`) | 8 |
| Restore fix | `git apply $LOG/fix.patch` | 0 |
| Rebuild with fix | `cmake --build build --target zene ArrangerTakePunchTest` (`build-7-refix.log`) | 0 |
| GREEN restored | `cd build/tests && ctest -R '^ArrangerTakePunchTest$' --output-on-failure` (`test-7-refix.log`) | 0 |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`feature_check-1.log`) | 0 (RESULT: PASS) |

The RED assertion was `Actual (band.left()): 338` versus `Expected (ruler->markerX(TimePos(3 * Bar))): 458`
at `ArrangerTakePunchTest.cpp:191` (338 is `m_xOffset`; 458 is three bars at 40 px/bar). The same assertion
fails again after reversing only the product patch and passes once it is restored.

## Scope and safety

- Only the ruler's GUI geometry changes. Timeline data, save/load and the punch commands (`transport.punch_set`,
  `transport.punch_clear`) are untouched; projects load exactly as before.
- No new command, allocation, lock or audio-thread path: the changed functions only run in `paintEvent`,
  mouse handlers and tests on the GUI thread.
- Undo is untouched: the drag still previews on the timeline and commits one `transport.punch_set`; the test
  re-checks the single undo step.
- A punch range fully outside the visible ruler now yields an empty band (the range has no visible extent);
  a partially visible range's band is clipped to the ruler. The loop brace's own paint and drag are unchanged.

## Self-check

- **Regression FAILS reverted, PASSES fixed:** `test-6-without-fix.log` exit 8 (band.left 338 vs 458) with
  the product patch reversed; `test-7-refix.log` exit 0 after `git apply $LOG/fix.patch`.
- **Deterministic:** no wall-clock timing, sleeps or races. The test uses the real `TimeLineWidget` geometry
  after a fixed resize, the deterministic `markerX(TimePos(...))` mapping, and explicit command calls; the
  offscreen case uses a fixed 100-bar range instead of scrolling.
- **Cause and completeness:** `punchBandRect()` was the only full-width rectangle; `paintEvent` and
  `getPunchAction` are its only callers. `getPunchAction` was updated so the end handles remain draggable
  (the new test failed on the drag until that was done), and the handle/undo paths were re-run.
- **No collateral damage:** no serialization or command-schema change; old projects load as before. The only
  new boundary is GUI visibility (offscreen ranges draw no band); no product limit was added that another
  caller could exceed.
- **Not an ALREADY-FIXED/FALSE-POSITIVE claim:** the original full-width return is quoted above, and the
  regression fails on that exact tree. The "code that shows the verdict" ask therefore does not apply.

## Completion check

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> gui-1030-main-punch-in-out-arm-a-range-at-the-pla`
exit 0 (RESULT: PASS - submit), in `submit_check-1.log`; every ASK item above is answered with the
RED/GREEN exit codes and the deterministic seam named in this Self-check.
