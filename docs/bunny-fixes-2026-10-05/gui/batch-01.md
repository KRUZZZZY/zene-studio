# Space Bunny fixer lane `gui` — batch 01 (2026-10-05)

Branch `bugs/bunny-gui-2026-10-05`. Six findings, two fixed, two false-positive,
two real but not provable headlessly. Both gates green on the final tree:
`tests/complexity-gate.sh --check` EXIT=0, `tests/file-length-gate.sh --check` EXIT=0.

### Cluster 5479 — A pending value announcement can fire after focus leaves the control

Verdict: REAL-UNTESTED

Evidence: `src/gui/AccessibleWidgets.cpp:76` — the settle timer's timeout calls
`announce()` on `m_pending` with no focus test, while `noteChange()` only records a
change `while m_control->hasFocus()` (`src/gui/AccessibleWidgets.cpp:84`). A change noted
in the last 250 ms before focus leaves is therefore announced by the control that has
already lost focus. The reported line matches the current code.

Commit: none

Test: none

Red/green: none. No headless test can observe it: the announcement leaves through
`QAccessible::updateAccessibility()` (`src/gui/AccessibleWidgets.cpp:109,114`) to a
screen-reader bridge that `QT_QPA_PLATFORM=offscreen` does not have, and
`QAccessible::isActive()` is false there, so `noteChange()` never runs at all. On top of
that `ValueAnnouncer` lives in an anonymous namespace, so it cannot be driven from a test.
Per the lane rule (no product change without a deterministic failing test) the defect is
reported, not fixed.

### Cluster 5488 — Pinch zoom uses the wrong anchor coordinates

Verdict: REAL-UNTESTED

Evidence: `src/gui/ArrangerAids.cpp:146` passes
`pinch->startCenterPoint().toPoint()` straight to `arrangerzoom::tickAtGlobal()`, which
takes a SCREEN point — it does `editor->timeLine()->mapFromGlobal(global)` and is
documented as "a screen point" (`include/ArrangerZoom.h:52`, `src/gui/ArrangerZoom.cpp:87`).
`QPinchGesture::startCenterPoint()` is in the gesture widget's own coordinates, so with the
arranger off the top-left of the screen the anchor tick is wrong. The wheel over the
arranger does it correctly with `we->globalPosition()` (`src/gui/editors/SongEditor.cpp:484`),
which is what the pinch path is missing.

Commit: none

Test: none

Red/green: none. The pinch path cannot be driven headlessly: `PinchZoom` is file-local, and
`QGestureEvent::gesture(Qt::PinchGesture)` (`src/gui/ArrangerAids.cpp:129`) returns nullptr for
any event a test can construct — `QGestureEvent`'s public constructor builds an event with no
gesture of its own, and the offscreen platform delivers no gesture events, so `begin()` is
never reachable from a test. Reported, not fixed, per the lane rule.

### Cluster 4579 — Navigator clips disappear when the track list is taller than the strip

Verdict: FALSE-POSITIVE

Evidence: `src/gui/ArrangerNavigator.cpp:253-271` (`paintClips`) already aggregates rows when
tracks outnumber them: `rows = clamp(room / MinRow, 1, count)` and a track's row is
`y = top + track * rows / count * rowHeight`, so `rows * rowHeight <= room` and the last row
ends at or before `room = height - top - LoopBand - 1`. That is the documented behaviour
("when there are more tracks than rows, tracks share a row",
`src/gui/ArrangerNavigator.cpp:84,112`). No clip row is painted below the widget, so nothing
is clipped away. The report read `rowHeight`'s clamp to 1 and missed the `rows` clamp above it.

Commit: none

Test: none (no code change)

Red/green: none

### Cluster 4580 — Navigator's accessible range reports one extra bar at exact boundaries

Verdict: FIXED

Evidence: `src/gui/ArrangerNavigator.cpp:224` (before the fix) built the description from
`(start + view) / ticksPerBar() + 1`, but `view` is `arrangerzoom::visibleTicks()`, an
EXCLUSIVE end (`src/gui/ArrangerZoom.cpp:82`), so a view ending exactly on a bar line named
the bar after the last one on screen. The last bar named is now the one the view ends inside.

Commit: 22366e812

Test: `ArrangerTimelineTest::theNavigatorNamesTheLastBarItShows`

Red/green:
- Red: `cmake --build build --target ArrangerTimelineTest zene` EXIT=0;
  `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` EXIT=8
  (`Actual 124, Expected 123`).
- Green: same build EXIT=0, same ctest EXIT=0.

### Cluster 5486 — A range endpoint in a track gap resolves to the last track

Verdict: FALSE-POSITIVE

Evidence: the premise is unreachable. `nearestTrackAt()` (`src/gui/ArrangerOverlays.cpp:116-121`)
does return the last track for a point between two rows, but no such point exists: probed in the
live arranger (GuiTestApplication, two tracks, `ArrangerRangeOverlay::trackAt()` scanned for
every y from the first row's top to the last row's bottom) there is NO y in that span for which
`trackAt()` returns nullptr — the track rows tile the viewport, so rows are contiguous and
`y >= area.top() && y <= area.bottom()` always hits. The only off-row points are above the first
row and below the last, and both already resolve to the nearest end row, as
`include/ArrangerOverlays.h:87` documents.

Commit: none

Test: none — the probe case was not committed (it asserted the gap's existence rather than a
defect, and the gap does not exist)

Red/green: probe only, in the red run below:
`aRangeEndpointInAGapTakesTheNearestTrack() 'gap >= 0' returned FALSE. (the track rows tile the
viewport, so there is no gap between them)`.

### Cluster 4578 — Switching tools during an audition can leave audition active

Verdict: FIXED

Evidence: `src/gui/ArrangerOverlays.cpp:70` — `setTool()` replaced `m_tool` without ending a
held audition, and the release path `finishGesture()` (`src/gui/ArrangerOverlays.cpp:206-220`)
switches on the CURRENT `m_tool`, so a gesture begun with AUDITION finished with the tool that
replaced it: the release dispatched a range (`default: dispatchRange(to)`) and never called
`transport.audition_stop`, leaving `audition_start`'s silenced tracks muted. A tool change now
ends a held audition first, matching how `event()` already ends one on
WindowDeactivate/UngrabMouse/Hide (`src/gui/ArrangerOverlays.cpp:165-177`).

Commit: 7fe9c83e7

Test: `ArrangerTimelineTest::switchingToolsDuringAHeldAuditionStopsIt` (press with AUDITION,
`arrangement.set_tool` to `range` while the button is still held, release: the audition's mutes
come back and no range is dispatched)

Red/green:
- Red: `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` EXIT=8
  (the held audition was still sounding after the release).
- Green: `cmake --build build --target ArrangerTimelineTest zene` EXIT=0;
  `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` EXIT=0 (15 cases).

Leftovers: clusters 5479 and 5488 are real defects in the tree and were NOT fixed (no headless
test can drive the a11y settle timer or a pinch gesture); cluster 5486 stays as-is, so if a
future track layout ever leaves a gap between rows `nearestTrackAt()` would again take the last
track — the reachability, not the code, was disproved.