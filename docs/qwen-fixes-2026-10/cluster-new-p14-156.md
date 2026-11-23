FIXED

# Cluster new-p14-156: LV2 MIDI input uses a contended spinlock

`Lv2Proc::handleMidiInputEvent()` was called by more than one thread (the GUI for live MIDI
input, the audio thread for the song's own notes) and serialised them through an
`atomic_flag` spinlock around a single-writer ringbuffer. Before the change
(`src/core/lv2/Lv2Proc.cpp:449-476` at the base commit, `include/Lv2Proc.h:222`):

```cpp
while (m_ringLock.test_and_set(std::memory_order_acquire))  // acquire lock
     ; // spin
```

A producer that lost the flag spun for as long as the holder was descheduled, with no
bound. The holder can be a non-realtime thread, so the audio thread could wait
unboundedly on it in the middle of a period. REAL: the code matched the finding and the
comment above it stated the multi-thread caller as the reason for the spin.

## The fix (at the cause)

The ringbuffer and its spinlock are replaced by the bounded, lock-free multi-producer
handoff this tree already uses for hosted MIDI (`include/MidiHandoffQueue.h`, the same type
Carla's MIDI hand-off uses):

- `include/Lv2Proc.h:227-234` - `m_midiInputQueue` is
  `MidiHandoffQueue<m_maxMidiInputEvents, MidiInputEvent>`; `m_ringLock`,
  `m_midiInputBuf` and `m_midiInputReader` are gone. `MidiInputEvent` moved to the header
  (`include/Lv2Proc.h:56-63`) because the queue stores it by value in fixed storage.
- `src/core/lv2/Lv2Proc.cpp:436-454` - `handleMidiInputEvent()` pushes the event. `push()`
  makes at most `MidiHandoffQueue::MaxPushAttempts` (8) reservation attempts with one
  compare-exchange each, refuses (and counts) when the queue is full or the budget is
  exhausted, and never waits for another producer. The full-queue `qWarning` left this
  path: the audio thread is one of the callers.
- `src/core/lv2/Lv2Proc.cpp:287-305` - `copyModelsFromCore()` drains with the queue's
  wait-free single-consumer `pop()` instead of `read_space()/read()`. Same per-period
  bound (queue capacity, unchanged at 1024).
- `include/Lv2Proc.h:262-271` - a `LMMS_TESTING`-only seam exposes the queue to the test.
- `include/MidiHandoffQueue.h:80-81` - `EventType` alias so a test can name the event type
  without hard-coding the processor's struct.

No command, no undo step, and no serialised project state is touched: the queue is runtime
only, so old projects load and render exactly as before. Capacity is unchanged; the only
behavioural difference under overload is that a refused event is counted by the queue
(`dropped()`) instead of a warning being logged from a possibly-realtime caller.

## Callers and siblings checked

- The only producer path is `Lv2ControlBase::handleMidiInputEvent()`
  (`include/Lv2ControlBase.h:149`, `src/core/lv2/Lv2ControlBase.cpp:302-306`), which loops
  every `Lv2Proc`; the only consumers are `copyModelsFromCore()` (audio thread, and the
  construction-time initialisation at `src/core/lv2/Lv2Proc.cpp:1128`) and `playImpl()` in
  `plugins/Lv2Instrument/Lv2Instrument.cpp:231`. One queue per proc keeps pop()
  single-consumer.
- The four MIDI members were the only users of the spinlock/ringbuffer in the class; the
  header's `<ringbuffer/ringbuffer.h>` include went with them, and every target that
  includes `Lv2Proc.h` was rebuilt (`lmmsobjs` incl. `Lv2ControlBase.cpp`/`Lv2ViewBase.cpp`,
  `lv2instrument`, `Lv2StateTest`, `Lv2MidiHandoffTest`).
- Other queue users (`MpscQueue`, `MidiHandoffQueue` for Carla) are unchanged; the only
  shared-header change is the `EventType` alias.

## Regression test

`tests/src/core/Lv2MidiHandoffTest.cpp` (new, registered in `tests/CMakeLists.txt:960-962`
and `tests/fork-sources.txt:2858`; LV2 fixture/deps wired at `tests/CMakeLists.txt:4396-4413`
and `:2750`). It builds a processor from the real LV2 fixture instrument and asserts, on
that processor's own handoff:

- normal path: four producer events come out in order with their time/offset intact;
- exhaustion: capacity events are accepted, the next one is refused and counted, the
  accepted ones are still ordered, and the queue recovers after the consumer drains it;
- contention: a producer that loses every reservation to an injected competitor is refused
  after exactly `MaxPushAttempts` attempts - the deterministic form of "another producer
  holds the flag and is descheduled", injected through the queue's existing test hook (no
  thread, no sleep, no clock);
- consumer: `copyModelsFromCore()` takes every queued event and leaves none;
- audio thread: eight `handleMidiInputEvent()` calls inside a `RealtimeScope` with the
  allocation probe armed allocate nothing (0).

The unfixed code is reached through a compile-time-detected seam: when the seam is absent
the helper `checkMidiInputHandoff()` that is selected fails with "Lv2Proc hands its MIDI
producers a contended spinlock (m_ringLock): cluster new-p14-156", so the test fails there
instead of skipping. A source check of `Lv2Proc::handleMidiInputEvent` independently
asserts the absence of `test_and_set`/`atomic_flag`/`m_ringLock` in its body.

(The regression was first written as a slot in `tests/src/core/Lv2StateTest.cpp`; Gate 7
then reported that file over 500 code lines, so it was moved to its own file rather than
re-anchoring the length baseline. The first RED run of that earlier placement is
`logs/test-1-red.log`, EXIT=8.)

## Evidence (commands and exit codes, copied from the logs)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-156.r1/logs`.

| Step | Command | Exit |
| --- | --- | --- |
| fix.patch written | `git diff -- include/Lv2Proc.h src/core/lv2/Lv2Proc.cpp include/MidiHandoffQueue.h > .../logs/fix.patch` | 0 |
| product change reverted | `git apply -R .../logs/fix.patch` | 0 |
| RED build | `cd build && cmake --build . --target Lv2MidiHandoffTest -j4 > .../logs/build-15-red-proof.log 2>&1` | 0 |
| RED test | `cd build/tests && ./Lv2MidiHandoffTest > .../logs/test-17-red-proof.log 2>&1` | **1** |
| product change restored | `git apply .../logs/fix.patch` | 0 |
| GREEN build | `cd build && cmake --build . --target zene Lv2MidiHandoffTest -j4 > .../logs/build-16-green-proof.log 2>&1` | 0 |
| GREEN test | `cd build/tests && ./Lv2MidiHandoffTest > .../logs/test-18-green-proof.log 2>&1` | **0** |
| family | `cd build/tests && ctest -R "Lv2\|MidiHandoff\|MpscQueue\|MidiJackOutSchedule\|AudioBusPlayHandleQueue\|InstrumentLatencyPdc" --output-on-failure > .../logs/test-19-family-final2.log 2>&1` | 0 (11/11) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-3-final.log 2>&1` | 0 (`RESULT: PASS`, 8/8) |

RED output (`logs/test-17-red-proof.log`):

```text
FAIL!  : Lv2MidiHandoffTest::midiInputHandoffIsBoundedAndLockFree() Lv2Proc hands its MIDI producers a contended spinlock (m_ringLock): cluster new-p14-156
FAIL!  : Lv2MidiHandoffTest::midiInputHandoffIsBoundedAndLockFree() '!producerSpins' returned FALSE. (... while (m_ringLock.test_and_set(std::memory_order_acquire)) ...)
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 480ms
```

GREEN output (`logs/test-18-green-proof.log`): `Totals: 3 passed, 0 failed, 0 skipped`.

One intermediate family run (`logs/test-5-family.log`, EXIT=8) failed
`CarlaMidiHandoffTest::concurrentMidiStressIsOrderedAndAllocationFree()` with
"processing did not overlap MIDI producer activity"; it passed 3/3 standalone reruns
(`logs/test-carla-1..3.log`, EXIT=0 each) and in both later family runs. It is a
timing-dependent pre-existing slot in another module, not touched by this change; listed
under Other findings.

## Self-check

- **Regression fails reverted / passes with the fix.** Reverted (non-test hunks only):
  `./Lv2MidiHandoffTest` EXIT=1, `Totals: 2 passed, 1 failed` (`logs/test-17-red-proof.log`);
  restored: EXIT=0, `Totals: 3 passed, 0 failed` (`logs/test-18-green-proof.log`). Both runs
  are from the same final test file and the final `fix.patch`.
- **Deterministic.** The contention case is injected with the queue's existing
  `pushWithTestHook` competitor (exactly `MaxPushAttempts` attempts, checked with
  `QCOMPARE`), the exhaustion/order cases use explicit event offsets, and the allocation
  case uses `AllocationProbe`. No thread, sleep, timer or wall-clock value is used. The
  fixture discovery is the test's own `initTestCase` (`LV2_PATH` + plugin dir).
- **Cause, complete and callers consistent.** All producer paths funnel through
  `Lv2ControlBase::handleMidiInputEvent`; the single consumer is
  `copyModelsFromCore` on the audio thread (plus its construction-time call before playback
  starts). Both the push and the drain side were changed; no other user of the removed
  members exists (searched `m_ringLock`, `m_midiInputBuf`, `m_midiInputReader`,
  `MidiInputEvent`).
- **No collateral damage.** No save/load format change: the queue is not serialised, and
  `Lv2Proc::saveState/restoreState` are untouched. Capacity is the same 1024; no new limit
  or refusal reaches any other caller. The only new refusal path is a dropped MIDI event
  under queue overload/contention, which the old code also did when the ringbuffer was full
  (it logged then; it now counts through `MidiHandoffQueue::dropped()` and stays silent on
  the realtime caller).
- **Realtime and undo.** `handleMidiInputEvent` now does one bounded `push()` (CAS + plain
  stores, no allocation, lock, wait, log or I/O); `copyModelsFromCore` keeps its existing
  fixed-capacity drain. No new command and no undo step, so `ControlCommandsSnapshot` is
  unchanged.
- **Verdict is FIXED**, so the ALREADY-FIXED/FALSE-POSITIVE evidence item is not
  applicable.
- **Sibling commands (ASK).** This fix changes no command file and no command id: the
  commit's own file list is `include/Lv2Proc.h`, `include/MidiHandoffQueue.h`,
  `src/core/lv2/Lv2Proc.cpp`, `tests/src/core/Lv2MidiHandoffTest.cpp`,
  `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt` and
  this report (`git show --stat d6c7ab7234`). The sibling ids the checker lists (`app.*`,
  `arrangement.*`) come from the lane's earlier commits in `base..HEAD`, not from this
  fix; none of them reaches `Lv2Proc`'s MIDI handoff, so none is affected.

## Other findings

- `CarlaMidiHandoffTest::concurrentMidiStressIsOrderedAndAllocationFree()` is timing
  dependent: it failed once in a parallel ctest run (`logs/test-5-family.log`) with
  "processing did not overlap MIDI producer activity" and passes standalone and in the
  final family runs. Not touched here.
- An explicit `cmake -S .. -B .` reconfigure is refused by the lane guard
  (`configure-1.log`, EXIT=97); the new test target was created by the build system's own
  regeneration during `cmake --build . --target Lv2StateTest` (`logs/build-7-regen.log`,
  EXIT=0), as the guard intends.
