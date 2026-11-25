FIXED

Cluster 3192: "MPE expression updates race with note rendering" (`src/tracks/InstrumentTrack.cpp`, historical `:370`).

**Round 2. The round-1 fix was sent back and is withdrawn.** Its two `requestChangesGuard()` calls are gone. The
review's three points, and what this round does instead:

1. **[realtime] the guard could block the JACK audio callback.** `AudioJack::processCallback()` dispatches MIDI
   before it renders (`src/core/audio/AudioJack.cpp:442` vs `:476`), so `processInEvent()` runs on the audio
   callback thread *outside* the render-owner scope, and a guard there would take `m_changeMutex`. The input path
   now takes **no lock at all**: it publishes the capture into this track's atomics and the render thread applies
   it.
2. **[not-fixed] the release-velocity write went through a pointer read before the wait.** The capture no longer
   touches a note handle. The velocity is published per key, and the note that is ending takes it in its own
   `noteOff()`, under its own lock - the note resolves and validates itself, so a retired handle cannot be written
   through.
3. **[test-proves-nothing] the release-velocity assertion raced the writer.** The rewrite drives every axis through
   the published handoff: the delivery completes before the value is observed, the value is read only after the
   render period that applies it, and the callback case is decided by `modelChangeWaiters()` (parked vs finished),
   never by a clock.

## Verdict: REAL (unchanged), and the cause is the one the review names

- The MIDI input thread is not the render thread: `src/core/midi/MidiClient.cpp:327` and the backend input threads
  (`src/core/midi/MidiJack.cpp:183-212` - called from `AudioJack::processCallback`'s dispatch phase,
  `src/core/midi/MidiAlsaSeq.cpp:1015-1078`, `src/core/midi/MidiWinMM.cpp:219-223`).
- The values the period reads were plain members written from that thread:
  `include/Note.h:453-455` (`m_mpePitchCents/m_mpePressure/m_mpeTimbre`) and `include/NotePlayHandle.h:342-345`
  with `:506` (`m_frequencyNeedsUpdate`), read on the audio thread by `NotePlayHandle::updateFrequency()`
  (`src/core/NotePlayHandle.cpp:337-340`, `:655-659`, `:728`) and `MpeSynthExpression::applyToNote()`
  (`src/tracks/InstrumentTrack.cpp:309`, `src/core/MpeSynthExpression.cpp:142-150`), all inside
  `renderNextPeriod()`'s `std::lock_guard{m_changeMutex}` (`src/core/AudioEngine.cpp:1221-1223`).
- The longer-lived form of the same bug: a write through `m_notes[key]` can outlive the handle, because the render
  thread disposes of handles (`src/core/AudioEngine.cpp:1171`, `:1224`) and the destructor clears the slot
  (`src/core/NotePlayHandle.cpp:165-167`).

## The fix (product code): publish, then apply on the thread that owns the handle

- **Publish (any thread, lock-free, one store).**
  `InstrumentTrack::publishLiveMpeExpression()` - `src/tracks/InstrumentTrack.cpp:445-464`: channel pressure,
  timbre and pitch of channel *N* packed into ONE `std::atomic<std::uint32_t>` in `m_mpeLive[N]`
  (`include/InstrumentTrack.h:508`), bit 31 marking "published" (`:513`) so the value-initialised zero means
  "nothing published" and a real all-neutral capture (pitch -48 st) stays distinguishable. Called from the input
  path at `src/tracks/InstrumentTrack.cpp:405`.
- **Apply (audio thread only).** `InstrumentTrack::applyLiveMpeExpression()` - `:467-508`, called from
  `NotePlayHandle::play()` at `src/core/NotePlayHandle.cpp:310`, before the note-on block and before the
  frequency mark. It only touches a note this track's MIDI *input* started
  (`note.origin() == NotePlayHandle::Origin::MidiInput`), so a clip note's saved expression is never overwritten.
  When the packed word differs from what the note carries it does exactly what the old code did from the wrong
  thread: `setMpeExpression()`, `setFrequencyUpdate()`, and `sendMpeExpressionMidi()` for a pressure/timbre change.
  A period with nothing new publishes nothing and writes nothing.
- **Release velocity.** `InstrumentTrack::captureMpeReleaseVelocity()` - `:412-441` publishes
  `(channel + 1) << 8 | velocity` into `m_mpeReleaseVelocity[key]` (`include/InstrumentTrack.h:519`) and touches no
  handle. `NotePlayHandle::noteOff()` takes it (`src/core/NotePlayHandle.cpp:492-497`) through
  `InstrumentTrack::takeMpeReleaseVelocity()` (`src/tracks/InstrumentTrack.cpp:511-533`), which consumes it once,
  only for its own channel, and converts it with the same `Note::releaseVelocityFromMidi()` the capture used. The
  note-on clears the key (`:594`), so a velocity that arrived for a key with no live note is handed to nobody -
  the behaviour before this existed.
- **Removed:** `loadMpeExpressionOntoChannelNotes()`, the function that wrote the handles from the input thread
  (declaration and definition gone; one caller, `trackMpeInputEvent`, now publishes).

Realtime: nothing is added to the audio thread that was not already there. `applyLiveMpeExpression()` is an
acquire load, three integer comparisons and (only on a change) the same `setMpeExpression`/`setFrequencyUpdate`/
`sendMpeExpressionMidi` calls the note-on path already makes on that thread; no allocation, no lock, no logging,
no wait, no new Qt call. The input path's cost is one relaxed-ish store (`release`) and one integer build.

## Callers and siblings checked

- `publishLiveMpeExpression()` - one caller, `trackMpeInputEvent()` (`:405`), which is reached only from
  `processInEvent()` for a bend / channel pressure / CC74 on a member channel.
- `applyLiveMpeExpression()` - one caller, `NotePlayHandle::play()` (audio thread).
  `MpePlaybackTest::aLiveExpressionEventReachesLayerNotes()` proves a layer track's own handle (created through
  the same `processInEvent` path, `src/core/InstrumentLayers.cpp:491`) takes its expression the same way.
- `takeMpeReleaseVelocity()` - one caller, `NotePlayHandle::noteOff()`, which every note (clip, live, sub-note,
  destructor) goes through; a note-off with nothing published is unchanged (`-1`).
- The note-on stamp (`src/tracks/InstrumentTrack.cpp:566-572`) is untouched and still runs before
  `addPlayHandle()` publishes the handle to the engine, so the first period already carries the channel's value.
- Siblings not changed here (reported under Other findings): the channel-wide `MidiKeyPressure` branch
  (`src/tracks/InstrumentTrack.cpp:609`), `InstrumentTrack::retunePlayingNotes()` (`include/InstrumentTrack.h:140-149`),
  and the GUI's read of a live handle's expression during record (`src/gui/editors/PianoRoll.cpp:4594`).
- `note.expression_set` (`src/core/ControlCommandsNoteExpression.cpp:397`) edits a **clip** note under a journal
  checkpoint; no live handle, no change needed.

## Regression tests

`tests/src/core/MpeInputPathTest.cpp` (the existing file for the real MPE input path):

- `liveExpressionIsNotWrittenByTheInputThread()` (`:344`) - pitch, pressure and CC74 through `processInEvent()`;
  the handle must still carry what it was born with, then a rendered period must move it to 2400 cents / 64 / 32;
  a note started afterwards is born with the channel's value (a value, not an event).
- `liveExpressionReachesTheNoteOnTheRenderThread()` (`:400`) - the same three axes dispatched from a second thread
  inside a `RealtimeScope` (the backend audio callback) while this thread holds the engine model: the delivery must
  finish **without** parking in `requestChangeInModel()` and `modelChangeWaiters()` must stay 0; the values must
  appear only on the note's own period; the note-off's velocity must be taken by the note; and after
  `removePlayHandlesOfTypes()` retires the handle, a velocity delivery to that key hands nothing to the next note.
- `renderOnePeriodOf()` (`:507`) renders one period of a handle, the period the audio thread would run.
- `inputPathStampsTheRightNotes()` now renders the period before reading a handle's expression (the value is
  applied on the period, by design).

`tests/src/core/MpePlaybackTest.cpp`: `aLivePressureEventReachesTheSoundingNote()` and
`aLiveExpressionEventReachesLayerNotes()` render the note's period before reading its expression, for the same
reason. No assertion was weakened: the same values, the same tolerances and the same "only its own note" checks
stay, and the rendered block is still the evidence that the axis reached the instrument.

Test matrix:

| Path | Covered by |
| --- | --- |
| Normal path: bend / pressure / CC74 arrive, the note renders them | `liveExpressionIsNotWrittenByTheInputThread`, `inputPathStampsTheRightNotes`, `MpePlaybackTest::aLivePressureEventReachesTheSoundingNote` |
| Failure: the delivery writes the handle from the input thread | `liveExpressionIsNotWrittenByTheInputThread` first three assertions (RED below) |
| Failure: the delivery waits for the engine model (the JACK callback) | `liveExpressionReachesTheNoteOnTheRenderThread` phase (1) (RED below) |
| Reconfiguration: MPE off / on, channel reset, a later note | the existing slots (`inputPathStampsTheRightNotes` last third), `liveExpressionIsNotWrittenByTheInputThread` second note |
| Exhaustion / retirement: the handle is destroyed while a capture is pending | `liveExpressionReachesTheNoteOnTheRenderThread` phase (5) - nothing holds a note pointer, and no stale velocity is inherited |
| Audio-thread constraints | the apply is one acquire load + comparisons + the calls the note-on already makes; the delivery is one atomic store; no lock, allocation, free, log or wait on either side |
| Supported platforms | Linux x86_64, Qt 6.12, GCC 16: built and run here. The change is portable C++20 (`std::atomic`, `std::array`); no platform API, no Qt-6-only call |

## Commands and exit codes

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/3192.r2/logs/`.

| # | Command | Log | Exit |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target MpeInputPathTest -j4` (new test, round-1 product code) | `build-4-red.log` | 0 |
| 2 | `ctest -R '^MpeInputPathTest$' --output-on-failure` - **RED** | `test-red-1.log` | **8** |
| 3 | `cmake --build build --target MpeInputPathTest -j4` (after the publish/apply fix) | `build-5-green.log` | 0 |
| 4 | `ctest -R '^MpeInputPathTest$' --output-on-failure` - **GREEN** | `test-green-1.log` | **0** |
| 5 | `cmake --build build --target MpeSynthExpressionTest MpePlaybackTest -j4` (after the packed-bit + origin fixes) | `build-7-fix2.log` | 0 |
| 6 | `ctest -R '^(MpeInputPathTest\|MpeSynthExpressionTest\|MpePlaybackTest)$'` | `test-green-3.log` | 8 -> fixed below |
| 7 | `cmake --build build --target zene MpeInputPathTest MpeExpressionTest MpePlaybackTest MpeNoteStorageTest MpeStreamSnapshotTest MpeSynthExpressionTest MpeInputActionTest InstrumentTrackRenderPathTest NotePlaybackTest TakeLaneCompPlaybackTest SessionTuningTest MidiInputTransformTest MidiInputGateTest MidiTakeRecordTest ExternalMidiDeviceTest ControlNoteExpressionCommandsTest PianoRollRecordTest -j4` | `build-16-family-final.log` | 0 |
| 8 | `ctest -R '^(MpeInputPathTest\|MpeExpressionTest\|MpePlaybackTest\|MpeNoteStorageTest\|MpeStreamSnapshotTest\|MpeSynthExpressionTest\|MpeInputActionTest\|InstrumentTrackRenderPathTest\|NotePlaybackTest\|TakeLaneCompPlaybackTest\|SessionTuningTest\|MidiInputTransformTest\|MidiInputGateTest\|MidiTakeRecordTest\|ExternalMidiDeviceTest\|ControlNoteExpressionCommandsTest\|PianoRollRecordTest)$' --output-on-failure` - 17/17 | `test-green-10-family-final.log` | **0** |
| 9 | `git diff -- include/InstrumentTrack.h src/tracks/InstrumentTrack.cpp src/core/NotePlayHandle.cpp > .../logs/fix.patch` | `fix.patch` | 0 |
| 10 | `git apply -R .../logs/fix.patch` (round-2 product change reversed = round-1 code) | - | 0 |
| 11 | `cmake --build build --target MpeInputPathTest -j4` | `build-14-red-proof.log` | 0 |
| 12 | `ctest -R '^MpeInputPathTest$' --output-on-failure` - **RED again** | `test-red-3-proof.log` | **8** |
| 13 | `git apply .../logs/fix.patch` (product change restored) | - | 0 |
| 14 | `cmake --build build --target MpeInputPathTest -j4` | `build-15-green-proof.log` | 0 |
| 15 | `ctest -R '^MpeInputPathTest$' --output-on-failure` - **GREEN again** | `test-green-9-proof.log` | **0** |
| 16 | `bash tests/fork-sources-gate.sh` / `bash tests/all-sources-reproduce.sh` / `bash tests/no-upstream-regression-gate.sh` / `tests/scripted/check-namespace` / `bash tests/no-tautology-gate.sh` / `python3 tests/doc-figures-gate.py` | `gate-fork-sources.log`, `gate-all-sources-reproduce.log`, `gate-no-upstream-regression.log`, `gate-check-namespace.log`, `gate-no-tautology.log`, `gate-doc-figures.log` | 0 / 0 / 0 / 0 / 0 / 0 |
| 17 | `bash tests/file-length-gate.sh --check` (no `REGRESSION:` line names a file this fix changed; my test file is 517 *lines* but under the 500 **code** lines the gate measures) | `gate-file-length.log` | 0 |
| 18 | `bash tests/complexity-gate.sh --check` - **cannot run here** (`lizard is not installed`), so it emits no `REGRESSION:` line | `gate-complexity.log` | 2 |
| 19 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` - **blocked by the session guard**, see Self-check | `gate-1.log` | 1 |

RED (step 12, `test-red-3-proof.log`), against the round-1 product code - both review defects, in one run:

```
FAIL!  : MpeInputPathTest::liveExpressionIsNotWrittenByTheInputThread() Compared values are not the same
FAIL!  : MpeInputPathTest::liveExpressionReachesTheNoteOnTheRenderThread() 'completedWhileModelHeld' returned FALSE. (an MPE expression event delivered on the audio callback waited for the engine's model lock)
Totals: 7 passed, 2 failed, 0 skipped, 0 blacklisted, 535ms
```

GREEN (step 15, `test-green-9-proof.log`):

```
1/1 Test #179: MpeInputPathTest .................   Passed    0.55 sec
100% tests passed out of 1
```

Step 6 is the honest intermediate: `MpeSynthExpressionTest` and `MpePlaybackTest` caught two real defects of my
first round-2 draft - the packed word's zero unpacked to pitch -48 st (a fresh note was bent down), and the
unpacked capture overwrote a clip note's saved expression. The published bit (`include/InstrumentTrack.h:513`) and
the MIDI-input origin guard (`src/tracks/InstrumentTrack.cpp:469-474`) are the fixes; after them the family is
green (step 8).

## Self-check

- **The regression test fails with the product change reverted and passes with it.** Step 10-12 reversed exactly
  the three product files (`git apply -R logs/fix.patch`, EXIT 0), rebuilt (EXIT 0), ran: EXIT **8** with the two
  review defects; step 13-15 restored (`git apply logs/fix.patch`, EXIT 0), rebuilt (EXIT 0), ran: EXIT **0**.
- **The test is deterministic.** No sleep, no clock assertion. `AudioEngine::modelChangeWaiters()` is the seam that
  says a control thread reached `requestChangeInModel()`, and the test decides on the two events it can observe
  (delivery finished / delivery parked). The 10 s deadline only turns a hang into a failure. Every value is
  injected: bend 12288 -> 2400 cents, bend 4096 -> -2400 cents, pressure 96/64, CC74 44/32, release velocity 96 at
  base velocity 100. The only cross-thread observation is the atomic `delivered` flag (release/acquire) and the
  waiters counter; the handle's fields are read on the thread that wrote them.
- **The fix is at the cause and complete.** The cause was "the input thread writes the handle": it no longer does,
  on either of the two sites (expression, release velocity), and the one function that existed only to do that
  (`loadMpeExpressionOntoChannelNotes`) is deleted. Both writers have exactly one caller each, both checked; the
  note-on stamp needs no publication because `addPlayHandle()` publishes the handle afterwards; layer notes come
  through the same `processInEvent` path and are covered by `aLiveExpressionEventReachesLayerNotes`.
- **No collateral damage.** Nothing serialized changed (no attribute, no loader, no model), so old projects load
  and render as before; MPE still runs only while `MpeExpression::isEnabled()`, and with it off both new paths
  return at once. A clip note's expression is explicitly left alone (origin guard), which is stricter than the
  round-1 code. The only behaviour change is timing: a live expression reaches the note on its next period (at
  most one period, ~11 ms at 44.1 kHz/512) instead of on the delivery thread, and the instrument's pressure/timbre
  event is sent from the render thread with it.
- **Undo, commands, realtime.** No command file changed, no undo step added or removed; the delivery is one atomic
  store on the MIDI thread, the apply runs on the audio thread with no allocation, lock, free, log or wait. Local
  TSan does not run on this box, so the race claim is a design argument read from the code (who writes, who reads,
  which thread owns the handle), stated as one.
- **Gates.** The six substantive gates pass individually (step 16), `file-length` reports no regression (step 17).
  `feature_check.py` itself cannot complete in this session: it must check out the base commit of the train into
  its own cache tree, and the session guard refuses git outside this worktree. Exact output (`gate-1.log`):

  ```
  bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12). Stage explicit paths and commit on your own branch only.
  ...
  subprocess.CalledProcessError: Command '['git', '-C', '.../gate-baselines/tree', 'checkout', '-q', '--detach', '9913d36d53...']' returned non-zero exit status 97.
  ```

  The base this run needs (`9913d36d53...`, the train's current merge base) has no cached baseline; round 1's base
  (`fa4a5f2e7...`) was cached, which is why the same command passed then. Both ratchet rows depend on that
  baseline: `file-length` prints no `REGRESSION:` line for any file this fix changed, and `complexity` cannot run
  at all here (`lizard is not installed`, exit 2), so it also prints none. I did not write the cache by hand - that
  would re-anchor a gate.
- **Sibling commands.** This fix changes no command file and registers no command (`git show --stat` lists
  `include/InstrumentTrack.h`, `src/tracks/InstrumentTrack.cpp`, `src/core/NotePlayHandle.cpp`, two test files,
  `tests/upstream-modifications.txt` and this report). The list the checker prints (`app.about`,
  `arrangement.fit_tracks`, ...) comes from the branch's whole diff against the train base, not from this change.
- **Completeness walk.** User path: hold a note on an MPE controller, gesture bend / pressure / CC74 - the capture
  reaches the note on its next period, the built-in synth follows through `MpeSynthExpression`, a hosted instrument
  gets the pressure/timbre events, and the note-off carries the release velocity. Edges driven here: a second note
  on another channel untouched (`inputPathStampsTheRightNotes`), MPE switched off mid-session, a note retired
  between the capture and the render, and a note that starts after the capture.

## Other findings

- `include/MpeExpression.h:192-196` still says "The caller stores the result on its note handles with plain
  assignments". That sentence described the round-1 mechanism and is now wrong (the caller publishes). Left
  unchanged deliberately: the header is pulled in by `include/Note.h`, so a comment edit there rebuilds nearly the
  whole tree in this lane; the accurate contract is documented at the code that replaced it
  (`include/InstrumentTrack.h:498-520`). Worth folding into the next change that touches that header anyway.
- `InstrumentTrack::processInEvent()`, channel-wide `MidiKeyPressure` branch
  (`src/tracks/InstrumentTrack.cpp:609`): `m_notes[event.key()]->setVolume(...)` writes a live handle's volume from
  the input thread - the same class of race, inherited from LMMS. Not this finding.
- `InstrumentTrack::retunePlayingNotes()` (`include/InstrumentTrack.h:140-149`, board card #712) stores
  `m_frequencyNeedsUpdate` from the control thread as a plain `bool` while the audio thread reads and spends it.
  Same class, documented as intended in that header; its own cluster.
- The note-off input branch (`src/tracks/InstrumentTrack.cpp:617-629`) still takes
  `requestChangeInModel()`/`doneChangeInModel()` from whatever thread delivered the note-off, so on JACK a note-off
  can block the audio callback. That is inherited upstream behaviour, not introduced here; this fix removes the
  guards it added rather than adding one.
- `src/gui/editors/PianoRoll.cpp:4594` copies a live handle's expression onto the recorded note on the GUI thread
  while the render thread may now write it (it already could, from the MIDI thread before this change).
