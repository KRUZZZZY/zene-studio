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
- arm lambda disabled: `ArrangerTakePunchTest::thePunchRangeHasItsOwnHandlesAndArmControl` exit 1
  (`punchArmed()` false);
- `getPunchAction()` disabled: same test exit 1 (end handle drag expected 960, actual 768).
- The audio gate pre-dates this unit; its inside/outside proof is `RecordingLatencyTest` exit 0 and
  `PunchWindowTest` exit 0.

## Q050-123 - take lanes and waveforms in the timeline

Feature doc: `docs/050-features/Q050-123.md`.

| # | Criterion | Test | Exit |
|---|---|---|---|
| 1 | Lane clip geometry + aligned waveforms in the arrangement; per-take waveforms and active-segment highlight in the comp strip | `ArrangerTakePunchTest::takeLaneRowsAlignWaveformsWithTheRulerAndKeepTheCompPanel`; `TakeLanesPanelTest::theStripShowsTakeWaveformsAndTheCompHighlight` | 0 / 0 |
| 2 | Qt GUI test checks ruler alignment and keeps the existing comp panel | Same two tests (scroll + zoom ruler comparisons; panel controls and strip present) | 0 / 0 |

Reverse proofs:
- arrangement rows not created: `takeLaneRowsAlign...` exit 1 (`rows != nullptr`);
- `CompSwipeStrip::rebuildTakes()` emptied: `theStripShows...` exit 1 (`takeBands().size() == 0`).

## Q050-274 - touch swipes for comp selection

Feature doc: `docs/050-features/Q050-274.md`.

| # | Criterion | Test | Exit |
|---|---|---|---|
| 1 | Touch swipe selects the same region in `comp.get_state` | `TakeLanesPanelTest::aTouchSwipeCompsTheSameRangeAsAMouse` (touch, tap, reverse swipe) | 0 |
| 2 | Existing mouse and keyboard comp gestures still pass | Same test's mouse-parity half; `CanvasKeyboardTest` (ctest) | 0 / 0 |

Reverse proof: with touch events consumed but not handled, `aTouchSwipeCompsTheSameRangeAsAMouse`
exit 1 (`segments.size()` 0 instead of 1).

## Commands, format and tests

- No new command. `transport.punch_set`'s description was corrected (gate wired, ruler/arm
  described); snapshot regenerated live: 833 ids / 76 groups, `tests/doc-figures-gate.py` PASS.
- Reversibility unchanged: punch writers `true_inverse`, `punch_get_state` `not_mutating`;
  `comp.select` `true_inverse`, `comp.get_state` `not_mutating`.
- No project-format change. Old projects load with empty/disarmed punch and unchanged rendering;
  new punch state uses the existing optional `<timeline>` attributes. Take-lane display state is
  derived, never saved.
- Tests run: `ctest -R '^(ArrangerTakePunchTest|ArrangerTimelineTest|TakeLanesPanelTest|CanvasKeyboardTest|PunchWindowTest|RecordingLatencyTest)$'`
  exit 0 (test-3 log); `python3 tests/control-punch-transcript.py build/zene` exit 0
  (punch-transcript-2 log).

## Self-check

See the per-unit feature docs and the final report for commands, exit codes, reversibility,
snapshot regeneration, portability guards and realtime reasoning.
