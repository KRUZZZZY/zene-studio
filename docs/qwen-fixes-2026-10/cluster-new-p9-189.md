FIXED

Cluster new-p9-189: "Scheduled MIDI note events take a mutex" (`src/tracks/InstrumentTrackNotes.cpp`, historical line 242).

The finding is REAL. A scheduled note reaches `InstrumentTrack::sendNoteOn()` / `sendNoteOff()` on the
audio thread, and before this change both functions took `m_midiNotesMutex` around the piano key-state
update *and* the instrument callbacks. `InstrumentTrack::silenceAllNotes()` takes the same mutex from the
control thread, so the audio thread could wait on a control thread inside a period. The lock is gone from
the audio path: the per-key running-note count and the piano key state are atomics, and the rendered state
is published through them.

## Evidence

- **The path is the audio thread.** `NotePlayHandle::play()` sends the note-on at
  [src/core/NotePlayHandle.cpp:326](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/core/NotePlayHandle.cpp:326)
  and the note-off at
  [src/core/NotePlayHandle.cpp:507](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/core/NotePlayHandle.cpp:507)
  through `processOutEventForNote()`
  ([src/tracks/InstrumentTrack.cpp:678](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/tracks/InstrumentTrack.cpp:678)),
  which dispatches to `sendNoteOn()` / `sendNoteOff()`
  ([src/tracks/InstrumentTrack.cpp:707](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/tracks/InstrumentTrack.cpp:707),
  [src/tracks/InstrumentTrack.cpp:711](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/tracks/InstrumentTrack.cpp:711)).
  `NotePlayHandle` is a play handle run by `AudioEngine::renderStageInstruments`.
- **The old shape.** `sendNoteOn` locked `m_midiNotesMutex` at line 326 and unlocked at 342; inside the
  critical section it called `m_piano.setKeyState()` and `m_instrument->handleMidiEvent()`.
  `sendNoteOff` locked at 349 and unlocked at 359 around the same pair. (Both still exist in this branch's
  base; the RED run below restores them and the test fails there.)
- **The other holder is the control thread.** `silenceAllNotes()` locked the same mutex
  ([src/tracks/InstrumentTrack.cpp:726-734 before the fix](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/tracks/InstrumentTrack.cpp:726));
  it is called from track teardown, instrument replacement, preset loading and the MIDI input note-off path
  (`InstrumentTrack.cpp:229, 636, 1225, 1439, 1463`).
- **The fix.**
  - `m_runningMidiNotes` is `std::array<std::atomic<int>, NumKeys>`
    ([include/InstrumentTrack.h:504](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/include/InstrumentTrack.h:504)).
    `sendNoteOn` uses `fetch_add` and releases a retriggered key when the previous count was non-zero
    ([src/tracks/InstrumentTrackNotes.cpp:335](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/tracks/InstrumentTrackNotes.cpp:335));
    `sendNoteOff` uses a compare-exchange loop instead of `--` so a concurrent control-thread reset can
    never make the count negative
    ([src/tracks/InstrumentTrackNotes.cpp:355-365](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/tracks/InstrumentTrackNotes.cpp:355)).
  - The piano key state is `std::array<std::atomic<bool>, NumKeys>`
    ([include/Piano.h:91](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/include/Piano.h:91)),
    loaded by `isKeyPressed()`
    ([include/Piano.h:56](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/include/Piano.h:56))
    and stored by `Piano::setKeyState()`, `handleKeyPress()` and `handleKeyRelease()`
    ([src/core/Piano.cpp:81](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/core/Piano.cpp:81),
    [:103](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/core/Piano.cpp:103),
    [:120](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/core/Piano.cpp:120)).
    This also removes the data race that existed between the audio thread's writes and the GUI's reads of
    the plain `bool` array.
  - `silenceAllNotes()` resets the counts with a relaxed store and no lock
    ([src/tracks/InstrumentTrack.cpp:726-735](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/src/tracks/InstrumentTrack.cpp:726)).
  - `m_midiNotesMutex` stays declared as a test sentinel that product code no longer acquires
    ([include/InstrumentTrack.h:510](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189/include/InstrumentTrack.h:510)),
    the same pattern `AutomatableModel::m_valueBufferMutex` uses for its ramp-read proof. The regression
    test takes it with `tryLock(0)` to prove the audio path does not.
- **Callers and siblings checked.** Every access to `m_runningMidiNotes` is the constructor, the two note
  paths and `silenceAllNotes` (`rg -n m_runningMidiNotes src include`, four sites). Every write to
  `m_pressedKeys` is `setKeyState`, `handleKeyPress` and `handleKeyRelease`; the only reads are
  `isKeyPressed` and those same functions (`rg -n m_pressedKeys src include`, five sites). The keyswitch
  path `sendArticulationEvents()`/`releaseKeyswitch()` never touched the mutex or the counts and is
  unchanged. `processOutEvent()`'s non-note branch is unchanged.

## Regression tests

Both are in `tests/src/tracks/InstrumentTrackRenderPathTest.cpp` (the existing test file for the instrument
track's audio-thread properties) and are deterministic: no wall-clock time, no sleep, no second thread.

- `theScheduledNotePathTakesNoMutex` (line 195). Installs a test instrument that, inside
  `handleMidiEvent()` (called by `sendNoteOn`/`sendNoteOff`), calls `m_midiNotesMutex.tryLock(0)` and
  records whether the lock is held; a direct connection to `Piano::dataChanged` does the same at the
  key-state publication. It then sends a real `MidiNoteOn` and `MidiNoteOff` through
  `InstrumentTrack::processOutEvent()` and requires the lock to be free at both probes, the key state to
  be published, and `isKeyPressed()` to follow the note. `tryLock(0)` never waits, so the observation is
  instantaneous and ordered by the calling thread.
- `runningNotesOnOneKeyKeepTheirCount` (line 250). Guards the behaviour the old lock also provided: two
  note-ons on one key retrigger and one note-off does not end the other; `silenceAllNotes()` forgets the
  counts, so a note started after it still retriggers nothing stale and still gets its note-off.

Test matrix:

| Path | Covered by |
| --- | --- |
| Normal path: scheduled note-on and note-off through the real `processOutEvent` | `theScheduledNotePathTakesNoMutex` |
| Failure: the note path takes the mutex (instrument callback) | the same test's instrument probe; the RED run below fails exactly here |
| Failure: the note path takes the mutex (piano publication) | the same test's `Piano::dataChanged` probe |
| Exhaustion/overlap: two note-ons on one key, two note-offs | `runningNotesOnOneKeyKeepTheirCount` |
| Reconfiguration: `silenceAllNotes()` resets the counts, then a new note | `runningNotesOnOneKeyKeepTheirCount`, second half |
| Audio-thread constraints | the test asserts `std::atomic<bool>`/`std::atomic<int>` are always lock-free; the fix adds no lock, allocation, free, log, signal or wait to either path |
| Platform | built and run on Linux x86_64 (Qt 6.12, GCC 16); no platform API, no POSIX-only path |

## Commands and exit codes

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-189.r1/logs/`. Every build ran with
`CCACHE_TEMPDIR=$TMPDIR` (ccache's temp directory is outside the write jail).

| Step | Command | EXIT |
| --- | --- | --- |
| Baseline build of the existing test target | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target InstrumentTrackRenderPathTest -j4 > logs/build-1-baseline.log 2>&1` | 0 |
| Baseline run (before any edit) | `cd build/tests && ctest -R '^InstrumentTrackRenderPathTest$' --output-on-failure > logs/test-1-baseline.log 2>&1` | 0 |
| RED build (new tests, product not yet fixed) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target InstrumentTrackRenderPathTest -j4 > logs/build-2-red.log 2>&1` | 0 |
| **RED run** | `cd build/tests && ctest -R '^InstrumentTrackRenderPathTest$' --output-on-failure > logs/test-2-red.log 2>&1` | **8** |
| RED assertion | `FAIL! : InstrumentTrackRenderPathTest::theScheduledNotePathTakesNoMutex() '!instrument->sawMutexHeld()' returned FALSE. (the scheduled note-on held m_midiNotesMutex across the instrument callback)` at `InstrumentTrackRenderPathTest.cpp(223)`; `Totals: 4 passed, 1 failed` | - |
| GREEN build (fix, zene + test) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene InstrumentTrackRenderPathTest -j4 > logs/build-3-green.log 2>&1` | 0 |
| **GREEN run** | `cd build/tests && ctest -R '^InstrumentTrackRenderPathTest$' --output-on-failure > logs/test-3-green.log 2>&1` | **0** |
| GREEN run, verbose | `cd build/tests && ctest -R '^InstrumentTrackRenderPathTest$' -V > logs/test-4-green-verbose.log 2>&1` | **0**; `Totals: 5 passed, 0 failed` |
| Proof: save the product diff | `git diff -- include/InstrumentTrack.h include/Piano.h src/core/Piano.cpp src/tracks/InstrumentTrack.cpp src/tracks/InstrumentTrackNotes.cpp > logs/fix.patch` | 0 |
| Proof: reverse the product change | `git apply -R logs/fix.patch` | 0 |
| Proof: rebuild without the fix | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene InstrumentTrackRenderPathTest -j4 > logs/build-4-nofix.log 2>&1` | 0 |
| **Proof: run without the fix** | `cd build/tests && ctest -R '^InstrumentTrackRenderPathTest$' --output-on-failure > logs/test-5-nofix.log 2>&1` | **8**; same assertion as RED, `Totals: 4 passed, 1 failed` |
| Proof: restore the fix | `git apply logs/fix.patch` | 0 |
| Proof: rebuild with the fix | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene InstrumentTrackRenderPathTest -j4 > logs/build-5-fixed.log 2>&1` | 0 |
| **Proof: run with the fix** | `cd build/tests && ctest -R '^InstrumentTrackRenderPathTest$' --output-on-failure > logs/test-6-fixed.log 2>&1` | **0** |
| Family build (14 targets that include the changed headers) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target InstrumentTrackRenderPathTest InstrumentRoutingTest InstrumentLayersCommandsTest InstrumentLayerZonePublishTest InstrumentLatencyPdcTest InstrumentLowLatencyMonitorTest HybridTrackInstrumentRoleTest MpeSynthExpressionTest MidiInputGateTest MidiOutQueueTest PianoRollBenchmarkTest TouchInstrumentSurfaceTest NoteEditorTest InstrumentSetupUiTest -j4 > logs/build-6-family.log 2>&1` | 0 |
| **Family run** | `cd build/tests && ctest -R '^(the 14 names above)$' -j2 --output-on-failure > logs/test-7-family.log 2>&1` | **0**; `100% tests passed out of 14` |
| Gates (after registration) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-189 > logs/gate-2-feature-check.log 2>&1` | **0**; `RESULT: PASS` (8/8 rows) |

## Registration

No new product file. `src/tracks/InstrumentTrackNotes.cpp` and
`tests/src/tracks/InstrumentTrackRenderPathTest.cpp` are already in `tests/fork-sources.txt`. The four
inherited files changed here (`include/InstrumentTrack.h`, `src/tracks/InstrumentTrack.cpp`,
`include/Piano.h`, `src/core/Piano.cpp`) each got a `cluster new-p9-189` row in
`tests/upstream-modifications.txt`, in the same commit.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.** Yes.
   `logs/test-5-nofix.log`, EXIT=8: `FAIL! : ...theScheduledNotePathTakesNoMutex() '!instrument->sawMutexHeld()'
   returned FALSE. (the scheduled note-on held m_midiNotesMutex across the instrument callback)`,
   `Totals: 4 passed, 1 failed`. `logs/test-6-fixed.log`, EXIT=0. The same failure appeared in the
   pre-fix RED run (`logs/test-2-red.log`, EXIT=8, same assertion, line 223).
2. **The test is deterministic.** No wall-clock timing, no sleep and no second thread. The seam is
   `QMutex::tryLock(0)` inside the real instrument callback and inside a direct `Piano::dataChanged`
   connection; both run on the test's own thread, so the observation is ordered by the call itself.
   The count semantics are injected as real `MidiEvent`s and read back as instrument call counts.
3. **Fix at the cause and complete.** The cause is the shared mutable state behind `m_midiNotesMutex`
   (per-key running counts, piano key flags). Both are atomics now, and every writer and reader was
   updated (the four `m_runningMidiNotes` sites and the five `m_pressedKeys` sites listed under Evidence).
   `silenceAllNotes()` keeps its reset semantics without the lock, and `sendNoteOff`'s CAS keeps the count
   from going negative if that reset lands concurrently. The keyswitch/articulation path never used
   either piece of state and is unchanged.
4. **No collateral damage.** The note-path behaviour is unchanged: retrigger, per-key count, MIDI-part
   channel choice, release velocity, voice bookkeeping and the `dataChanged` notification all run as
   before, and the key state publishes the same values. No save/load format or model setting changed; the
   atomics are runtime-only. No limit or check was added. The 14-target family that includes the changed
   headers passes (`logs/test-7-family.log`, EXIT=0), including the routing tests that drive
   `processOutEvent` directly and the piano-roll tests that read `isKeyPressed`.
   Realtime: the two functions lost two mutex acquisitions and gained relaxed atomic loads/stores and one
   short CAS loop; nothing allocates, frees, logs, waits or emits a signal that the old path did not
   already emit.
5. **Verdict evidence.** REAL: `sendNoteOn`/`sendNoteOff` locked `m_midiNotesMutex` on the audio thread
   (pre-fix `src/tracks/InstrumentTrackNotes.cpp:326-342` and `:349-359`) and `silenceAllNotes()` took it
   from the control thread (`src/tracks/InstrumentTrack.cpp:726-734` before the change). The RED run
   demonstrates the lock is held across the instrument callback.
6. **Sibling commands.** This fix changes no command file: the commit's 8 paths are the five product
   files, the test, `tests/upstream-modifications.txt` and this report (`git show --stat <commit>`), and
   none of them is under `ControlCommands*`, `ControlReversibilityTable*` or `ControlRegistry`. The
   `app.*` / `arrangement.*` sibling list the check prints comes from the branch's whole diff against the
   older `FIX-BASE` (`355 changed files`, most from the train merges below this branch), not from this
   change. No command id, schema or snapshot entry was added, removed or altered, so the
   `ControlCommandsSnapshot` canary is untouched.

## Other findings

- `Piano::setKeyState()` still `emit`s `dataChanged()` from the audio thread when a scheduled note
  changes a key. In a running GUI that is a queued connection, which allocates a `QMetaCallEvent` on the
  audio thread. This predates the finding, the mutex change does not alter it, and replacing it needs a
  GUI-side poll (the pattern the fork used for Carla's deferred parameter signals), so it is not fixed
  here. It is the remaining realtime-rule concern on this path and deserves its own change.
- `m_notes[]` and the MPE capture state are read on the MIDI input thread without a lock and were never
  guarded by `m_midiNotesMutex`, so this change neither introduces nor removes a race there.
