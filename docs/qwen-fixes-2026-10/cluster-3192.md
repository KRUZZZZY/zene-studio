FIXED

Cluster 3192: "MPE expression updates race with note rendering" (`src/tracks/InstrumentTrack.cpp`, historical `:370`).

**Round 3.** Round 2 published the release velocity per key and let `NotePlayHandle::noteOff()` take it. The review
and the diagnostic checkpoint showed two things were still wrong with that, plus one wrong test. This round removes
the per-key release-velocity mechanism and resolves the note under the engine guard instead.

## Verdict: REAL (unchanged)

- The MIDI input thread is not the render thread (`src/core/midi/MidiClient.cpp:327`; JACK dispatches MIDI from its
  audio callback before it renders, `src/core/audio/AudioJack.cpp:442` vs `:476`).
- The live note's expression fields were plain members written from that thread (`include/Note.h:453-455`,
  `include/NotePlayHandle.h:342-345`, `:506`) and read in `renderNextPeriod()` under `m_changeMutex`
  (`src/core/AudioEngine.cpp:1221-1224`).
- A write through `m_notes[key]` can outlive the handle: the render thread disposes of handles
  (`src/core/AudioEngine.cpp:1171`, `:1224`) and the destructor clears the slot (`src/core/NotePlayHandle.cpp:165-167`).

## The fix (product code)

Kept from round 2 (the expression handoff): `publishLiveMpeExpression()` packs pitch, pressure and CC74 into one
`std::atomic<std::uint32_t>` per member channel, `m_mpeLive` (`include/InstrumentTrack.h:508`); the input thread does
one store and never touches a note handle. `applyLiveMpeExpression()` (`src/tracks/InstrumentTrack.cpp:~452`) runs from
`NotePlayHandle::play()` (`src/core/NotePlayHandle.cpp:310`), so the render thread alone writes the handle.

Changed in round 3:

1. **Released notes freeze.** `applyLiveMpeExpression()` returns early when `note.isReleased()`
   (`src/tracks/InstrumentTrack.cpp:~475`). The input path used to update only notes still held on their key
   (`m_notes[key]`), so a note keeps its note-off expression through its release tail, as before.
2. **Release velocity: no per-key atomics, no take.** `m_mpeReleaseVelocity`, `takeMpeReleaseVelocity()`, the
   note-on clear and the take in `NotePlayHandle::noteOff()` are deleted. `captureMpeReleaseVelocity()`
   (`src/tracks/InstrumentTrack.cpp:410`) is now a pure event-to-velocity conversion: it returns the velocity on the
   note's own scale, or -1 (MPE off, not a member channel, velocity 0). It touches no note.
3. **Note-off resolves the note under the guard.** `InstrumentTrack::releaseLiveNote()`
   (`src/tracks/InstrumentTrack.cpp:510`, called from the `MidiNoteOff` branch of `processInEvent()` at `:620`) calls
   `requestChangeInModel()` first, reads `m_notes[key]` **once** after the guard, and if it is non-null stores the
   release velocity (own channel only) and calls `noteOff()` inside the same guard window. No pointer is held across
   a wait, and the retired-handle window is gone: the render thread retires a handle while holding the lock, and the
   destructor clears the slot, so after the guard the slot is a live note or null. Before this round the code tested
   `m_notes[key] != nullptr`, waited for the guard, then dereferenced `m_notes[key]` again (null if retired during the
   wait). This adds no new JACK wait that note-on/off did not already have: the note-off branch already took the same
   guard for any live note; the one difference is a note-off for a key with no live note now also takes it.
4. **`sendMpeExpressionMidi()` stays on the render thread**, from `applyLiveMpeExpression()`. This is the same
   thread and the same `processOutEvent` path the note's own note-on and note-off messages already use
   (`NotePlayHandle::play`/`noteOff`); documented at the call.

Removed in round 2 and still gone: `loadMpeExpressionOntoChannelNotes()`; the round-1 `requestChangesGuard()` calls
on the expression path.

Realtime: the expression path takes no lock (one atomic store on the input side; an acquire load and comparisons on
the render side). Nothing is added to the audio thread that allocates, frees, locks, waits or logs. Note-off is the
existing guarded path; nothing new waits on the expression path.

Behaviour difference I cannot avoid by design: a bend that arrives and then is followed by a note-off within one
render period (about 11 ms at 44.1 kHz / 512) is not applied, because the released note no longer takes published
values. Before, the bend was written to the handle at once. The note keeps the value from its last rendered period.
This is within the one-period latency that the render-thread handoff already has.

## Callers and siblings checked

- `captureMpeReleaseVelocity()`: one caller now, `releaseLiveNote()`. `trackMpeInputEvent()` no longer calls it
  (`:386-390`); it keeps `m_mpeExpression.noteOff()`.
- `releaseLiveNote()`: one caller, the `MidiNoteOff` case. The note-on-with-velocity-0 spelling falls into the same
  case (and carries velocity 0, which captures nothing), as before.
- The sustain-pedal branch (`isSustainPedalPressed()` and `m_sustainedNotes`) is unchanged and runs inside the same guard
  window, with the same note pointer.
- `applyLiveMpeExpression()`: one caller (`NotePlayHandle::play()`); a layer track's own handle takes the same path
  (`MpePlaybackTest::aLiveExpressionEventReachesLayerNotes`); a clip note is skipped by the origin check.
- Not changed, listed under Other findings: the `MidiKeyPressure` branch, `retunePlayingNotes()`, the GUI read of a
  live handle.

## Regression tests (`tests/src/core/MpeInputPathTest.cpp`)

| Slot | What it holds |
| --- | --- |
| `releasedNoteKeepsItsExpressionThroughTheReleaseTail` | bend/pressure/CC74 published after the note-off do not reach the released note, over a rendered period |
| `releaseVelocityIsNotHandedToAClipNote` | an input note-off with no live input note leaves the release velocity unclaimed; a clip note (`Origin::MidiClip`) on the same key and channel that ends afterwards keeps -1 |
| `threadedNoteOffStoresTheReleaseVelocity` | the note-off runs on a second thread; the velocity is read only after `join()` |
| `noteOffAfterRetirementTouchesNoRetiredHandle` | single-threaded retire then note-off; and threaded: the writer parks on the model lock (observed through `modelChangeWaiters()`, not a clock), the note is retired while the lock is held (recursive mutex), and the writer then finds an empty slot |
| `liveExpressionReachesTheNoteOnTheRenderThread` | (kept) pitch, pressure, CC74 delivered from a `RealtimeScope` thread while the model is held finish without waiting; values appear only on the note's own period; release velocity lands on the note |
| `liveExpressionIsNotWrittenByTheInputThread`, `inputPathStampsTheRightNotes`, the `noteOffVelocity*` slots | (kept) |

Synchronisation: no value is read while a writer may still run. The threaded slots read note state only after
`join()`. The one cross-thread observation made before a join is the waiter counter, used only to know that the writer
reached the lock; it decides nothing about a value. The 10 s deadline only turns a hang into a failure.

## Commands and exit codes

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/3192.r3/logs/`. Builds and runs were done by a Haiku assistant; I read
the logs myself. (A first green run reported EXIT=0 for a build that had failed on `midiPort()` not being const; I read
`build-green.log`, fixed the signature, and reran everything. Only the final runs below count.)

| # | Command | Log | Exit |
| --- | --- | --- | --- |
| 1 | `git diff -- include/InstrumentTrack.h src/core/NotePlayHandle.cpp src/tracks/InstrumentTrack.cpp > logs/fix.patch` | `fix.patch` | 0 |
| 2 | `git apply -R logs/fix.patch` (round-3 product change reversed = round-2 code) | - | 0 |
| 3 | `cmake --build build --target MpeInputPathTest -j4` | `build-red3.log` | 0 (ends `Built target MpeInputPathTest`) |
| 4 | `cd build/tests && ctest -R '^MpeInputPathTest$' --output-on-failure` - **RED** | `test-red3.log` | **8** |
| 5 | `git apply logs/fix.patch` | - | 0 |
| 6 | `cmake --build build --target zene MpeInputPathTest MpeExpressionTest MpePlaybackTest MpeNoteStorageTest MpeStreamSnapshotTest MpeSynthExpressionTest MpeInputActionTest InstrumentTrackRenderPathTest NotePlaybackTest TakeLaneCompPlaybackTest SessionTuningTest MidiInputTransformTest MidiInputGateTest MidiTakeRecordTest ExternalMidiDeviceTest ControlNoteExpressionCommandsTest PianoRollRecordTest -j4` | `build-green3.log` | 0 (no `error:`, ends `Built target PianoRollRecordTest`) |
| 7 | `ctest -R '^MpeInputPathTest$' --output-on-failure` - **GREEN** | `test-green3.log` | **0** |
| 8 | `ctest -R '^(MpeInputPathTest\|MpeExpressionTest\|MpePlaybackTest\|MpeNoteStorageTest\|MpeStreamSnapshotTest\|MpeSynthExpressionTest\|MpeInputActionTest\|InstrumentTrackRenderPathTest\|NotePlaybackTest\|TakeLaneCompPlaybackTest\|SessionTuningTest\|MidiInputTransformTest\|MidiInputGateTest\|MidiTakeRecordTest\|ExternalMidiDeviceTest\|ControlNoteExpressionCommandsTest\|PianoRollRecordTest)$' --output-on-failure` - 17/17 | `test-family3.log` | **0** |
| 9 | `bash tests/fork-sources-gate.sh` / `bash tests/all-sources-reproduce.sh` / `bash tests/no-upstream-regression-gate.sh` / `tests/scripted/check-namespace` / `bash tests/no-tautology-gate.sh` / `python3 tests/doc-figures-gate.py` | `gate-fork-sources.log`, `gate-all-sources-reproduce.log`, `gate-no-upstream-regression.log`, `gate-check-namespace.log`, `gate-no-tautology.log`, `gate-doc-figures.log` | 0 / 0 / 0 / 0 / 0 / 0 |
| 10 | `bash tests/file-length-gate.sh --check` | `gate-file-length.log` | 0 (`PASS (check mode: no regressions)`) |
| 11 | `bash tests/complexity-gate.sh --check` - cannot run here: `lizard is not installed` | `gate-complexity.log` | 2 |
| 12 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` - blocked by the session guard, see Self-check | `gate-1.log` | 1 |

RED (step 4, `test-red3.log`), against the round-2 product code:

```
FAIL!  : MpeInputPathTest::releasedNoteKeepsItsExpressionThroughTheReleaseTail() Compared values are not the same
   Actual   (note->mpePitchCents()): -2400      Expected (2400)
FAIL!  : MpeInputPathTest::releaseVelocityIsNotHandedToAClipNote() Compared values are not the same
   Actual   (clip->releaseVelocity()): 96       Expected (-1)
Received signal 11 (SIGSEGV), ... accessing address 0x0000000000000168      (noteOffAfterRetirementTouchesNoRetiredHandle,
   in NotePlayHandle::noteOff, called from InstrumentTrack::processInEvent)
179 - MpeInputPathTest (SEGFAULT)
```

GREEN (step 7): `1/1 Test #179: MpeInputPathTest ... Passed`, `100% tests passed out of 1`. Step 8: `100% tests passed out of 17`.

Not run: an AddressSanitizer build (none is configured in this worktree), and TSan (does not run on this box). The retired-handle
case is shown by the SIGSEGV in the red run and by the clean green run; the data-race claim is a design argument
(who writes, who reads, which thread owns the handle).

## Self-check

- **The regression test fails with the product change reverted and passes with it.** Steps 2-4 reversed exactly the
  three product files (EXIT 0), rebuilt (EXIT 0) and ran: EXIT 8 with the three failures above; steps 5-7 restored
  and ran: EXIT 0.
- **The test is deterministic.** No sleeps, no clock assertion; every value is injected (bend 12288 -> 2400 cents,
  4096 -> -2400, velocity 96/88, pressure 96/50/100, CC74 44/77). Cross-thread values are read after `join()`. The
  waiter counter is a progress signal for "the writer reached the lock", not a value handoff.
- **The fix is at the cause and complete.** The input thread writes no note field for expression, and the release
  velocity is written by the thread that resolved the note under the guard. The per-key release-velocity mechanism is
  deleted (no caller left; `m_mpeReleaseVelocity`, `takeMpeReleaseVelocity` do not appear in `src/`, `include/`,
  `tests/`). Every caller/sibling is listed above.
- **No collateral damage.** Nothing serialized changed. MPE off: `captureMpeReleaseVelocity()` returns -1 at once and
  `applyLiveMpeExpression()` returns at once. A clip note is never given an input note-off's velocity or live
  expression. One timing difference, stated above (a bend within one period of a note-off).
- **Undo, commands, realtime.** No command file changed; no undo step added. No new allocation, lock, wait or log on
  the audio thread; the note-off keeps the guard it already took.
- **Gates.** The six substantive gates pass (step 9); `file-length` shows no regression (step 10). `complexity` cannot
  run (`lizard is not installed`), so I extracted the note-off into `releaseLiveNote()` (a small function) so that
  `processInEvent()` does not grow. `feature_check.py` cannot finish in this session: it must check out the train's base
  commit into its own cache tree and the session guard refuses it. Exact output (`gate-1.log`):

  ```
  bunny-guard: 'git -C .../gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed in a Bunny lane ...
  subprocess.CalledProcessError: ... returned non-zero exit status 97.
  ```

  I did not write the baseline cache by hand: that would re-anchor a gate.
- **Sibling commands.** No command file changed and none was added.
- **Completeness walk.** Hold an MPE note, bend / press / slide CC74: the note takes it on its next period. Release with a
  velocity: the note gets it, and plays the tail with the expression it had. A clip note on the same key and channel
  is untouched. A note retired while a note-off waits: the note-off finds an empty slot.

## Other findings

- `include/MpeExpression.h:192-196` still says "The caller stores the result on its note handles with plain
  assignments" (round-1 wording). Left unchanged: a comment edit there rebuilds nearly the whole tree.
- `InstrumentTrack::processInEvent()`, `MidiKeyPressure` branch: `m_notes[event.key()]->setVolume(...)` also reads
  the slot and writes a live handle from the input thread, with no guard. Same class of race, inherited. Not this finding.
- `InstrumentTrack::retunePlayingNotes()` (`include/InstrumentTrack.h:140-149`) stores `m_frequencyNeedsUpdate` from the
  control thread as a plain `bool`. Same class; its own cluster.
- `src/gui/editors/PianoRoll.cpp:4594` copies a live handle's expression onto the recorded note on the GUI thread
  while the render thread may write it.
- In the note-off branch, the sustain-pedal branch reads `m_notes[...]` slots after the guard as before; not changed.

Delegation: Haiku ran the builds and test runs (red proof, green proof, family) and reported exit codes and failing
slots; I read the logs and the diff myself. The first green build it reported as EXIT=0 had actually failed; I caught
that from the log and the final runs were repeated. I wrote the fix, the tests and this report.
