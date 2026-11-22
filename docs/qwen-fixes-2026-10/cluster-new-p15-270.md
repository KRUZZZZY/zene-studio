FIXED

# new-p15-270: dropping onto an automation lane can divide by zero

## Verdict

REAL. `AutomationTrackView::dropEvent` divided the dropped position by
`static_cast<int>(trackContainerView()->pixelsPerBar())`. In the Pattern Editor,
`PatternEditor::updatePixelsPerBar` computed `(width() - m_trackHeadWidth)` as the
scale when the editor had no clip length. A Pattern Editor narrower than its track
head therefore stored 0 pixels per bar, and dropping an automatable model onto one
of its automation lanes divided by zero.

Before the fix, the hard evidence is the regression test run against the product
code with this change reversed: `a pattern editor with no content width reports 0
px per bar` (log `test-3-red.log`, EXIT 1). The same slot passes with the fix
(`test-4-green-restored.log`, EXIT 0).

## Fix

* `src/gui/editors/PatternEditor.cpp:266-277` - `updatePixelsPerBar` computes the
  content width and clamps the value handed to `setPixelsPerBar` with
  `std::max(1, ppb)`. A zero (or negative) content width can no longer store a
  zero scale. Normal widths are unchanged because the clamp only applies below 1.
* `src/gui/tracks/AutomationTrackView.cpp:59-84` - `dropEvent` reads
  `pixelsPerBar()` as a float, refuses to create a clip when it is not positive,
  and computes the tick offset in float instead of truncating the scale to `int`.
  The existing `update()` is still reached on every path. This is defence in depth
  for any other container view; the product path is fixed at the source above.

Sibling check: the other truncating users of the scale
(`TrackContentWidget::getPosition` at `src/gui/tracks/TrackContentWidget.cpp:295`,
`TrackContentWidget::updateBackground` at `:112`, `paintEvent` at `:653`) are
protected by the same invariant: no `TrackContainerView` can now report 0. The
Song Editor's scale comes from `MIN_PIXELS_PER_BAR`, not from a content width
(`src/gui/editors/SongEditor.cpp:789-801`), so it was never zero.

## Regression test

`tests/src/gui/AutomationLaneChildGuiTest.cpp:499`
(`aZeroWidthPatternEditorKeepsOnePixelPerBar`): clears the pattern store, processes
events, asserts the Pattern Editor has no rows, sets the editor's fixed width to
`trackSettingsWidth + trackOperationsWidth` so the old arithmetic yields exactly 0,
forces `updatePixelsPerBar` through the meta-object, records the reported scale,
restores the width, forces it again, and asserts the recorded scale is >= 1.
No drag is simulated and no timing is involved.

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-270.r7/logs/`.

| # | Command | EXIT | Result |
|---|---------|------|--------|
| 1 | `cmake --build build --target AutomationLaneChildGuiTest -j4` | 0 | build-1.log, fix applied |
| 2 | `ctest -R AutomationLaneChildGuiTest --output-on-failure` (build/tests) | 0 | test-1-green.log, 1/1 passed |
| 3 | `QT_QPA_PLATFORM=offscreen ./AutomationLaneChildGuiTest -v1` | 0 | test-2-green-verbose.log, slot PASS, 12 passed |
| 4 | `git diff -- src/gui/editors/PatternEditor.cpp src/gui/tracks/AutomationTrackView.cpp > logs/fix.patch` | 0 | fix.patch |
| 5 | `git apply -R logs/fix.patch` | 0 | product change reversed |
| 6 | `cmake --build build --target AutomationLaneChildGuiTest -j4` | 0 | build-2-red.log |
| 7 | `QT_QPA_PLATFORM=offscreen ./AutomationLaneChildGuiTest aZeroWidthPatternEditorKeepsOnePixelPerBar -v1` | 1 | test-3-red.log: FAIL, reports 0 px per bar |
| 8 | `git apply logs/fix.patch` | 0 | product change restored |
| 9 | `cmake --build build --target AutomationLaneChildGuiTest -j4` | 0 | build-3-green.log |
| 10 | `QT_QPA_PLATFORM=offscreen ./AutomationLaneChildGuiTest -v1` | 0 | test-4-green-restored.log, slot PASS, 12 passed |
| 11 | `cmake --build build --target AccessibilityWalkTest ArrangerFileDropTest TrackRouteDropTest -j4` | 0 | build-4-family.log |
| 12 | `ctest -R "AutomationLaneChildGuiTest|ArrangerFileDropTest|TrackRouteDropTest|AccessibilityWalkTest" --output-on-failure` | 0 | test-6-family.log, 4/4 passed |
| 13 | `python3 .../feature_check.py <worktree>` | 0 | feature-check-1.log, RESULT: PASS |
| 14 | `cmake --build build --target zene -j4` | 0 | build-5-zene.log |
| 15 | `python3 logs/pattern-shot.py build/zene logs/gui` | 0 | gui-shot.log, `window.screenshot` 1600x1000 |

RED/GREEN proof: step 7 ran the same slot with only the product diff reversed and
failed with `a pattern editor with no content width reports 0 px per bar`; step 10
with the fix restored passed.

GUI screenshot: `logs/gui/pattern-editor.png`, sha256
`ec30f70b2a6fb352c80561f50512cdd87be06825b49584a6273af3a09fef6d2b`, taken through
the control socket's `window.screenshot` with the Pattern Editor panel shown.

## Registration

`tests/upstream-modifications.txt` gains one reason line each for
`src/gui/editors/PatternEditor.cpp` and `src/gui/tracks/AutomationTrackView.cpp`.
`tests/src/gui/AutomationLaneChildGuiTest.cpp` was already a fork source.

## Self-check

Answers to the pre-submit check's questions:

1. **The regression test fails with the product change reverted and passes with
   it (exit codes).** Yes. Reversed product diff: EXIT 1, `FAIL! ...
   a pattern editor with no content width reports 0 px per bar`
   (`test-3-red.log`). Restored fix: EXIT 0, slot PASS
   (`test-4-green-restored.log`).
2. **The test is deterministic: no wall-clock timing, sleeps or races.** Yes. It
   sets a fixed width, invokes the `updatePixelsPerBar` slot synchronously with
   `Qt::DirectConnection`, reads `pixelsPerBar()` before restoring the width, and
   uses one `QCoreApplication::processEvents()` only for the queued row removal.
   There is no timer, sleep or thread.
3. **The fix is at the cause and complete: every caller and sibling checked.**
   Yes. `setPixelsPerBar` has two callers: `PatternEditor::updatePixelsPerBar`
   (clamped here) and `SongEditor::zoomingChanged`, whose scale comes from
   `MIN_PIXELS_PER_BAR` (`src/gui/editors/SongEditor.cpp:789-801`) and is never 0.
   The truncating readers of the scale (`TrackContentWidget.cpp:112, 295, 653`)
   are protected by the new >= 1 invariant; `dropEvent` additionally guards a
   non-positive scale itself.
4. **No collateral damage.** The clamp changes only a scale that was 0 or
   negative (a crash path); every legitimate scale >= 1 is unchanged, so saved
   projects load and render as before. The float tick offset truncates to the
   same tick for integer scales. No command or undo state was added.
5. **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is
   FIXED. (The red run above shows the defect was real on this branch.)
6. **Sibling commands in the command files you changed.** No command file was
   changed in this fix. `app.*`, `arrangement.*` and every other command listed
   are untouched, so none of their handlers or schemas is affected.

- **Test proves the fix?** Yes. With only the product diff reversed the same slot
  failed (EXIT 1) with the recorded 0 px per bar; with the fix it passed (EXIT 0).
- **Realtime?** No audio-thread code touched. Both files are GUI code; no
  allocation, lock, wait or logging was added on a render path.
- **Undo/commands?** No command was added. The drop still creates its clip exactly
  as before when the scale is positive; a non-positive scale previously crashed,
  so nothing can observe a different undo sequence.
- **Collateral damage?** The clamp changes only values below 1 (previously 0 or
  negative); every legitimate scale >= 1 is unchanged. The float offset keeps the
  same truncation to ticks for positive integer scales. Project load/save is
  untouched.
- **Platforms?** Change is portable C++20, no platform-specific code. Qt 5 matrix
  was not run, per the round-7 instruction.
- **Gates?** `feature_check.py` returned EXIT 0 with `RESULT: PASS`; complexity and
  file-length report no new item in the changed files.

## Other findings

- `TrackContentWidget::getPosition` (`src/gui/tracks/TrackContentWidget.cpp:295`)
  also divides by the truncated scale with no local guard. It is protected now by
  the >=1 invariant, but a future view that reports 0 would divide by zero there.
  Not changed beyond the scope of this finding.
- `TrackContentWidget::updateBackground` (`:112`) takes `std::log2` of a zero
  pixel spacing if a scale of 0 ever returned; same invariant protects it.
