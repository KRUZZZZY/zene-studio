FIXED

# new-p15-270: dropping onto an automation lane can divide by zero

## Verdict

REAL. `AutomationTrackView::dropEvent` (`src/gui/tracks/AutomationTrackView.cpp:59`)
turned the view's scale into the divisor of the dropped clip position with
`static_cast<int>(trackContainerView()->pixelsPerBar())`. The Pattern Editor is a
`TrackContainerView` that also hosts `AutomationTrackView` rows
(`src/gui/editors/PatternEditor.cpp:147` creates them) and it computes its scale by
integer truncation:

```
ppb = (width() - m_trackHeadWidth) * TimePos::ticksPerBar() / m_maxClipLength
```

(`src/gui/editors/PatternEditor.cpp:266-275`). When the content width times
ticks-per-bar is smaller than the longest clip the result truncates to 0, and the
drop then divides by 0. The regression test forces `width() - m_trackHeadWidth` to
exactly 0 so that branch is reached deterministically; a very long pattern reaches
the same branch with an ordinary width.

This session's RED run is the hard evidence: with only the product diff reversed,
the product method reports 0 px per bar and the slot fails
(`test-9-red.log`, EXIT 1: `a pattern editor with no content width reports 0 px per
bar`). With the diff applied the same slot passes (`test-10-green-restored.log`,
EXIT 0).

The Song Editor scale cannot reach this branch: it is clamped to
`[MIN_PIXELS_PER_BAR=4, MAX_PIXELS_PER_BAR=400]`
(`src/gui/editors/SongEditor.cpp:86-87,776-801`). The Pattern Editor was the missing
clamp.

## Fix

* `src/gui/editors/PatternEditor.cpp:266-276` - `updatePixelsPerBar` keeps the
  computed scale at `std::max(1, ppb)`. A zero (or negative) content width can no
  longer store a zero scale; every scale that was already >= 1 is unchanged.
* `src/gui/tracks/AutomationTrackView.cpp:66-77` - `dropEvent` reads the scale as a
  float, refuses to create a clip when it is not positive, and computes the tick
  offset with float division instead of truncating the scale to an `int` divisor.
  `update()` is still reached on every path. This is defence in depth for any other
  container; the product path is fixed at the source above.

Sibling check (all callers of the scale):

* `TrackContainerView::setPixelsPerBar` has two callers:
  `PatternEditor::updatePixelsPerBar` (clamped here, and the only call in
  `PatternEditor.cpp`) and `SongEditor::zoomingChanged`, which takes its value from
  `calculatePixelsPerBar` and is clamped to 4..400 (`SongEditor.cpp:776-801`).
* The truncating readers of the scale -
  `TrackContentWidget::getPosition` (`src/gui/tracks/TrackContentWidget.cpp:295`),
  `TrackContentWidget::updateBackground` (`:112`) and `paintEvent` (`:653`) - are
  downstream of those two sources and now always receive a scale >= 1.
  `dropEvent` additionally guards a non-positive scale itself.

## Regression test

`tests/src/gui/AutomationLaneChildGuiTest.cpp:499`
(`AutomationLaneChildGuiTest::aZeroWidthPatternEditorKeepsOnePixelPerBar`): clears
the editor's tracks and the pattern store, processes the queued row removal, asserts
there are no track views, sets the editor's fixed width to
`trackSettingsWidth + trackOperationsWidth` (so `width() - m_trackHeadWidth` is
exactly 0 and the pre-fix arithmetic yields 0), forces `updatePixelsPerBar` through
the meta-object with `Qt::DirectConnection`, records `pixelsPerBar()`, restores the
width, forces it again, and asserts the recorded scale is >= 1. No drag is simulated
and no timing is involved. The test file was already registered as a fork source.

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-270.r7/logs/`.
This session's runs:

| # | Command | EXIT | Log / result |
|---|---------|------|--------------|
| 1 | `cmake --build build --target AutomationLaneChildGuiTest -j4` | 0 | build-6-green.log, fix applied |
| 2 | `ctest -R AutomationLaneChildGuiTest --output-on-failure` (build/tests) | 0 | test-7-green.log, 1/1 passed |
| 3 | `QT_QPA_PLATFORM=offscreen ./AutomationLaneChildGuiTest aZeroWidthPatternEditorKeepsOnePixelPerBar -v1` | 0 | test-8-green-slot.log, slot PASS |
| 4 | `git diff a0070f8b8b^ a0070f8b8b -- src/gui/editors/PatternEditor.cpp src/gui/tracks/AutomationTrackView.cpp > logs/fix.patch` | 0 | 47-line product patch |
| 5 | `git apply -R --check logs/fix.patch` | 0 | patch reverses cleanly |
| 6 | `git apply -R logs/fix.patch` | 0 | product change reversed |
| 7 | `cmake --build build --target AutomationLaneChildGuiTest -j4` | 0 | build-7-red.log |
| 8 | `QT_QPA_PLATFORM=offscreen ./AutomationLaneChildGuiTest aZeroWidthPatternEditorKeepsOnePixelPerBar -v1` | 1 | test-9-red.log: FAIL, reports 0 px per bar |
| 9 | `git apply logs/fix.patch` | 0 | product change restored |
| 10 | `cmake --build build --target AutomationLaneChildGuiTest -j4` | 0 | build-8-green-restored.log |
| 11 | `QT_QPA_PLATFORM=offscreen ./AutomationLaneChildGuiTest -v1` | 0 | test-10-green-restored.log, 12 passed |
| 12 | `cmake --build build --target AccessibilityWalkTest ArrangerFileDropTest TrackRouteDropTest -j4` | 0 | build-9-family.log |
| 13 | `ctest -R "AutomationLaneChildGuiTest\|ArrangerFileDropTest\|TrackRouteDropTest\|AccessibilityWalkTest" --output-on-failure` | 0 | test-11-family.log, 4/4 passed |
| 14 | `python3 .../feature_check.py <worktree>` | 0 | feature-check-2.log, `RESULT: PASS` |
| 15 | `cmake --build build --target zene -j4` | 0 | build-10-zene.log |
| 16 | `python3 logs/pattern-shot.py build/zene logs/gui` | 0 | gui-shot-2.log, `window.screenshot` 1600x1000 |
| 17 | `python3 .../submit_check.py <worktree> new-p15-270` | 0 | submit-check-2.log, `RESULT: PASS - submit` |

RED/GREEN proof: step 8 ran the same slot with only the product diff reversed and
failed with `a pattern editor with no content width reports 0 px per bar` (EXIT 1);
step 11 with the fix restored passed 12/12 (EXIT 0).

GUI proof: `logs/gui/pattern-editor.png`, sha256
`a3cd9952f20e19a7def8a0daac0db2105ef8cb6e2990fcf6a6a215825d3ff775`, captured from
the current `build/zene` through the control socket's `window.screenshot` with the
Pattern Editor panel open.

## Registration

`tests/upstream-modifications.txt` carries one reason line each for
`src/gui/editors/PatternEditor.cpp` and `src/gui/tracks/AutomationTrackView.cpp`,
and the file ends with a newline. `tests/src/gui/AutomationLaneChildGuiTest.cpp`
was already a fork source.

## Self-check

1. **The regression test fails with the product change reverted and passes with
   it.** Yes, this session. Reversed product diff: `test-9-red.log`, EXIT 1,
   `FAIL! ... 'narrowPixelsPerBar >= 1.0f' returned FALSE. (a pattern editor with no
   content width reports 0 px per bar)`. Restored fix: `test-10-green-restored.log`,
   EXIT 0, `Totals: 12 passed, 0 failed`.
2. **The test is deterministic.** It injects the layout instead of waiting for one:
   `clearAllTracks()` plus one `processEvents()` for the queued row removal, a fixed
   `setFixedWidth(trackSettingsWidth + trackOperationsWidth)`, and synchronous
   `QMetaObject::invokeMethod(..., "updatePixelsPerBar", Qt::DirectConnection)`
   calls. No timer, sleep, thread, drag or wall-clock value is used.
3. **The fix is at the cause and complete.** Yes: the only truncating producer of
   the scale (`PatternEditor::updatePixelsPerBar`) is clamped, the other producer
   (`SongEditor::zoomingChanged`) is already bounded at 4..400, and the readers
   (`TrackContentWidget.cpp:112,295,653`) receive the same invariant.
   `AutomationTrackView::dropEvent` no longer truncates the scale at all and refuses
   a non-positive one.
4. **No collateral damage.** The clamp changes only values that were 0 or negative
   (the crash path); every legitimate scale >= 1 is bit-for-bit unchanged, so
   rendering and saved projects are unaffected. The float offset truncates to the
   same tick as the old integer division for the non-negative offsets a drop
   produces. No data format, command or undo state changed.
5. **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is
   FIXED, and the RED run above shows the defect was present on this branch.
6. **Sibling commands in the command files the diff touched.** None: this fix
   touches no command file. `git show --stat a0070f8b8b` lists only
   `PatternEditor.cpp`, `AutomationTrackView.cpp`, the GUI test, the registration
   list and the report; this round's commit adds only the report. The `app.*` and
   `arrangement.*` ids in the submit check's ASK list come from the train merges
   already in the base..HEAD range, not from this fix, so their handlers, schemas
   and reversibility classes are untouched.

- **Realtime?** Both files are GUI code; nothing was added to an audio-thread path
  (no allocation, lock, wait, I/O or logging).
- **Undo/commands?** No command was added. For a positive scale the drop creates the
  same clip at the same tick as before, so the undo step is unchanged; a
  non-positive scale previously crashed instead of creating a step.
- **Platforms?** Portable C++20 and existing Qt APIs only. The Qt 5 matrix was not
  run here, per this round's instruction.
- **Gates?** `feature_check.py` EXIT 0, `RESULT: PASS`; complexity and file-length
  report no new item in the changed files.

## Other findings

- `TrackContentWidget::getPosition` (`src/gui/tracks/TrackContentWidget.cpp:295`)
  and `paintEvent` (`:653`) still divide or tile by a `static_cast<int>` of the
  scale. They are protected now by the >= 1 invariant, but a future container view
  that reports 0 would divide by zero there. Not changed beyond this finding.
- `TrackContentWidget::updateBackground` (`:112`) would take the log of a zero
  pixel spacing if a zero scale ever returned; the same invariant protects it.
