FIXED

Cluster 3192: "MPE expression updates race with note rendering" (`src/tracks/InstrumentTrack.cpp`, historical `:370`).

## Verdict: REAL

The MIDI input path stores MPE expression, the frequency-update mark and the note-off's release
velocity onto a live `NotePlayHandle` that the render period reads, and it did so without the
model-change guard the render period holds for the whole period. That is an unsynchronized
read/write pair on plain `int`/`bool` members: undefined behaviour, and a torn or stale expression
on the note.

Evidence, read in this session:

- **The writer is the MIDI input thread.**
  `InstrumentTrack::processInEvent()` reaches the MPE capture at
  [src/tracks/InstrumentTrack.cpp:533](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/tracks/InstrumentTrack.cpp:533)
  (`trackMpeInputEvent`, defined at `:364`), which calls
  `captureMpeReleaseVelocity()` at `:391` and `loadMpeExpressionOntoChannelNotes()` at `:404`.
  That path is fed by the MIDI client, on the client's own thread, never by the render thread:
  [src/core/midi/MidiClient.cpp:327](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/core/midi/MidiClient.cpp:327)
  (`midiPort->processInEvent(m_midiParseData.m_midiEvent)`), plus the backend threads
  (`src/core/midi/MidiAlsaSeq.cpp:1015-1078`, `src/core/midi/MidiWinMM.cpp:219-223`,
  `src/core/midi/MidiApple.cpp:555`).
- **What it wrote.** `note->setMpeExpression( expression )` and `note->setFrequencyUpdate()`
  ([src/tracks/InstrumentTrack.cpp:491-492](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/tracks/InstrumentTrack.cpp:491)),
  then `note->sendMpeExpressionMidi()`, and `note->setReleaseVelocity(...)` at
  [src/tracks/InstrumentTrack.cpp:449-450](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/tracks/InstrumentTrack.cpp:449).
  The stored members are plain values: `int m_mpePitchCents/m_mpePressure/m_mpeTimbre`
  ([include/Note.h:453-455](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/include/Note.h:453))
  and `bool m_frequencyNeedsUpdate`
  ([include/NotePlayHandle.h:342-345](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/include/NotePlayHandle.h:342),
  member at `:506`).
- **The readers are the render period.** `NotePlayHandle::play()` tests the mark and calls
  `updateFrequency()`
  ([src/core/NotePlayHandle.cpp:337-340](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/core/NotePlayHandle.cpp:337)),
  which spends the mark at `:655-659` and reads the expression at `:728`;
  `InstrumentTrack::processAudioBuffer()` calls `MpeSynthExpression::applyToNote( *n, ... )` for the
  built-in synthesisers
  ([src/tracks/InstrumentTrack.cpp:309](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/tracks/InstrumentTrack.cpp:309),
  `src/core/MpeSynthExpression.cpp:142-150`), and `NotePlayHandle::noteOff()` emits the release
  velocity.
- **The period owns the change lock while it reads.**
  `AudioEngine::renderNextPeriod()` takes `std::lock_guard{m_changeMutex}` before the render stages
  ([src/core/AudioEngine.cpp:1221-1223](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/core/AudioEngine.cpp:1221)),
  and `AudioEngine::requestChangeInModel()` is how every control thread waits for a period boundary
  ([src/core/AudioEngine.cpp:1983-1993](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/core/AudioEngine.cpp:1983)).
  The MIDI input path already used that convention for its note-off (`requestChangeInModel()` /
  `doneChangeInModel()` around `m_notes[key]->noteOff( offset )`); the MPE writes added later were
  not bracketed, and neither was the release-velocity store, which runs *before* that bracket.

So the finding is real, at the cause the finding names, and the intent that shows it is the engine's
own rule: a control thread that edits state the render period reads takes the change guard.

## The fix (product code)

Two stores, both in `src/tracks/InstrumentTrack.cpp`, now take
`Engine::audioEngine()->requestChangesGuard()` - the RAII form of the same request/done pair, the
same idiom `RackZones::edit()` uses (`src/core/RackZones.cpp:97-98`):

- `loadMpeExpressionOntoChannelNotes()` -
  [src/tracks/InstrumentTrack.cpp:456-469](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/tracks/InstrumentTrack.cpp:456):
  the guard covers the whole walk (the expression stores, the frequency mark, and the
  `sendMpeExpressionMidi()` call that forwards pressure/timbre to the instrument).
- `captureMpeReleaseVelocity()` -
  [src/tracks/InstrumentTrack.cpp:442](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/tracks/InstrumentTrack.cpp:442):
  the guard covers `note->setReleaseVelocity(...)`.

Why this and not a lock-free queue: the writes are not values the audio thread only samples - the
pressure/timbre store is immediately followed by an instrument call (`processOutEvent` →
`Instrument::handleMidiEvent`) that must not interleave with the period either. The guard excludes
both in one step, costs the audio thread nothing (it already holds the lock; `requestChangeInModel()`
returns at once on the render thread, `AudioEngine.cpp:1985`), and reuses the tree's existing
control-thread contract instead of adding a second one. `MpeExpression`'s own capture state stays
lock-free (`noteOn/noteOff/handleExpressionEvent` allocate nothing and take no lock,
`include/MpeExpression.h:192-193`); the guard is taken after `handleExpressionEvent()` has returned.

## Callers and siblings checked

- Each of the two changed functions has exactly one caller, both in `trackMpeInputEvent()`
  (`:391`, `:404`); nothing else in `src/`, `include/` or `tests/` calls them (fff `multi_grep`).
- The note-on branch of the same input path stamps expression on the newborn handle
  (`:566-574`) **before** `Engine::audioEngine()->addPlayHandle( nph )`: `addPlayHandle()` stages the
  handle in the engine's lock-free new-handle queue
  ([src/core/AudioEngine.cpp:1762-1789](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3192/src/core/AudioEngine.cpp:1762)),
  which the render thread adopts later under the change lock, so the render thread cannot see that
  handle yet and the stamp needs no guard. Recorded in the new comment.
- The note-off branch of `processInEvent()` keeps its own `requestChangeInModel()`/`doneChangeInModel()`
  pair. The guard is a recursive mutex with a per-thread depth counter, and the two are sequential
  here (capture first, note-off after), so nesting and ordering are unchanged.
- No other writer of the note expression was found on a non-render thread:
  `note.expression_set` (`src/core/ControlCommandsNoteExpression.cpp:397`) edits a **clip** note
  under a journal checkpoint, not a live play handle.
- Untouched by this fix, and reported under Other findings: the channel-wide `MidiKeyPressure` input
  branch (`src/tracks/InstrumentTrack.cpp:604`) calls `m_notes[key]->setVolume(...)` on the same
  thread without the guard, and `InstrumentTrack::retunePlayingNotes()`
  (`include/InstrumentTrack.h:139-149`) stores the same frequency mark from the control thread. They
  are the same class of race; they are not this finding's behaviour, so changing them here would
  widen the blast radius of a bug fix.

## Regression test

`tests/src/core/MpeInputPathTest.cpp` (the existing file for the real MPE input path):
`expressionUpdatesWaitForTheEngineChangeLock()` with the helper
`requireTheWriteWaitsForTheRenderPeriod()`.

The helper holds the model the way `renderNextPeriod()` does
(`engine->requestChangesGuard()` on the test thread), starts the write on a second thread, and
decides on `AudioEngine::modelChangeWaiters()` - the seam that reports a control thread parked in
`requestChangeInModel()` - so no sleep and no clock decides the outcome. It requires, for each
drive: the writer reached the guard, did not finish while the period owned the model, and did not
change the note. Then it releases the model, joins, and requires the value to have landed.

Test matrix:

| Path | Covered by |
| --- | --- |
| Normal path: bend, pressure, note-off release velocity through the real `processInEvent` | `expressionUpdatesWaitForTheEngineChangeLock` (post-release assertions) and the pre-existing `inputPathStampsTheRightNotes`, `noteOffVelocityIsCapturedOnTheReleasedNote` |
| Failure: the pitch-axis write (no instrument event) races the period | the test's first drive; RED run below fails here |
| Failure: the pressure/timbre write, including the instrument forwarding | the test's second drive; RED run below fails here |
| Failure: the release-velocity store races the period | the test's third drive; RED run below fails here (it is written *before* the pre-existing note-off bracket) |
| Exhaustion / overlap | not applicable: no queue, no capacity, no limit is added. The guard is the recursive engine lock |
| Reconfiguration | none added; the guard is per call and holds nothing across calls. `MpeExpression::setEnabled(false)` skips the whole capture, as before |
| Audio-thread constraints | nothing new on the audio thread: the guard runs on the MIDI input thread, which is a control thread; the render thread's path is unchanged and it already owns the lock |
| Supported platforms | Linux x86_64, Qt 6.12, GCC 16 built and ran here. `AudioEngine::RequestChangesGuard`, `modelChangeWaiters()` and `Engine::audioEngine()` are portable; the test uses `<thread>`, `<atomic>`, `<chrono>` only (already used by `TakeLaneCompPlaybackTest`, which builds on the Windows CI lanes) |

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/3192.r1/logs/`.

| # | Command | Log | Exit |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target MpeInputPathTest -j4` (test added, no fix) | `build-1.log` | 0 |
| 2 | `ctest -R '^MpeInputPathTest$' --output-on-failure` (from `build/tests`) - **RED** | `test-red-1.log` | **8** |
| 3 | `cmake --build build --target MpeInputPathTest -j4` (fix in) | `build-2.log` | 0 |
| 4 | `ctest -R '^MpeInputPathTest$' --output-on-failure` - **GREEN** | `test-green-1.log` | **0** |
| 5 | `cmake --build build --target zene MpeExpressionTest MpePlaybackTest MpeNoteStorageTest InstrumentTrackRenderPathTest NotePlaybackTest TakeLaneCompPlaybackTest SessionTuningTest -j4` | `build-3-family.log` | 0 |
| 6 | `ctest -R '^(MpeInputPathTest\|MpeExpressionTest\|MpePlaybackTest\|MpeNoteStorageTest\|InstrumentTrackRenderPathTest\|NotePlaybackTest\|TakeLaneCompPlaybackTest\|SessionTuningTest)$' --output-on-failure` - 8/8 passed | `test-green-2-family.log` | 0 |
| 7 | `cmake --build build --target MidiInputTransformTest MidiInputGateTest MpeSynthExpressionTest MpeStreamSnapshotTest MidiTakeRecordTest MpeInputActionTest ExternalMidiDeviceTest -j4` | `build-4-midi.log` | 0 |
| 8 | `ctest -R '^(MidiInputTransformTest\|MidiInputGateTest\|MpeSynthExpressionTest\|MpeStreamSnapshotTest\|MidiTakeRecordTest\|MpeInputActionTest\|ExternalMidiDeviceTest)$' --output-on-failure` - 7/7 passed | `test-green-4-midi-family.log` | 0 |
| 9 | `git diff -- src/tracks/InstrumentTrack.cpp > .../logs/fix.patch` | `fix.patch` | 0 |
| 10 | `git apply -R .../logs/fix.patch` (product change reversed) | - | 0 |
| 11 | `cmake --build build --target MpeInputPathTest -j4` | `build-5-red-proof.log` | 0 |
| 12 | `ctest -R '^MpeInputPathTest$' --output-on-failure` - **RED again** | `test-red-2-proof.log` | **8** |
| 13 | `git apply .../logs/fix.patch` (product change restored) | - | 0 |
| 14 | `cmake --build build --target MpeInputPathTest -j4` | `build-6-green-proof.log` | 0 |
| 15 | `ctest -R '^MpeInputPathTest$' --output-on-failure` - **GREEN again** | `test-green-5-proof.log` | **0** |
| 16 | `cmake --build build --target zene -j4` | `build-7-zene.log` | 0 |
| 17 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gate-1.log` | 0 |

RED (step 12, `test-red-2-proof.log`) failed exactly on the three drives:

```
FAIL!  : MpeInputPathTest::expressionUpdatesWaitForTheEngineChangeLock() 'reachedLock' returned FALSE. (a per-note pitch bend never asked for the audio engine's change lock)
FAIL!  : MpeInputPathTest::expressionUpdatesWaitForTheEngineChangeLock() 'reachedLock' returned FALSE. (a per-note pressure never asked for the audio engine's change lock)
FAIL!  : MpeInputPathTest::expressionUpdatesWaitForTheEngineChangeLock() 'unchanged' returned FALSE. (an MPE note-off's release velocity wrote the note while a render period owned the model)
Totals: 7 passed, 1 failed, 0 skipped, 0 blacklisted, 488ms
```

GREEN (step 15, `test-green-5-proof.log`):

```
1/1 Test #179: MpeInputPathTest .................   Passed    0.53 sec
100% tests passed out of 1
```

Gates (step 17, `gate-1.log`), unpiped exit 0:

```
feature-check kads-3192 (base fa4a5f2e7, 654 changed files; logs in .../build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Files committed

- `src/tracks/InstrumentTrack.cpp` - the fix (registered in `tests/upstream-modifications.txt`).
- `tests/src/core/MpeInputPathTest.cpp` - the regression (registered in
  `tests/upstream-modifications.txt`).
- `tests/upstream-modifications.txt` - both entries, in the same commit.
- `docs/qwen-fixes-2026-10/cluster-3192.md` - this report.

No new product file, so `tests/fork-sources.txt` is unchanged. No header changed. No control
command added, no undo step touched, no saved-data format touched.

## Other findings

- `InstrumentTrack::processInEvent()`, channel-wide `MidiKeyPressure` branch
  (`src/tracks/InstrumentTrack.cpp:604-609`): `m_notes[event.key()]->setVolume( ... )` writes the note's
  volume, and through it `Note::setVolume` + an instrument event, on the MIDI input thread with no
  change guard. It is inherited LMMS behaviour and the same race class as cluster 3192, but it is
  not this finding, so it is left alone.
- `InstrumentTrack::retunePlayingNotes()` (`include/InstrumentTrack.h:139-149`, board card #712)
  stores `m_frequencyNeedsUpdate` from the control thread as a plain `bool`, and
  `NotePlayHandle::updateFrequency()` reads and spends it on the audio thread. The header documents
  that as intended ("a plain bool store on both sides"); under the C++ memory model it is still a
  data race, and `retunePlayingNotes()` has no guard. Not this finding; worth its own cluster.
- Local TSan does not run on this box, so the race claim here is a design argument read from the
  code (who writes, who reads, which lock the reader holds), not a sanitizer report.
