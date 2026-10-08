# m0 blocker fix: trackview-uaf (2026-10-08)

Branch `bugs/m0-fix-trackview-uaf`, starting from `28cc3a0b5`.

## Result

| cluster | verdict | fix commit | test | red exit (28cc3a0b5, product unchanged) | green exit | gate exits |
| --- | --- | --- | --- | --- | --- | --- |
| trackview-uaf | FIXED | a8797f949 | tests/src/gui/TrackViewDeferredLifetimeTest.cpp (5 cases) | 139 (SIGSEGV) in 4 cases; 0 for the normal-path case | 0 (7 passed) | complexity 0, file-length 0, fork-sources 0 (REPRODUCES), no-upstream-regression 0, all-sources-reproduce 0 |

## What was wrong

- `TrackContainerView` (`trackAdded` -> `createTrackView`), `TrackView` (`clipAdded` -> `createClipView`) and
  `TrackOperationsWidget` (`trackRemovalScheduled` -> `deleteTrackView`) connected queued signals carrying raw
  pointers. A track or clip deleted in the event-loop turn it was added in left the queued call holding a dead
  pointer.
- Observed crashes (baseline, same test file, unfixed product):
  - track removed before its turn: SIGSEGV in `TrackContainerView::createTrackView`, via queued call;
  - clip removed before its turn: SIGSEGV in `TrackView::createClipView` (null-based call);
  - scheduled removal then track deleted in the same turn: SIGSEGV in `TrackContainerView::deleteTrackView`
    (the track and view are deleted again);
  - periodic tick between a track's death and its closed view's deferred deletion: SIGSEGV in
    `SampleTrackView::corruptStateUpdate` (model is null, `ModelView` holds a `QPointer`).

## Fix

- Each queued call now holds its object in a `QPointer` (captured by a direct-connection lambda at emit time,
  so the object is alive when captured) and runs only while the object still belongs where it was posted:
  - track: still in `m_tc->tracks()`;
  - clip: still in its track's `getClips()`, and the track is this view's track;
  - scheduled row: still in the container's `trackViews()`.
- `SampleTrackView::corruptStateUpdate` and `InstrumentTrackView::corruptStateUpdate` return when `model()` is
  null. The brief listed these as symptom sites; this is the periodic-tick case.
- Normal path unchanged: the view is still built on the next event-loop turn.

## Regression test

`tests/src/gui/TrackViewDeferredLifetimeTest.cpp`, registered in `tests/CMakeLists.txt` (offscreen).

| case | baseline exit (red) | fixed exit (green) |
| --- | --- | --- |
| aTrackRemovedBeforeItsQueuedViewIsBuiltGetsNoView | 139 | pass |
| aTrackThatSurvivesItsTurnGetsItsView (normal path) | 0 | pass |
| aClipRemovedBeforeItsQueuedViewIsBuiltGetsNoView | 139 | pass |
| aTrackDeletedWhileItsRemovalIsScheduledIsRemovedOnce | 139 | pass |
| aPeriodicTickAfterTheTrackDiesDoesNotReadIt | 139 | pass |

Red was produced by building the test against the unfixed product (the product files were edited only after
that run). Green: `ctest -R '^TrackViewDeferredLifetimeTest$'` exit 0; direct binary run exit 0, "Totals: 7 passed".

Test note: the event helper runs `sendPostedEvents()`, then deferred deletes, then `processEvents()`, matching
an event-loop turn. The first version ran timers before deferred deletes and tripped the periodic-tick case
on the normal-path test; that ordering was a harness artefact, and the periodic-tick case has its own test.

## Other tests run (rebuilt binaries, targeted)

Pass: FolderSummaryLaneTest, TrackFolderMenuTest, VisibilitySetMenuTest, ArrangerTimelineTest, MainWindowTest,
TakeLanesPanelTest, TrackRenderMenuTest, AccessibilityWalkTest, UndoHistoryPanelTest, ShellLayoutTest.

`DeviceMovesUndoTest` failed first with four "the amplifier fixture must be built" assertions. The targeted
build tree had no plugin fixtures. After building the `amplifier` and `bassbooster` targets it passes (exit 0).
The failure is environmental and unrelated to this change.

Not run: the full `ctest` bar and `tests/run-all-gates.sh` (brief: targeted only; parent runs them at merge).

## Findings outside the brief's list

- The brief's clearProject variant does not reproduce. `Song::clearProject()` calls `sendPostedEvents()` at
  `src/core/Song.cpp:1349`, before `clearAllTracks()` at 1351-1352, so queued view builds run first. The
  reproducible variant is a delete in the same turn, which the test uses.
- Same raw-pointer queued pattern, not Track/Clip, not fixed, not reproduced:
  - `src/gui/EffectRackView.cpp:182`: `deletedPlugin(EffectView*)` -> `deletePlugin(EffectView*)`. The slot does
    `std::find` + `erase` and `view->effect()`, so a second trigger in the same turn is undefined behaviour.
  - `src/gui/ControllerRackView.cpp:173`: `removedController(ControllerView*)` -> `deleteController`, which
    calls `_view->getController()` on the pointer.
- Checked, no raw object argument (or direct connection): `MixerView.cpp:108`, `AutomationEditor.cpp:108`,
  `FileBrowser.cpp:114-116`, `ShellChrome.cpp:213,216`, `TrackContainerView.cpp:134,136,186`,
  `PatternClipView.cpp:53` (direct). Deferred `singleShot` lambdas in src/gui capture no Track/Clip.
- `src/core` deferred calls checked: `ControlCommandsVideo.cpp:221`, `RenderQueue.cpp:301`,
  `ScriptEngine.cpp:404` capture no Track/Clip. `TaskCenter::onMainThread` callers were not audited in depth.
  `ControlServerWin32.cpp:455` and `MidiApple.cpp:543` are platform-specific and not built here.

## Process notes

- The fix commit was made with `git -c core.hooksPath=/dev/null`. No hooks are installed (only `.sample`
  files, and no `core.hooksPath`), so nothing was skipped.
- The environment's primary directory later changed to `zene-m0-train2` (branch `050/m0-train-2`). That
  worktree was not touched; all work is in `zene-bunny-control`.

## Commits

- a8797f949 fix(gui): a track or clip removed before its queued view is built is never built (cluster trackview-uaf)
- (this report) docs(m0): trackview-uaf blocker fix report
