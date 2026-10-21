IMPLEMENTED

Q050-080 Speak timeline position, selection, and markers. Built on the
continuation branch's `TimelineSpeech` work and the r2.feature rework: every
GUI selection now goes through the registered `window.select` command, the
arranger's Left/Right/Home and the note editor's Home go through
`transport.seek`, and real mouse/key QtTest events prove the speech. All
acceptance criteria pass; the only environment caveat is the log directory
noted under Self-check.

| # | Acceptance criterion | Test (all run by me) | Command and exit code |
|---|---|---|---|
| 1 | Select items and move the edit cursor; announcement includes the item and bar/beat position | `TimelineSpeechTest::selectionAndEditCursorAreAnnouncedWithBarBeat`, `guiMouseSelectionAnnouncesItemAndPositionOnce`, `guiCursorKeysAnnounceConfiguredPosition`, `pianoRollHomeAnnouncesConfiguredPosition` | `cd build/tests && ctest -R TimelineSpeechTest --output-on-failure` -> EXIT=0 (4 speech cases + init/cleanup). `window.select` says "Riff, on Bass, 2.1.0, ... long"; a real `MouseButtonPress` on `ClipView`/`TrackView` through ShellSelection's event filter says exactly `clipSpeech` / "Track Lead, edit cursor ..." once; real `QTest::keyClick` Right/Left/Home on the arranger says `positionSpeech(2*Bar)`, `positionSpeech(Bar)`, `positionSpeech(0)`; a real `PianoRollWindow` Home says `positionSpeech(0)` |
| 2 | Start, stop, locate, and scrub the transport and verify the configured position announcement | `TimelineSpeechTest::transportStartStopLocateScrubAnnouncesConfiguredPosition` | same `ctest` command -> EXIT=0 (Playing/Stopped/Playhead; three scrub seeks; `transport.time_format=ticks` changes the spoken text, then restores `bars_beats`) |
| 3 | Jump to a marker and play across one; its name announced only when view.announcements permits | `TimelineSpeechTest::markerJumpAndCrossingAnnounceNameOnlyWhenPermitted` | same `ctest` command -> EXIT=0 (jump says "Verse" + position; with `errors` the jump moves but `lastAnnouncement()` holds; `markeractions::advance` across the marker says "Verse" under `all` and stays silent under `errors`) |

## Commands and exit codes seen

| What | Command | Exit |
|---|---|---:|
| Product build | `cmake --build build --target zene TimelineSpeechTest -- -j4` (log `build-24-restored.log`) | 0 |
| Regression targets build | `cmake --build build --target ProjectMarkersTest ArrangerKeysNavigatorTest ItemListPanelTest CanvasKeyboardTest ReversibilityContractTest NoteEditorTest -- -j4` (log `build-19.log`) | 0 |
| Speech test, green | `cd build/tests && ctest -R TimelineSpeechTest --output-on-failure` (log `test-17-restored.log`) | 0 |
| Neighbour regression set | `cd build/tests && ctest -R 'TimelineSpeechTest\|ProjectMarkersTest\|ArrangerKeysNavigatorTest\|ItemListPanelTest\|CanvasKeyboardTest\|ReversibilityContractTest\|NoteEditorTest' --output-on-failure` (log `test-18-final7.log`, 7/7 passed) | 0 |
| Negative control, all GUI product hunks reversed | `cmake --build build --target zene TimelineSpeechTest -- -j4` then the speech ctest (logs `build-21-reversed.log`, `test-14-reversed.log`) | build 0, test **8** (4 failures: selection command, mouse, arranger keys, note-editor Home) |
| Targeted negative control: event filter sets selection directly instead of invoking `window.select` | same build/test (logs `build-23-mouse-reversed.log`, `test-16-mouse-reversed.log`) | build 0, test **8** (only `guiMouseSelectionAnnouncesItemAndPositionOnce` fails; the other 7 pass) |
| Snapshot regenerated from a live instance | `python3 .../m0/snapshot_live.py "$PWD"` (log `snapshot-1.log`) | 0 (833 ids / 76 groups) |
| Doc figures | `python3 tests/doc-figures-gate.py` (log `gate-docfig-2.log`) | 0 |
| All gates | `python3 .../feature_check.py` (log `feature-check-2.log`): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length | 0 (8/8 PASS; complexity/file-length report no new item in this lane's files) |
| Completion check | `python3 .../submit_check.py <worktree> Q050-080`, see Self-check for the environment workaround | see Self-check |

Complexity and file-length gates are red on the train baseline (68 over-CCN
functions and the pre-existing >500-line files); no function or file from
Q050-080 appears in either regression list, and `feature_check.py` passes both
on the changed-file comparison.

## Self-check

Every ASK item from `submit_check.py`, with the evidence I ran:

- **Each new test FAILS with the product change reverted, then PASSES restored.**
  Reversing the five GUI product files to the base (`git checkout HEAD^ -- src/gui/shell/ShellSelection.cpp src/gui/ControlCommandsSelection.cpp src/gui/noteeditor/NoteEditor.cpp src/gui/editors/SongEditor.cpp src/gui/editors/PianoRoll.cpp`) rebuilt `zene TimelineSpeechTest` with EXIT=0 and ran the speech test with **EXIT=8**; failures were `selectionAndEditCursorAreAnnouncedWithBarBeat`, `guiMouseSelectionAnnouncesItemAndPositionOnce`, `guiCursorKeysAnnounceConfiguredPosition`, `pianoRollHomeAnnouncesConfiguredPosition` (4 passed, 4 failed). A second, targeted negative kept the command announcer but made only the event filter call the setters directly: `zene TimelineSpeechTest` EXIT=0, `ctest -R TimelineSpeechTest` **EXIT=8**, exactly `guiMouseSelectionAnnouncesItemAndPositionOnce` failing (7 passed, 1 failed), which proves the mouse test exercises the registered-command path. Restored and rebuilt: EXIT=0, ctest **EXIT=0** (logs `build-24-restored.log`, `test-17-restored.log`).
- **Every acceptance criterion is proven by a test I ran, with its exit code.**
  The three rows above name the tests; all passed under `ctest -R TimelineSpeechTest` (EXIT=0) and in the 7/7 neighbour run (EXIT=0, log `test-18-final7.log`).
- **One undo step per edit, and honest reversibility classes.**
  No command was added and no reversibility row changed. `window.select` stays `not_mutating` (interface state, no journal); `marker.jump` stays `not_mutating` with its nested seek's step; `transport.seek` keeps its `true_inverse` class and its single `addUndoStep`, and the new arranger/note-editor key paths invoke that same command, so each cursor move is the same one transport step a ruler click or agent seek records. Speech adds no undo step. `ReversibilityContractTest` passed in the 7/7 run (EXIT=0).
- **Nothing added runs on the audio thread and allocates/frees/locks/waits/does I/O or emits a signal.**
  `TimelineSpeech` is called only from control-command handlers (`window.select`, `transport.*`, `marker.jump`, `note.select`) and from `markeractions::advance`, the existing 15 ms Qt timer watcher; `ShellSelection::announceCurrent` queues its deferred mouse message on the GUI thread with `QTimer::singleShot`. No render/audio-path file was touched. TSan does not run on this box (thread-creation crash); the argument is by design and by the call sites above.
- **Old projects load and render exactly as before; new data round-trips.**
  No project format, XML section or serialized field changed; no project data is added by speech. `ProjectMarkersTest` (which loads/saves markers) passed in the 7/7 run (EXIT=0), and the marker-storage code (`ProjectMarkers`) was not modified.
- **Qt 5.15 / MSVC / macOS < 10.15 portability.**
  No Qt-6-only API was introduced: the additions use `QString`, `QTimer`, `QMouseEvent`/`QKeyEvent`, `QCoreApplication::translate` and `QJsonObject`, all in Qt 5.15; no `std::filesystem`, no `windows.h` macro names, no new `findChild<T>` in product code. The existing `QTimer::singleShot(0, this, lambda)` overload and four-argument `QString::arg` are Qt 5.15. `TimelineSpeech.h` has only forward declarations plus `LMMS_EXPORT`; the implementation is out of line in one translation unit.
- **Completion check and environment caveat.**
  The prescribed scratch directory `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-080.r1/logs` became unwritable in this session (`touch .../build-17.log` -> `Permission denied`, exit 1), so this run's logs are in `/tmp/q050-080-logs/` and the required `submit_check.py <real worktree> Q050-080` cannot create `../zene-feat-q050-080-feature-check` inside `feature_check.py` (`PermissionError: [Errno 13] Permission denied`, exit 1). I ran the same checks on an extracted copy of the committed tree under `/tmp/q050-work` using the worktree's real git metadata: `feature_check.py /tmp/q050-work` -> **EXIT=0, 8/8 PASS**. `submit_check.py /tmp/q050-work Q050-080` -> **EXIT=0, RESULT: PASS**; the only difference on the real path is the sandbox's inability to create that sibling log directory, not a product or gate failure.

## New commands and format changes

None. No new command ids (snapshot 833 ids / 76 groups, regenerated live),
no schema change, no project-format change, no reversibility-class change.

## Known limits

- The seek inside `marker.jump` says "Playhead ..." first and the marker
  second; a screen reader hears both, the marker last.
- Unnamed markers have no name to say; a crossing says "Marker, <position>".
- Announcements are Polite: with `view.announcements=errors` or `off`,
  selections, positions and marker names are silent.
- The arranger cursor keys are `transport.seek`, so each cursor move records
  the same one-step transport undo entry a ruler click or agent seek does
  (speech itself adds none).
