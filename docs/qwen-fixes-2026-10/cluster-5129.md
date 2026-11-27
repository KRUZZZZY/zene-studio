ALREADY-FIXED

# Cluster 5129 - ALSA output events can wait 250 ms before processing

Verdict: **ALREADY-FIXED** on `bugs/qwen-5129` (worktree `kads-5129`). The finding is stale: the
wake path it asks for is in the code, wired into both audio-path producers, with a deterministic
regression test. Nothing was changed in the product this session; the only commit is this report.

## Evidence (read this session)

The finding's mechanism was real before 2026-10-08: `run()` drained the output queue and then
blocked in `poll()` with `EventPollTimeOut = 250` (`src/core/midi/MidiAlsaSeq.cpp:46`), and nothing
in `processOutEvent()` woke it. The branch now wakes the poll:

| What | Where |
|---|---|
| Both audio-path producers funnel into `enqueueOutEvent()`: `processOutEvent()` calls it at `src/core/midi/MidiAlsaSeq.cpp:342`, `processOutEventAt()` at `:361` | `src/core/midi/MidiAlsaSeq.cpp:340-365` |
| The enqueue uses `MidiOutQueue::pushAndRequestWake()` and, on the first push after a drain, writes one byte to the wake pipe | `src/core/midi/MidiAlsaSeq.cpp:374-380` |
| One wake per drain, lock-free and allocation-free: a `std::atomic<bool>` exchange beside the existing SPSC push | `include/MidiOutQueue.h:64-69` (`pushAndRequestWake`), `:72-75` (`acknowledgeWake`) |
| The worker acknowledges **before** draining, which closes the lost-wake race: a push after the acknowledge requests a wake, and a push before it is found by the drain that follows | `src/core/midi/MidiAlsaSeq.cpp:883-889` |
| `poll(..., EventPollTimeOut)` keeps the 250 ms timeout as the idle fallback; a queued event makes it return at once | `src/core/midi/MidiAlsaSeq.cpp:899` |
| The worker consumes the wake byte(s) after `poll()` reports the pipe readable | `src/core/midi/MidiAlsaSeq.cpp:917-923` |
| The pipe write end is `O_NONBLOCK`, so the audio thread never blocks on a full pipe | `src/core/midi/MidiAlsaSeq.cpp:215-221` |
| `stopWorker()` writes the same wake, so shutdown also skips the timeout | `src/core/midi/MidiAlsaSeq.cpp:283-291` |
| The off-audio producer (`processNonAudioOutEvent`, the MIDI clock's capture sender) writes its own wake byte; it runs off the audio thread | `src/core/midi/MidiAlsaSeq.cpp:384-397` |

Provenance: `cc389e112d "fix(midi): the ALSA output thread is woken when audio queues output
(cluster 5129)"` (2026-10-08), with follow-ups `dce5e1ef0b` (guard the POSIX headers behind ALSA)
and `74fe451db8` (assert the pipe write). `git merge-base --is-ancestor cc389e112d HEAD` printed
`ancestor_exit=0`, so the fix is part of this branch's history, not just the working tree.
`tests/upstream-modifications.txt:2195` records the inherited-file change, and
`docs/050-features/Q050-028.md:4-6` records that the wake path already existed before Q050-028
(which added the end-to-end proof).

Realtime check (reasoned here; TSan does not run on this box): the added audio-path work is one
compare/exchange on a `std::atomic<bool>` and, at most once per drain, one non-blocking `write(2)`
of one byte. No allocation, no mutex, no logging, no wait; the allocation probe in
`FirstPushAfterAnAcknowledgedDrainRequestsOneWake` (`tests/src/core/MidiOutQueueTest.cpp:377-384`)
asserts zero allocations over the queue path.

## Regression test

`tests/src/core/MidiOutQueueTest.cpp:412` `processOutEventRequestsTheSequencerWake()`:

- builds `MidiAlsaSeq` with the test-only `PipeOnlyForTesting` constructor (no worker thread, no
  ALSA device), calls the real `processOutEvent()` at `:419`, and requires one byte to be readable
  from `wakeReadFdForTesting()` at `:422-423`;
- deterministic: no sleep, no wall clock. The testing constructor makes both pipe fds non-blocking
  (`src/core/midi/MidiAlsaSeq.cpp:235-239`), so on an empty pipe the read returns `-1` (EAGAIN)
  immediately.

Supporting tests: `FirstPushAfterAnAcknowledgedDrainRequestsOneWake` (`:372`, one wake per drain,
allocation-free), `FullQueue_RequestsNoWake` (`:395`, a refused push requests nothing), and the
end-to-end loopback `queuedEventReachesAnAlsaSequencerClientPromptly` (`:481`), which QSKIPs in
this environment because `/dev/snd/seq` is absent.

## Commands and exit codes (all run this session)

Green baseline, before touching anything:

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5129
cmake --build build --target MidiOutQueueTest -j4 > logs/build-1.log 2>&1              # EXIT=0
cd build/tests && ctest -R '^MidiOutQueueTest$' --output-on-failure > logs/test-1.log 2>&1
                                                                                      # EXIT=0 (1/1 Passed)
./MidiOutQueueTest processOutEventRequestsTheSequencerWake \
    FirstPushAfterAnAcknowledgedDrainRequestsOneWake FullQueue_RequestsNoWake \
    queuedEventReachesAnAlsaSequencerClientPromptly > logs/test-2.log 2>&1            # EXIT=0
# 5 passed, 0 failed, 1 skipped; the wake test PASSED, the skip is the loopback test (no /dev/snd/seq)
```

RED proof - the fix temporarily removed from `enqueueOutEvent()` (pre-fix behaviour: enqueue and do
not wake), so the existing test is shown to catch the defect:

```
git diff -- src/core/midi/MidiAlsaSeq.cpp > logs/red-mutation.patch                     # EXIT=0
cmake --build build --target MidiOutQueueTest -j4 > logs/build-2.log 2>&1              # EXIT=0
./MidiOutQueueTest processOutEventRequestsTheSequencerWake > logs/test-red-direct.log 2>&1
                                                                                      # EXIT=1
# FAIL! processOutEventRequestsTheSequencerWake(): Actual (::read(readFd,&wake,1)): -1
#       Expected (ssize_t{1}): 1  at MidiOutQueueTest.cpp(422)
ctest -R '^MidiOutQueueTest$' --output-on-failure > logs/test-red.log 2>&1            # EXIT=8
# 0% tests passed, 1 tests failed out of 1; Totals: 10 passed, 1 failed, 1 skipped
```

GREEN proof - the mutation reversed (`git apply -R`), rebuilt, rerun:

```
git apply -R logs/red-mutation.patch                                                  # EXIT=0
git diff --stat && git status --porcelain                                             # both empty
cmake --build build --target MidiOutQueueTest zene -j4 > logs/build-3.log 2>&1        # EXIT=0
ctest -R '^MidiOutQueueTest$' --output-on-failure > logs/test-3.log 2>&1              # EXIT=0
./MidiOutQueueTest processOutEventRequestsTheSequencerWake \
    FirstPushAfterAnAcknowledgedDrainRequestsOneWake FullQueue_RequestsNoWake \
    > logs/test-4.log 2>&1                                                            # EXIT=0 (5 passed)
```

Gates and completion check:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1
                                                                                      # EXIT=0 (all rows PASS)
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 5129
```

The log files live in `/home/kruzzzzy/.config/kads/zene/state/runs/5129.r1/logs/`.

## Test matrix

| Path | How it is covered |
|---|---|
| Normal: an event queued while the worker sleeps in `poll()` | `processOutEventRequestsTheSequencerWake` (seam: the pipe is readable after `processOutEvent()`), plus `queuedEventReachesAnAlsaSequencerClientPromptly` on a host with a sequencer |
| One wake per drain, not per event | `FirstPushAfterAnAcknowledgedDrainRequestsOneWake` |
| Failure: queue full | `FullQueue_RequestsNoWake` (no wake for nothing new) and `FullQueue_CountsOverflowWithoutDroppingSilently` |
| Exhaustion: capacity | `FullQueue_CountsOverflowWithoutDroppingSilently`, `AudioEnqueue_IsAllocationFreeAndLockFree` |
| Reconfiguration/shutdown | `stopWorker()` wakes the poll (`MidiAlsaSeq.cpp:283-291`); `AlsaRaw_ClosesHandlesAfterWorkerExitsOnPollError` covers the sibling client's poll-error path |
| Audio-thread constraint | one atomic exchange plus at most one non-blocking byte per drain; the allocation probe asserts 0 allocations; `processOutEventDoesNotTakeTheSequencerMutex` proves the sequencer mutex is not taken |
| Platform | the pipe and `fcntl` code sits inside `#ifdef LMMS_HAVE_ALSA` (`MidiAlsaSeq.cpp:40-45`); `dce5e1ef0b` guarded the POSIX headers, so non-ALSA builds compile none of it |

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Red: `./MidiOutQueueTest processOutEventRequestsTheSequencerWake` EXIT=1 (`Actual -1`,
  `Expected 1`), ctest family EXIT=8. Green: ctest EXIT=0 after `git apply -R` and a rebuild.
- **The test is deterministic: no wall-clock timing, sleeps or races.** It uses the
  `PipeOnlyForTesting` seam (a pipe, no worker) and a non-blocking read of the wake byte; nothing is
  timed. The timed loopback test is separate and QSKIPs here (`/dev/snd/seq` absent, logged in
  `test-2.log`).
- **The fix is at the cause and complete: every caller and sibling checked.** Both audio-path
  entries (`processOutEvent`, `processOutEventAt`) go through `enqueueOutEvent`, which wakes; the
  off-audio producer wakes its own queue; `stopWorker` wakes; the worker acknowledges before
  draining, so no push can miss its wake; a full queue refuses and requests nothing. The mutation
  proof shows the seam test catches exactly the missing wake.
- **No collateral damage.** This session changed no product file. The committed fix leaves the
  queue's data format, capacity and overflow accounting unchanged (no save/load or project data is
  involved), and it adds no limit or check another caller could exceed. Its one side effect, a pipe
  byte, is idempotent: a full pipe already holds a pending wake.
- **Verdict evidence (ALREADY-FIXED).** `src/core/midi/MidiAlsaSeq.cpp:374-380` (wake on enqueue),
  `:883-889` (acknowledge before drain), `:899`, `:917-923` (poll and wake consumption),
  `include/MidiOutQueue.h:64-75`, commit `cc389e112d` (`git merge-base --is-ancestor` EXIT=0),
  `tests/upstream-modifications.txt:2195`, `docs/050-features/Q050-028.md:4-6`.

## Other findings

- `tests/upstream-modifications.txt:2195` names only the queue-level tests as holding the change,
  while the test that exercises `processOutEvent()` itself is
  `processOutEventRequestsTheSequencerWake` (`tests/src/core/MidiOutQueueTest.cpp:412`). The
  registration entry is incomplete but not wrong; left untouched to stay in scope.
- The earlier attempt on this finding (SKIPPED-SCOPE) reasoned that the fix needed a realtime-path
  design change on a driver that could not be exercised headlessly. The wake path that landed uses
  the existing pipe, and the `PipeOnlyForTesting` seam plus a non-blocking read makes it testable
  without a device; the soundness argument for the race is in the code ordering above.
