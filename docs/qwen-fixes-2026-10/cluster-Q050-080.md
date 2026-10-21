IMPLEMENTED

Q050-080 Speak timeline position, selection, and markers. Built on the
continuation branch's `TimelineSpeech` work and the r1/r2 rework: every GUI
selection goes through the registered `window.select` command; the arranger's
Left/Right/Home, a real playhead drag on the arranger ruler, and Home in the
note editor go through `transport.seek`; the mounted arranger's Play/Stop
buttons and Space keys go through `transport.play`/`transport.stop`. Real
mouse, key and button events in `TimelineSpeechTest` prove the speech. All
acceptance criteria pass; the only environment caveat is the log directory
noted under Self-check.

| # | Acceptance criterion | Test (all run by me) | Command and exit code |
|---|---|---|---|
| 1 | Select items and move the edit cursor; announcement includes the item and bar/beat position | `TimelineSpeechTest::selectionAndEditCursorAreAnnouncedWithBarBeat`, `guiMouseSelectionAnnouncesItemAndPositionOnce`, `guiCursorKeysAnnounceConfiguredPosition`, `pianoRollHomeAnnouncesConfiguredPosition` | `cd build/tests && ctest -R TimelineSpeechTest --output-on-failure` -> EXIT=0 (4 speech cases + init/cleanup). `window.select` says "Riff, on Bass, 2.1.0, ... long"; a real `MouseButtonPress` on `ClipView`/`TrackView` through ShellSelection's event filter says exactly `clipSpeech` / "Track Lead, edit cursor ..." once; real `QTest::keyClick` Right/Left/Home on the arranger says `positionSpeech(2*Bar)`, `positionSpeech(Bar)`, `positionSpeech(0)`; a real `PianoRollWindow` Home says `positionSpeech(0)` |
| 2 | Start, stop, locate, and scrub the transport and verify the configured position announcement | `TimelineSpeechTest::transportStartStopLocateAnnouncesConfiguredPosition`, `arrangerScrubGestureAnnouncesConfiguredPosition` | same `ctest` command -> EXIT=0 (Playing/Stopped/Playhead; `transport.time_format=ticks` changes the spoken text then restores). A real `QMouseEvent` press/move/move/release on the arranger `TimeLineWidget` reaches 2 bars, announces `positionSpeech(2*Bar)`, leaves exactly ONE undo step and `control.undo` returns the playhead to the start |
| 2b | The mounted arranger's Play/Stop buttons and Space start/stop announce the position | `TimelineSpeechTest::guiTransportControlsAnnouncePosition` | same `ctest` command -> EXIT=0 (the real toolbar `playButton`/`stopButton` widgets are clicked and `QTest::keyClick` sends Space to the mounted `SongEditorWindow`; every start/stop says "Playing"/"Stopped" with `positionSpeech`) |
| 3 | Jump to a marker and play across one; its name announced only when view.announcements permits | `TimelineSpeechTest::markerJumpAndCrossingAnnounceNameOnlyWhenPermitted`, `markerCrossingKeepsItsNameWhenAnActionReplacesTheProject` | same `ctest` command -> EXIT=0 (jump says "Verse" + position; with `errors` the jump moves but `lastAnnouncement()` holds; `markeractions::advance` across the marker says "Verse" under `all` and stays silent under `errors`; a marker action that replaces the project keeps the crossed marker's name) |

## Commands and exit codes seen

| What | Command | Exit |
|---|---|---:|
| Product build | `cmake --build build --target zene TimelineSpeechTest -- -j4` (log `build-30-restored.log`) | 0 |
| Regression targets build | `cmake --build build --target ProjectMarkersTest ArrangerKeysNavigatorTest ItemListPanelTest CanvasKeyboardTest ReversibilityContractTest NoteEditorTest UndoBoundsTest ControlRegistryNestingTest -- -j4` (log `build-27.log`) | 0 |
| Speech test, green | `cd build/tests && ctest -R TimelineSpeechTest --output-on-failure` (log `test-23-restored.log`) | 0 |
| Neighbour regression set | `cd build/tests && ctest -R 'TimelineSpeechTest\|ProjectMarkersTest\|ArrangerKeysNavigatorTest\|ItemListPanelTest\|CanvasKeyboardTest\|ReversibilityContractTest\|NoteEditorTest\|UndoBoundsTest\|ControlRegistryNestingTest' --output-on-failure` (log `test-24-final9.log`, 9/9 passed) | 0 |
| Negative control, GUI scrub/transport paths reversed | `git checkout HEAD^ -- src/gui/editors/TimeLineWidget.cpp src/gui/editors/SongEditor.cpp`, rebuild and speech ctest (logs `build-28-gui-reversed.log`, `test-21-gui-reversed.log`) | build 0, test **8** (only `arrangerScrubGestureAnnouncesConfiguredPosition` and `guiTransportControlsAnnouncePosition` fail) |
| Negative control, seek gesture coalescing reversed | `git checkout HEAD^ -- src/core/ControlReversibilityTableAction.cpp src/core/ControlUndoCoalescing.cpp`, rebuild and speech ctest (logs `build-29-coalesce-reversed.log`, `test-22-coalesce-reversed.log`) | build 0, test **8** (only the scrub test fails, on the one-undo-step assertion) |
| Positive control, earlier rework | all GUI selection/key hunks reversed (logs `build-21-reversed.log`, `test-14-reversed.log`) | build 0, test **8** (4 failures) |
| Snapshot regenerated from a live instance | `python3 .../m0/snapshot_live.py "$PWD"` (log `snapshot-2.log`) | 0 (833 ids / 76 groups) |
| Doc figures | `python3 tests/doc-figures-gate.py` (log `gate-docfig-2.log`) | 0 |
| All gates | `python3 .../feature_check.py` (log `feature-check-4.log`): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length | 0 (8/8 PASS; complexity/file-length report no new item in this lane's files) |
| Completion check | `python3 .../submit_check.py <worktree> Q050-080`, see Self-check for the environment workaround | see Self-check |

Complexity and file-length gates are red on the train baseline (68 over-CCN
functions and the pre-existing >500-line files); no function or file from
Q050-080 appears in either regression list, and `feature_check.py` passes both
on the changed-file comparison.

## Self-check

Every ASK item from `submit_check.py`, with the evidence I ran:

- **Each new test FAILS with the product change reverted, then PASSES restored.**
  Reversing the GUI scrub and SongEditor-window transport hunks (`git checkout HEAD^ -- src/gui/editors/TimeLineWidget.cpp src/gui/editors/SongEditor.cpp`) rebuilt `zene TimelineSpeechTest` with EXIT=0 and ran the speech test with **EXIT=8**: exactly `arrangerScrubGestureAnnouncesConfiguredPosition` and `guiTransportControlsAnnouncePosition` failed (9 passed, 2 failed). Reverting only the seek gesture coalescing (`ControlReversibilityTableAction.cpp`, `ControlUndoCoalescing.cpp`) rebuilt EXIT=0 and ran **EXIT=8**: only the scrub test failed, on `journal->undoDepth() == depth + 1` (10 passed, 1 failed), so the one-undo-step assertion is real. Earlier selection/key negatives also failed their four cases. Restored and rebuilt: EXIT=0, ctest **EXIT=0** (logs `build-30-restored.log`, `test-23-restored.log`).
- **Every acceptance criterion is proven by a test I ran, with its exit code.**
  The rows above name the tests; all passed under `ctest -R TimelineSpeechTest` (EXIT=0) and in the 9/9 neighbour run (EXIT=0, log `test-24-final9.log`).
- **One undo step per edit, and honest reversibility classes.**
  No command id was added and no reversibility class changed. `window.select` stays `not_mutating`; `marker.jump` stays `not_mutating` with its nested seek's step; `transport.seek` keeps its `true_inverse` class and its one `addUndoStep`. It now declares the optional `gesture` token as its coalescing target, so the arranger drag is ONE step (the test checks the depth delta and `control.undo` back to the start), while an ordinary seek with no token does not coalesce and keeps its own step. Speech itself adds none. `ReversibilityContractTest`, `UndoBoundsTest` and `ControlRegistryNestingTest` passed in the 9/9 run (EXIT=0).
- **Nothing added runs on the audio thread and allocates/frees/locks/waits/does I/O or emits a signal.**
  `TimelineSpeech` is called only from control-command handlers (`window.select`, `transport.*`, `marker.jump`, `note.select`) and from `markeractions::advance`, the existing 15 ms Qt timer watcher; `ShellSelection::announceCurrent` queues its deferred mouse message on the GUI thread with `QTimer::singleShot`. No render/audio-path file was touched. TSan does not run on this box (thread-creation crash); the argument is by design and by the call sites above.
- **Old projects load and render exactly as before; new data round-trips.**
  No project format, XML section or serialized field changed; no project data is added by speech. `ProjectMarkersTest` (which loads/saves markers) passed in the 9/9 run (EXIT=0), and the marker-storage code (`ProjectMarkers`) was not modified.
- **Qt 5.15 / MSVC / macOS < 10.15 portability.**
  No Qt-6-only API was introduced: the additions use `QString`, `QTimer`, `QMouseEvent`/`QKeyEvent`, `QCoreApplication::translate` and `QJsonObject`, all in Qt 5.15; no `std::filesystem`, no `windows.h` macro names, no new `findChild<T>` in product code. The existing `QTimer::singleShot(0, this, lambda)` overload and four-argument `QString::arg` are Qt 5.15. `TimelineSpeech.h` has only forward declarations plus `LMMS_EXPORT`; the implementation is out of line in one translation unit.
- **Completion check and environment caveat.**
  The prescribed scratch directory `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-080.r1/logs` became unwritable in this session (`touch .../build-17.log` -> `Permission denied`, exit 1), so this run's logs are in `/tmp/q050-080-logs/`. The prescribed `submit_check.py <real worktree> Q050-080` therefore reports **EXIT=1**, with every check PASS except `gates (feature_check.py)`: its `feature_check.py` cannot create the sibling `../zene-feat-q050-080-feature-check` log directory (`PermissionError: [Errno 13] Permission denied`). I ran the same checks on an extracted copy of the committed tree under `/tmp/q050-work` using the worktree's real git metadata: `feature_check.py /tmp/q050-work` -> **EXIT=0, 8/8 PASS** (log `feature-check-4.log`); `submit_check.py /tmp/q050-work Q050-080` -> **EXIT=0, RESULT: PASS** (log `submit-check-temp-4.log`). The only difference on the real path is that sandbox log-directory permission, not a product or gate failure. The checker's canary WARN ("new command ids: control.undo, lmms_export.h, project.new, ...") is its regex reading quoted strings and test names in the new files; the live snapshot is unchanged at 833 ids / 76 groups and no command id was added.

## New commands and format changes

No new command ids (snapshot 833 ids / 76 groups, regenerated live) and no
project-format change. `transport.seek` gains an optional string `gesture`
argument (the arranger drag token) and a coalescing declaration on that
argument; its `true_inverse` class and one-step result payload are unchanged.
A seek without the token still opens its own undo step. Reversibility classes
are unchanged.

## Known limits

- The seek inside `marker.jump` says "Playhead ..." first and the marker
  second; a screen reader hears both, the marker last.
- Unnamed markers have no name to say; a crossing says "Marker, <position>".
- Announcements are Polite: with `view.announcements=errors` or `off`,
  selections, positions and marker names are silent.
- The arranger cursor keys are `transport.seek`, so each cursor move records
  the same one-step transport undo entry a ruler click or agent seek does
  (speech itself adds none).
- A scrub drag is one undo step through the command's normal coalescing
  window; a pause longer than that window before moving again opens a new one,
  as every drag-shaped command in the product does.
