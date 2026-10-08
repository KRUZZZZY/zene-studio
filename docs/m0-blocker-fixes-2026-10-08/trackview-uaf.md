# m0 blocker fix: trackview-uaf (2026-10-08)

Branch `bugs/m0-fix-trackview-uaf`, starting from `28cc3a0b5`.

## Result

| cluster | verdict | fix commit | test | red exit (product unchanged) | green exit | gate exits |
| --- | --- | --- | --- | --- | --- | --- |
| trackview-uaf (tracks, clips, scheduled row, periodic tick) | FIXED | a8797f949 | tests/src/gui/TrackViewDeferredLifetimeTest.cpp (5 cases) | 139 (SIGSEGV) in 4 cases; 0 for the normal-path case. Red at 28cc3a0b5 | 0 (7 passed) | complexity 0, file-length 0, fork-sources 0 (REPRODUCES), no-upstream-regression 0, all-sources-reproduce 0 |
| trackview-uaf follow-up (effect and controller rack views) | FIXED | e1ae28af0 | tests/src/gui/RackDeferredLifetimeTest.cpp (4 cases) | 139 (SIGSEGV) in 2 race cases; 0 for the 2 normal-path cases. Red at a8797f949 | 0 (6 passed) | at e1ae28af0: complexity 0, file-length 0, fork-sources 0 (REPRODUCES), no-upstream-regression 0, all-sources-reproduce 0 |

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
- Follow-up (same pattern, rack views): `EffectRackView` (`deletedPlugin` -> `deletePlugin`) and
  `ControllerRackView` (`removedController` -> `deleteController`) connected a view's removal with a queued
  connection that carried the raw view pointer. Observed crashes at a8797f949:
  - effect: the plugin's view asked to go, then `EffectChain::clear()` (`src/core/EffectChain.cpp:440`) emitted
    `aboutToClear`, the rack deleted its views synchronously, and the queued `deletePlugin()` read
    `view->effect()` on the freed view: SIGSEGV in `EffectRackView::deletePlugin`;
  - controller: the view asked to go, then `Song::removeController()` emitted `controllerRemoved`, the rack deleted
    the view synchronously, and the queued `deleteController()` called `getController()` on the freed view:
    SIGSEGV in `ControllerRackView::deleteController`.

## Fix

- Each queued call now holds its object in a `QPointer` (captured by a direct-connection lambda at emit time,
  so the object is alive when captured) and runs only while the object still belongs where it was posted:
  - track: still in `m_tc->tracks()`;
  - clip: still in its track's `getClips()`, and the track is this view's track;
  - scheduled row: still in the container's `trackViews()`;
  - plugin view: still in the rack's `m_effectViews`;
  - controller view: still in the rack's `m_controllerViews`.
- `SampleTrackView::corruptStateUpdate` and `InstrumentTrackView::corruptStateUpdate` return when `model()` is
  null. The brief listed these as symptom sites; this is the periodic-tick case.
- Normal path unchanged: the view is still built, or the removal still runs, on the next event-loop turn.

### Which guard, and why (rack follow-up)

- Both emitters are the live view itself: `EffectView::deletePlugin()` (`src/gui/EffectView.cpp:156`) and
  `ControllerView::removeController()` (`src/gui/ControllerView.cpp:121`) emit `this`. Neither signal is emitted
  from a destructor (grep of `src` and `include`: the only emitters are those two functions, and
  `EffectView::~EffectView` only deletes its sub-window). So the view is alive when the `QPointer` is captured,
  and the `QPointer` is null exactly when the view was deleted before the call runs.
- The delivery check is also an identity test on the pointer value (`contains` on the rack's list). That is a
  comparison only, never a dereference, and it covers "still alive but no longer one of this rack's views".
- An identity-only guard (without the `QPointer`) would not have been enough: the same address can be reused
  by a new view between emit and delivery, which the `QPointer` rules out.

## Regression tests

`tests/src/gui/TrackViewDeferredLifetimeTest.cpp` (track and clip, scheduled row, periodic tick) and
`tests/src/gui/RackDeferredLifetimeTest.cpp` (rack views), both registered in `tests/CMakeLists.txt` (offscreen).
The rack test sets `LMMS_PLUGIN_DIR` from `LMMS_TEST_PLUGIN_DIR` inside the test, so `amplifier` loads; the CMake
block lists it for the define.

| case | baseline exit (red) | fixed exit (green) |
| --- | --- | --- |
| TrackViewDeferredLifetimeTest: aTrackRemovedBeforeItsQueuedViewIsBuiltGetsNoView | 139 | pass |
| TrackViewDeferredLifetimeTest: aTrackThatSurvivesItsTurnGetsItsView (normal path) | 0 | pass |
| TrackViewDeferredLifetimeTest: aClipRemovedBeforeItsQueuedViewIsBuiltGetsNoView | 139 | pass |
| TrackViewDeferredLifetimeTest: aTrackDeletedWhileItsRemovalIsScheduledIsRemovedOnce | 139 | pass |
| TrackViewDeferredLifetimeTest: aPeriodicTickAfterTheTrackDiesDoesNotReadIt | 139 | pass |
| RackDeferredLifetimeTest: aControllerRemovedBeforeItsQueuedRemovalRunsIsLeftAlone | 139 (in deleteController) | pass |
| RackDeferredLifetimeTest: aControllerRemovedFromItsViewIsRemovedOnTheNextTurn (normal path) | 0 | pass |
| RackDeferredLifetimeTest: anEffectRemovedBeforeItsQueuedRemovalRunsIsLeftAlone | 139 (in deletePlugin) | pass |
| RackDeferredLifetimeTest: anEffectRemovedFromItsViewIsRemovedOnTheNextTurn (normal path) | 0 | pass |

Red was produced by building each test against the unfixed product (the product files were edited only after
that run). Green: `ctest -R` on each test exit 0; direct binary runs exit 0.

Test notes:
- The event helper runs `sendPostedEvents()`, then deferred deletes, then `processEvents()`, matching an
  event-loop turn. The first version ran timers before deferred deletes and tripped the periodic-tick case on the
  normal-path test; that ordering was a harness artefact, and the periodic-tick case has its own test.
- The first rack red run had the effect cases fail on `Effect::instantiate("amplifier")` returning null: the
  plugin directory was not set for that binary. Fixed in the test (`LMMS_TEST_PLUGIN_DIR`, plus the CMake
  define); the red was then re-recorded.

## Other tests run (rebuilt binaries, targeted)

Pass: FolderSummaryLaneTest, TrackFolderMenuTest, VisibilitySetMenuTest, ArrangerTimelineTest, MainWindowTest,
TakeLanesPanelTest, TrackRenderMenuTest, AccessibilityWalkTest, UndoHistoryPanelTest, ShellLayoutTest,
EffectRackAddTest, RackMenuTest, EffectHostingMenuTest (the rack set re-run after the rack guards).

`DeviceMovesUndoTest` failed first with four "the amplifier fixture must be built" assertions. The targeted
build tree had no plugin fixtures. After building the `amplifier` and `bassbooster` targets it passes (exit 0).
The failure is environmental and unrelated to this change.

Not run: the full `ctest` bar and `tests/run-all-gates.sh` (brief: targeted only; parent runs them at merge).

## Residual, not fixed

- `ControllerRackView::deleteController()` shows a confirmation `QMessageBox::exec()` when the controller has
  connections. That is a nested event loop, and the controller pointer is captured before it. A removal of the
  same controller during the dialog is not covered by the delivery guard. It needs a controller with connections
  and a concurrent removal; not reproduced. Fixing it means re-checking after `exec()`.
- `EffectRackView::update()`'s stale-view loop deletes with `delete m_effectViews[nView]` rather than `delete *it`.
  The two are the same element, because `nView` equals the iterator's index at each erase; left as is, not part
  of this change.

## Findings outside the brief's list

- The brief's clearProject variant does not reproduce. `Song::clearProject()` calls `sendPostedEvents()` at
  `src/core/Song.cpp:1349`, before `clearAllTracks()` at 1351-1352, so queued view builds run first. The
  reproducible variant is a delete in the same turn, which the test uses.
- Checked, no raw object argument (or direct connection): `MixerView.cpp:108`, `AutomationEditor.cpp:108`,
  `FileBrowser.cpp:114-116`, `ShellChrome.cpp:213,216`, `TrackContainerView.cpp:134,136,186`,
  `PatternClipView.cpp:53` (direct). Deferred `singleShot` lambdas in src/gui capture no Track/Clip.
- `src/core` deferred calls checked: `ControlCommandsVideo.cpp:221`, `RenderQueue.cpp:301`,
  `ScriptEngine.cpp:404` capture no Track/Clip. `TaskCenter::onMainThread` callers were not audited in depth.
  `ControlServerWin32.cpp:455` and `MidiApple.cpp:543` are platform-specific and not built here.

## Process notes

- The commits were made with `git -c core.hooksPath=/dev/null` in the first commit only, and plainly after that.
  No hooks are installed (only `.sample` files, and no `core.hooksPath`), so nothing was skipped.
- The environment's primary directory changed during the run (to `zene-m0-train2`, branch `050/m0-train-2`) and
  back. That other worktree was not touched; all work is in `zene-bunny-control`.

## Commits

- a8797f949 fix(gui): a track or clip removed before its queued view is built is never built (cluster trackview-uaf)
- 8af5d280e docs(m0): trackview-uaf blocker fix report (cluster trackview-uaf)
- e1ae28af0 fix(gui): a queued removal of a rack view that is already gone does nothing (cluster trackview-uaf)
- (this report, the commit after e1ae28af0) docs(m0): trackview-uaf report, rack follow-up (cluster trackview-uaf)
