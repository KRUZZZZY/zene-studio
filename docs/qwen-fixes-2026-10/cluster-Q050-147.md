IMPLEMENTED
Q050-147: IMPLEMENTED
Q050-123: IMPLEMENTED
Q050-274: IMPLEMENTED

# Cluster Q050-147 / Q050-123 / Q050-274

Three L03 units landed on `feat/050-Q050-147-oc` from train 050/m0-train-8. The shared surface is
the Song Editor ruler and the take-lane/comp strip; the shared pieces are the Timeline's existing
punch range, the registered `transport.punch_*` and `comp.*` commands, and one waveform painter
used by both the arrangement rows and the comp strip.

## Q050-147 - punch range has its own ruler range

Feature doc: `docs/050-features/Q050-147.md`.

| # | Criterion | Test | Exit |
|---|---|---|---|
| 1 | Separate punch band, draggable handles, arm control | `ArrangerTakePunchTest::thePunchRangeHasItsOwnHandlesAndArmControl` (band/handles/arm; reverse proofs below) | 0 |
| 2 | Handles leave loop unchanged; recording only inside the range | Same GUI test (loop attributes before/after); `RecordingLatencyTest::anArmedPunchRegionCapturesTheInsideTransientAndOnlyIt`; `tests/control-punch-transcript.py` loop-attribute check | 0 / 0 / 0 |

Reverse proofs:
- arm lambda disabled: build exit 0, `ArrangerTakePunchTest::thePunchRangeHasItsOwnHandlesAndArmControl`
  exit 1 (`timeline.punchArmed()` false);
- `getPunchAction()` disabled: build exit 0, same test exit 1 (end handle drag expected 960, actual 768).
- The audio gate pre-dates this unit; its inside/outside proof is `RecordingLatencyTest` exit 0 and
  `PunchWindowTest` exit 0.

## Q050-123 - take lanes and waveforms in the timeline

Feature doc: `docs/050-features/Q050-123.md`.

| # | Criterion | Test | Exit |
|---|---|---|---|
| 1 | Lane clip geometry + aligned waveforms in the arrangement; per-take waveforms and active-segment highlight in the comp strip | `ArrangerTakePunchTest::takeLaneRowsAlignWaveformsWithTheRulerAndKeepTheCompPanel`; `TakeLanesPanelTest::theStripShowsTakeWaveformsAndTheCompHighlight` | 0 / 0 |
| 2 | Qt GUI test checks ruler alignment and keeps the existing comp panel | Same two tests (scroll + zoom ruler comparisons; panel controls and strip present) | 0 / 0 |

Reverse proofs:
- arrangement rows not created: build exit 0, `takeLaneRowsAlign...` exit 1 (`rows != nullptr`);
- `CompSwipeStrip::rebuildTakes()` emptied: build exit 0, `theStripShows...` exit 1
  (`takeBands().size()` 0 instead of 2).

## Q050-274 - touch swipes for comp selection

Feature doc: `docs/050-features/Q050-274.md`.

| # | Criterion | Test | Exit |
|---|---|---|---|
| 1 | Touch swipe selects the same region in `comp.get_state` | `TakeLanesPanelTest::aTouchSwipeCompsTheSameRangeAsAMouse` (touch, tap, reverse swipe) | 0 |
| 2 | Existing mouse and keyboard comp gestures still pass | Same test's mouse-parity half; `CanvasKeyboardTest` (ctest) | 0 / 0 |

Reverse proof: with touch events accepted but not handled, build exit 0 and
`aTouchSwipeCompsTheSameRangeAsAMouse` exit 1 (`segments.size()` 0 instead of 1).

## Commands, format and tests

- No new command. `transport.punch_set`'s description was corrected (gate wired, ruler/arm
  described); snapshot regenerated live: 833 ids / 76 groups, `tests/doc-figures-gate.py` PASS.
- Reversibility unchanged: punch writers `true_inverse`, `punch_get_state` `not_mutating`;
  `comp.select` `true_inverse`, `comp.get_state` `not_mutating`.
- No project-format change. Old projects load with empty/disarmed punch and unchanged rendering;
  new punch state uses the existing optional `<timeline>` attributes. Take-lane display state is
  derived, never saved.
- Tests run: `ctest -R '^(ArrangerTakePunchTest|ArrangerTimelineTest|TakeLanesPanelTest|CanvasKeyboardTest|PunchWindowTest|RecordingLatencyTest)$'`
  exit 0, 6/6 (test-final4 log); `python3 tests/control-punch-transcript.py build/zene` exit 0
  (punch-transcript-final2 log); `python3 tests/doc-figures-gate.py` exit 0.
- Gates: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
  no-tautology and doc-figures exit 0; complexity and file-length report no new item in this
  branch's files (the train's own pre-existing entries stay red as on the base).

## Self-check

Commands and exit codes observed (logs under `build/q050-logs/`):

- Build: `cmake --build build --target zene ArrangerTakePunchTest ArrangerTimelineTest TakeLanesPanelTest RecordingLatencyTest CanvasKeyboardTest PunchWindowTest -- -j4`
  exit 0 (build-final2 after the test move, and build-24 after the final touch fix; the earlier builds build-22/build-23 also exited 0).
- Tests: `ctest -R '^(ArrangerTakePunchTest|ArrangerTimelineTest|TakeLanesPanelTest|CanvasKeyboardTest|PunchWindowTest|RecordingLatencyTest)$'`
  exit 0, 6/6 (test-final4).
- Punch transcript: `python3 tests/control-punch-transcript.py build/zene` exit 0
  (punch-transcript-final2); its new check "the punch edit left every loop attribute unchanged" ok.
- Gates: six simple gates exit 0; complexity and file-length PASS in the patched
  `feature_check` run (`RESULT: PASS`, feature-check-run2). The stock script cannot create
  `<worktree>-feature-check` in this sandbox (the parent directory is write-protected), so the
  same script was run from a local copy with only the log directory redirected
  (`build/q050-logs/checker`); every gate command and comparison is unchanged.

**1. Each new test fails with the product change reverted; passes restored.**

| Reverse-proof | Build | Test | Exit |
|---|---|---|---|
| Punch handles disabled (`getPunchAction` returns `NoAction`) | 0 | `ArrangerTakePunchTest::thePunchRangeHasItsOwnHandlesAndArmControl` (punch end stayed 768, expected 960) | 1 |
| Punch arm lambda writes nothing | 0 | same test (`punchArmed()` false) | 1 |
| Take-lane rows not created in `TrackView` | 0 | `ArrangerTakePunchTest::takeLaneRowsAlignWaveformsWithTheRulerAndKeepTheCompPanel` (`rows == nullptr`) | 1 |
| `CompSwipeStrip::rebuildTakes()` returns without reading | 0 | `TakeLanesPanelTest::theStripShowsTakeWaveformsAndTheCompHighlight` (`takeBands()` 0, expected 2) | 1 |
| Touch events accepted but not handled | 0 | `TakeLanesPanelTest::aTouchSwipeCompsTheSameRangeAsAMouse` (`segments.size()` 0, expected 1) | 1 |

Restored, the same commands exit 0 (test-final4). The recording gate pre-dates this unit, so
`RecordingLatencyTest` stays green on a UI revert; it is cited for the inside/outside behaviour,
not as a red-on-revert proof.

**2. Every acceptance criterion is proven by a test run here.**

See the three feature docs: Q050-147 row 1/2 (`ArrangerTakePunchTest`, `RecordingLatencyTest`,
transcript, all exit 0), Q050-123 row 1/2 (`ArrangerTakePunchTest`, `TakeLanesPanelTest::theStrip...`,
exit 0), Q050-274 row 1/2 (`TakeLanesPanelTest::aTouch...`, `CanvasKeyboardTest`, exit 0).

**3. One undo step per edit; honest reversibility classes.**

The punch drag previews on the timeline and, on release, restores the pre-drag range before one
`transport.punch_set`; the test's single `control.undo` restores the pre-drag end (no second undo
needed). Touch and mouse commits call one `comp.select`, which is `true_inverse` with a Track
checkpoint. No command id, schema or class changed: `punch_set`/`punch_clear` and `comp.select`
remain `true_inverse`; `punch_get_state`/`comp.get_state` remain `not_mutating`.

**4. Realtime.**

No audio-thread path changed. The diff is GUI/ruler, GUI take rows, `TakeWaveform` (GUI-thread
thumbnail cache) and command descriptions; the capture path keeps reading the existing atomic
punch window (`AudioEngine::punchWindow`, `SampleRecordHandle::writePunched`). No allocation,
free, lock, wait, I/O or Qt signal was added on the render thread.

**5. Old projects and round-trip.**

No format change. `Timeline::loadSettings` still resets punch on absent attributes; the transcript
proves a project that never punched saves no punch attributes and that the region round-trips
through save/open. Take-lane rows and waveforms are derived from the existing track/clip model and
add no saved display state; the old comp panel still reads `comp.get_state`.

**6. Portability.**

Touch coordinate mapping is guarded at compile time for Qt 5.15 (`QTouchEvent::TouchPoint::pos()`
/`screenPos()`) and Qt 6.8 (`QEventPoint::position()`/`globalPosition()`); no Qt-6-only API is used
unguarded. No `small`/`near`/`far`/`interface`/`min`/`max` identifiers were added, no
`std::filesystem`, and no POSIX-only call. `std::clamp`/`std::find`/`QPointer` are available on all
targets.

**7. Commands metadata.**

Snapshot regenerated live: 833 ids / 76 groups; `doc-figures-gate.py` PASS; no new ids, so the A16
histogram is unchanged. Only `transport.punch_set`'s description changed.
