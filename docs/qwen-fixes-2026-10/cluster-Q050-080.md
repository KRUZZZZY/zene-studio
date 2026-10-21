IMPLEMENTED

Q050-080 Speak timeline position, selection, and markers (round 2: review
Q050-080.r1 rework - GUI mouse selection now routes through the shared
`speech::announceSelection` in `ShellSelection`, deferred past the press
handler's hint; `window.select` keeps no announcement of its own): every
acceptance criterion met and proven by `TimelineSpeechTest` (run, exit 0).

| # | Criterion | Test | Command + EXIT |
|---|-----------|------|----------------|
| 1 | Select items and move the edit cursor; announcement includes the item and bar/beat position | `TimelineSpeechTest::selectionAndEditCursorAreAnnouncedWithBarBeat` | `ctest -R TimelineSpeechTest --output-on-failure` from `build/tests`, EXIT=0 (says "Riff, on Bass" + bar/beat; seek + note.select positions) |
| 1b | GUI mouse selection announces the item and bar/beat position once (new rework regression) | `TimelineSpeechTest::guiMouseSelectionAnnouncesItemAndPositionOnce` | same ctest, EXIT=0 (MouseButtonPress through the real event filter on ClipView/TrackView; exact-match single message; window.select re-select leaves it unchanged) |
| 2 | Start, stop, locate, and scrub the transport and verify the configured position announcement | `TimelineSpeechTest::transportStartStopLocateScrubAnnouncesConfiguredPosition` | same ctest, EXIT=0 (Playing/Stopped/Playhead; ticks format verified, then restored) |
| 3 | Jump to a marker and play across one; name announced only when view.announcements permits | `TimelineSpeechTest::markerJumpAndCrossingAnnounceNameOnlyWhenPermitted` | same ctest, EXIT=0 (jump + `markeractions::advance` crossing say "Verse" under All, silent under errors) |

Builds: `cmake --build build --target zene TimelineSpeechTest -- -j4` EXIT=0
(logs/build-9.log); neighbours built same way EXIT=0 (logs/build-5.log).
Tests: `ctest -R
'TimelineSpeechTest|ProjectMarkersTest|ArrangerKeysNavigatorTest|ItemListPanelTest|CanvasKeyboardTest|ReversibilityContractTest'
--output-on-failure` from `build/tests`, 6/6 passed EXIT=0 (logs/test-10.log).
Negative control: mouse path forced back to speaking at once
(`announceChange` early synchronous say) rebuilds EXIT=0 and only the new GUI
test fails (5 passed, 1 failed, EXIT=8, logs/test-8-neg.log) while the three
command-path cases still pass - the GUI test exercises the real event-filter
path, not `window.select`; fix restored and 6/6 green again.
Regressions: `python3 tests/doc-figures-gate.py` PASS (828 ids / 76 groups) EXIT=0.
Gates: fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0,
check-namespace 0, no-tautology 0; complexity 1 and file-length 1 are red on
train 5 with none of this lane's functions/files listed (68 over-CCN
functions, no announceSelection/announceChange/guiMouseSelection match).
No new commands (snapshot unchanged 828/76); no format change.
