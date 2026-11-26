ALREADY-FIXED

# Cluster 3982: "Arrangement Record creates silent clips"

The land pass copies the launched content into the arrangement clip. The finding points at
`src/core/ControlCommandsSessionRecordInternal.h:185`; that header now holds only the shared
declarations - the writing pass is `src/core/ControlCommandsSessionRecordLand.cpp`, and at
`:377` it copies the recorded cell's content between `createClip` (`:375`) and
`changeLength` (`:378`). Nothing in this job changes product code; the product change that
makes this finding false is already on the branch.

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3982`
(branch `bugs/qwen-3982`). Logs:
`/home/kruzzzzy/.config/kads/zene/state/runs/3982.r1/logs/`.

## 1. Decision and evidence

REAL at the time it was reported, ALREADY-FIXED on this branch. The copy the finding asks for
exists, and two commits on this branch put it there:

| where | what shows it |
| --- | --- |
| `src/core/ControlCommandsSessionRecordLand.cpp:377` | `const QString content = copySlotContent(clip, slot, audioSample, pair);` - the pass copies into the clip it just created (`:375`) before it sets the length (`:378`) |
| `src/core/ControlCommandsSessionRecordLand.cpp:283-310` | `copySlotContent`: MIDI -> `copyPatternNotes(*destination, captured ? pair.patternId : slot.patternId())` (`:293`); audio -> `destination->setSampleBuffer(audioSample)` (`:305`) |
| `src/core/ControlCommandsSessionRecordLand.cpp:265-281` | `copyPatternNotes` walks `Engine::patternStore()->tracks()` and adds every note of the landed pattern column, so the landed MIDI clip carries the notes |
| `src/core/ControlCommandsSessionRecordLand.cpp:155-184`, `:186-213` | preflight loads the audio BEFORE the ring is consumed: a cell's source is loaded with `SampleBuffer::fromFile` (`:174`), a recorded take is copied into a `SampleBuffer` (`:204-210`); an unreadable or mismatched cell is refused with `nothing was consumed` (`:176-182`, `:198-203`) instead of landing a silent clip |
| `src/core/SessionFollow.cpp:510-519` | the capture names what was launched: `recordStart(track, scene, tick, content->patternId, content->clip)` |
| `include/SessionArrangementRecorder.h:152-159` | that overload stores `ContentKind::Midi / Audio / Empty` with the event, so the land pass knows which content to copy even when the model slot changed afterwards |
| `src/core/SampleClip.cpp:446-461` | `setSampleBuffer` installs the buffer AND `resetWindowToFullBuffer()` + `updateLength()`, so the landed sample clip plays the whole buffer rather than a stale empty window |

Commits (both ancestors of this branch head - `git merge-base --is-ancestor <sha> HEAD` exits 0
for each, and FIXED `0` / `0` was printed by that command):

- `cfa3dccab2` "fix(session): land MIDI notes and audio samples (cluster 3982)" - added
  `copySlotContent`, `copyPatternNotes` and the audio preflight.
- `adbb591eea` "fix: capture launched arrangement content (cluster new-p12-41)" - made the
  recorder tape the launched cell's pattern id and buffer, so the copy is the content that
  PLAYED, not whatever the model holds at land time.

Existing tests already pin it (`tests/src/core/ControlSessionRecordLandTest.cpp`, all green in
`test-5-family.log`): `aLandedMidiClipCarriesTheSlotPatternsNotes` (`:189`),
`aLandedMidiClipUsesThePatternThatWasLaunched` (`:212`), `aLandedAudioCellCarriesItsSample`
(`:252`), `redoRestoresALandedMidiClipExactly` (`:330`),
`redoRestoresALandedAudioClipAndItsRender` (`:374`).

## 2. The regression test added here

`tests/src/core/ControlSessionRecordLandTest.cpp:282`
`anAudioCellPlayedFromTheSessionLandsWithItsAudio` - the finding's own "when", end to end: an
audio cell is published, LAUNCHED THROUGH THE SCHEDULER, stopped, and landed. It asserts the
reported `content` is `audio_sample_copied` (the launched buffer, not the model fallback), that
the clip has a positive length, and that its buffer holds at least one non-zero frame - "the
landed clip carries the recorded span but no audio" is the failure message. Existing coverage
drove the ring directly for the audio case; this one goes through the scheduler, which is the
path the finding describes.

## 3. Commands and EXIT codes

All from this session, logs under the run's `logs/` folder.

| command | log | EXIT |
| --- | --- | --- |
| `cmake --build build --target ControlSessionRecordLandTest -j4` (before the test existed) | `build-1.log` | 0 |
| `ctest -R 'ControlSessionRecordLand\|ControlSessionRecordLandUndo'` - the Undo binary was not built yet, so ctest reported it `Not Run` | `test-1.log` | 8 |
| `cmake --build build --target ControlSessionRecordLandTest -j4` (with the new test) | `build-2.log` | 0 |
| `ctest -R '^ControlSessionRecordLandTest$'` - GREEN on the branch's product code | `test-2.log` | 0 |
| `git diff -- src/core/ControlCommandsSessionRecordLand.cpp > mutation.patch` | `mutation.patch` | 0 |
| `cmake --build build --target ControlSessionRecordLandTest -j4` (mutation applied) | `build-3-mutation.log` | 0 |
| `ctest -R '^ControlSessionRecordLandTest$'` - RED: 7 cases `FAIL!` | `test-3-mutation.log` | 8 |
| `git apply -R mutation.patch` (product code back to the branch's) | - | 0 |
| `cmake --build build --target zene ControlSessionRecordLandTest -j4` | `build-4.log` | 0 |
| `ctest -R '^ControlSessionRecordLandTest$'` - GREEN again | `test-4.log` | 0 |
| `cmake --build build -j4 --target ControlSessionRecordLandUndoTest SessionArrangementRecordTest SessionAudioCellPlaybackTest SessionCellPlaybackMenuTest` | `build-5-family.log` | 0 |
| `ctest -R 'SessionRecordLand\|SessionArrangementRecord\|SessionAudioCellPlayback\|SessionCellPlaybackMenu'` - `100% tests passed out of 5` | `test-5-family.log` | 0 |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` - `RESULT: PASS`, 8/8 PASS | `feature_check-1.log` | 0 |

`mutation.patch` is the one-line RED proof, kept in the log folder: it makes `copySlotContent`
return `"empty"` before copying anything - exactly the defect the finding describes ("creates
an arrangement clip and sets its length, but never copies or renders the session slot's
content into it"). With it applied, `test-3-mutation.log` shows:

```
 9: FAIL!  : ...::everyPairBecomesOneClipAtItsRecordedTicks() Compared values are not the same
13: FAIL!  : ...::aLandedMidiClipCarriesTheSlotPatternsNotes() Compared values are not the same
17: FAIL!  : ...::aLandedMidiClipUsesThePatternThatWasLaunched() Compared values are not the same
21: FAIL!  : ...::aLandedAudioCellCarriesItsSample() Compared values are not the same
25: FAIL!  : ...::anAudioCellPlayedFromTheSessionLandsWithItsAudio() Compared values are not the same
29: FAIL!  : ...::redoRestoresALandedMidiClipExactly() Compared values are not the same
33: FAIL!  : ...::redoRestoresALandedAudioClipAndItsRender() '!std::get<0>(landed).isEmpty()' returned FALSE.
```

## 4. Test matrix

| path | coverage |
| --- | --- |
| normal: MIDI cell launched and landed | `aLandedMidiClipCarriesTheSlotPatternsNotes`, `aLandedMidiClipUsesThePatternThatWasLaunched` |
| normal: audio cell launched from the scheduler and landed | `anAudioCellPlayedFromTheSessionLandsWithItsAudio` (new) |
| failure: destination cannot hold the content, source unreadable | `validateMidiDestination` / `validateAudioDestination` / `validateRecordedAudioDestination` refusals, exercised by the refusal cases in this file and `ControlSessionRecordLandUndoTest` |
| exhaustion: a full ring (`2 * 64` events) lands wholly | `aWorstCasePerformanceLandsWholly` |
| reconfiguration: grid cell / grid track removed before landing, column with no song track | `aColumnWithoutASongTrackAndAnEmptySpanAreReportedNotLanded`, `aRemovedSceneCellIsReportedNotLandedWithTheGridCellReason`, `aRemovedGridTrackCellIsReportedNotLandedWithTheGridCellReason` |
| undo | `everyPairBecomesOneClipAtItsRecordedTicks`, `ControlSessionRecordLandUndoTest` |
| audio thread | not touched: no product change in this job. The land pass runs on the model thread from the command handler; the recorder ring is the only audio-thread side and it is unchanged |
| platforms | portable C++20/Qt only: `std::size_t`, `SampleBuffer::data()/size()`, no Qt-6-only API, no new file |

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Red: `test-3-mutation.log`, `ctest -R '^ControlSessionRecordLandTest$'` -> EXIT 8, seven
  `FAIL!` lines (quoted above). Green: `test-4.log` -> EXIT 0, `100% tests passed out of 1`. The
  product code under test was rebuilt in each direction (`build-3-mutation.log`,
  `build-4.log`, both EXIT 0), and the mutation was reverted with `git apply -R` (EXIT 0), so
  the tree holds only the test change.
- **The test is deterministic.** No wall-clock, no sleep, no timer. `initTestCase` calls
  `Engine::audioEngine()->audioDev()->stopProcessing()` so the dummy render thread is not a
  second producer; the test then drives `SessionScheduler::processAudio(ctx, 16)` with an
  injected `SessionClockContext` (`ticksPerBar = 192`, `framesPerTick = 1.0f`,
  `positionTicks = 0` then `96`). The recorder ring is filled through the scheduler itself, not
  by a thread.
- **The fix is at the cause and complete; every caller and sibling checked.** `landPairState` has
  exactly one caller, the command handler's per-pair loop
  (`src/core/ControlCommandsSessionRecordLand.cpp:461`), and `copySlotContent` has exactly one
  caller, `landPairState` at `:377`; the header declares `landPairState`
  (`ControlCommandsSessionRecordInternal.h:208`) and nothing else calls it. Both content kinds go
  through the same copy (`:289-308`), and the capture side that feeds them
  (`SessionFollow.cpp:518`, `SessionArrangementRecorder.h:152-159`) passes the launched pattern
  id and buffer. This job changes no product file, so there is no sibling left on an old shape.
  The `git status` after the RED/GREEN round trip lists only the test file.
- **No collateral damage.** No product, schema, save-format or command-surface change: the diff
  is one test function. Old projects load as before because nothing serializes differently, and
  no new limit or check was added that another caller could trip over. The existing family (5
  tests) passes (`test-5-family.log`, EXIT 0) and the gates pass
  (`feature_check-1.log`, `RESULT: PASS`).
- **Verdict ALREADY-FIXED: the code that shows it** - `src/core/ControlCommandsSessionRecordLand.cpp:377`
  (the copy), `:283-310` (`copySlotContent`: `copyPatternNotes` at `:293`, `setSampleBuffer` at
  `:305`), `:186-211` (audio preflight), `src/core/SessionFollow.cpp:518` and
  `include/SessionArrangementRecorder.h:152-159` (the capture), brought in by `cfa3dccab2` and
  `adbb591eea`, both ancestors of this branch head (`git merge-base --is-ancestor` EXIT 0 for
  both).
- **Sibling commands in the command files I changed.** I changed no command file: the only
  edited file is `tests/src/core/ControlSessionRecordLandTest.cpp`. No id, group, argument
  schema, result schema or description moved, so `app.*`, `arrangement.*` and every other
  command in the files this lane touched keep the reply shape they had, and the
  `ControlCommandsSnapshot` canary is not moved by this job.

## Other findings

- `test-1.log` shows `ControlSessionRecordLandUndoTest` reported as `Not Run` because the test
  binary had not been built yet; a stale/absent binary reads as a test failure in a `ctest -R`
  family run. It was built and passes (`test-5-family.log`).
- The test logs carry `Fontconfig error: No writable cache directories` noise from the
  offscreen Qt platform; it is unrelated to this cluster and does not affect the exit codes.
