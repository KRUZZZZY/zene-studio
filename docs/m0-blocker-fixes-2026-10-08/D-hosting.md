# M0 blocker batch D: plug-in hosting realtime fixes (2026-10-08)

Branch `bugs/m0-blk-hosting`, based on train 2's `28cc3a0b5`. Fixes by a Claude Haiku 5.5 agent in two rounds;
round 1 was reviewed independently (REWORK 3 / ACCEPT-NOTE 2, findings B1-B9) and round 2 answers that review.
This file is written by the orchestrator from the agent's two reports (its own instructions forbade report files).
Exit codes are the agent's, from targeted builds and `ctest -R` in `build/tests`; the full suite has not run on this
branch.

## Round 1

| Cluster | Verdict | Commit | Test | Red | Green |
|---|---|---|---|---|---|
| 3417 client work inside the period | FIXED | 3152a763e | RemotePluginClientE2ETest::channelCountRequestDuringPeriodIsAppliedOnTheMainThread | count applied inside the period | pass |
| 3418 unbounded processing wait | PARTIAL: bounded on the Linux socket path (round 1-2); the shared-memory path was bounded in round 3 but is compiled, not run (see round 3) | 3152a763e | stalledClientCannotHoldTheAudioPeriod | 30 s wait | bounded < 500 ms |
| new-p8-197 deferred apply | FIXED | 3152a763e, 7de396b3a | as 3417 | - | pass |
| 3433 / 3434 VST client MIDI queue | FIXED in the client; runtime under a real VST unproven | ca5b95981 | VstMidiEventBatchTest (cap + drop count; sortAllocatesNothing with a control) | no in-process product red | pass |
| 5451 remote MIDI under the lock | FIXED | 72c405a50, f3953cc8e | midiEventDoesNotWaitForTheCommunicationLock | 300 ms wait | pass |
| 5508 Carla, 908 ZynAddSubFx MIDI | FIXED, compile-only | f3953cc8e | (Zyn out-of-process suites pass but send no notes) | - | - |

## Round 2 (review findings)

| Finding | Verdict | Commit | Test | Red on round 1 | Green |
|---|---|---|---|---|---|
| B1 a late client writes the planes the instrument reads | FIXED: the instrument reads a host-private block, copied only after a completed reply; every silent exit zeroes it | ec235fd96 | lateClientCannotReachTheInstrumentOutput | FAIL | PASS |
| B2 buffer resize vs render | FIXED: every resize and ports-view update runs under RemotePluginBufferChangeGuard, render holds the engine mutex for the period | ec235fd96 | bufferResizeWaitsForRenderingToFinish | FAIL | PASS |
| B3 the bound covered only the first byte | PARTIAL: socket path peeks and reads only complete messages; shared-memory path open at round 2 (round 3 bounds it, not run); a message over the peek window fails the plugin (round 3 N6: now 1 MB) | 008dc0bd9 | clientStoppingMidMessageCannotHoldThePeriod | FAIL | PASS |
| B4 silent MIDI loss, dropped note-off, order | PARTIAL: loss fixed; the cut was ordered after events queued after the refusal (round 2 N2, fixed in round 3); counters are not in the control surface | 4350c56cb | refusedNoteOffBecomesAllNotesOffInOrder; MidiHandoffQueueTest::refusedEventsLeaveTheirChannelRecorded | FAIL | PASS |
| B5 Windows shm allocation, blocking socket sends, budget | PARTIAL: shm frames in stack buffers (round 2); socket sends try-lock + SIOCOUTQ room + MSG_DONTWAIT; wait budget ends with the render period. Round 3 makes the shm send exact and timed and fixes the no-room/short-write handling off Linux (N4), not run | 20db042a0 | no-room branch justified, untested; shm branch syntax-checked only | - | - |
| B6 late-reply drain untested | ADDED | 4350c56cb | lateReplyIsDrainedAndNeverUsedForTheNextPeriod | passes on round 1 (guard) | PASS |
| B8 no allocation probe | ADDED | 4350c56cb | realtimePathsAllocateNothingInSteadyState (process, processMidiEvent, idle applyDeferredClientRequests: 0 allocations) | passes on round 1 (guard) | PASS |

Round-2 suites at the tip: RemotePluginClientE2ETest (15), MidiHandoffQueueTest (6), VstMidiEventBatchTest,
AllocationProbeTest, RemotePluginAudioPortsTest, AudioPortsTest, AudioPortsModelTest, ZynSeparateProcessTest,
OutOfProcessHostTest, OutOfProcessHostClientLoopTest. Round-2 gates complexity, file-length, fork-sources and
no-upstream-regression exit 0.

## Round 3 (re-review N1-N9)

Re-review verdicts: round 1 B1/B2/B6/B8 accepted; B3, B4, B5 and the 3418 claim overstated (now PARTIAL above).

| Finding | Verdict | Commit | Test | Red on round 2 | Green now |
|---|---|---|---|---|---|
| N1 (blocker) isUIVisible and siblings read the reply outside the communication lock | FIXED: RemotePlugin::waitForMessage holds the lock across the wait; owed replies taken by a main-thread wait are accounted for | f7114c841 | uiQueryDuringRenderingDoesNotBlockThePeriod (20 queries while the render thread runs periods) | FAIL (a reply taken by the render thread) | PASS |
| Windows shm wait unbounded (blocker) | PARTIAL: timed token and data lock on the audio thread, exact FIFO room check, no second token. POSIX backend unit-tested (SystemSemaphoreTest). Shm transport compiled with -DSYNC_WITH_SHM_FIFO (syntax only); the Win32 backend is not compiled here; no end-to-end shm run | f7114c841 | SystemSemaphoreTest (timeout without count; take at once) | not run on Windows | PASS (POSIX) |
| N2 cut ordered after later events | FIXED: cut recorded with the queue position at the refusal; drainMidi sends it before the first later event | f7114c841 | noteOnAfterARefusedNoteOffSounds; MidiHandoffQueueTest::drainSendsTheCutAtTheRefusalPoint | not reproduced red in the E2E (round 2 also passes that sequence) | PASS |
| N3 drain not bounded per period | FIXED: 128 MIDI events per period; the rest stay queued | f7114c841 | midiSentPerPeriodIsCapped; MidiHandoffQueueTest::drainTakesAtMostTheCapPerCall | FAIL (4 outputs, not 3) | PASS |
| N4 no room check off Linux; a short write killed the plugin | PARTIAL: short write keeps the tail and it goes out first; flushed before each request and while waiting. Not forced in a test (Linux has room) | f7114c841 | none forced | - | compiled |
| N6 a complete message above 16 KB killed the plugin | FIXED up to 1 MB: window is 1 MB, allocated in the constructor. Messages above 1 MB are still refused | f7114c841 | messageLargerThanThePeekWindowDoesNotFailThePlugin (300 KB) | FAIL (plugin failed) | PASS |
| N7 wait consumed the whole remaining period | FIXED: ends an eighth of a period early; counts only on the rendering thread | f7114c841 | suites | - | PASS |
| N8 stale comments and manifest text | FIXED | f7114c841 | - | - | - |
| N5 receiveMessage allocates for messages with arguments and takes a blocking mutex | TRACKED, not fixed | - | - | - | - |
| Vestige and Carla runtime | NOT RUN: no Carla host or Carla library on this box; the installed VST2 set (lsp-plugins) has effects only, no note output, so no Vestige note can be run; compile-only | f7114c841 | - | - | compiled (vestige, carlabase) |

Round-3 suites at the tip: RemotePluginClientE2ETest (19), MidiHandoffQueueTest (9), SystemSemaphoreTest (4),
VstMidiEventBatchTest, AllocationProbeTest, RemotePluginAudioPortsTest, AudioPortsTest, AudioPortsModelTest,
ZynSeparateProcessTest, OutOfProcessHostTest, OutOfProcessHostClientLoopTest: all pass. Gates complexity,
file-length, fork-sources and no-upstream-regression exit 0.

## Round 4 (re-review round 3)

Re-review verdicts: N1, N2, N3, N6 and N7 accepted with notes; the Windows shm wait and the no-buffer branch were
blockers. The Windows bounded wait is now bounded in design on the audio thread (both blockers closed in code). It is
still PARTIAL: compiled for Linux only (shm syntax check), the POSIX semaphore backend unit-tested, the Win32 backend
and the shm FIFO path never compiled or run here. Hosted Windows CI is the first run.

| Item | Verdict | Commit | Test | Red on round 3 | Green now |
|---|---|---|---|---|---|
| 1 blocker: the no-buffer branch pumped messages on the audio thread | FIXED: the branch returns silence and reads nothing; the main thread's waits read those messages | 516f3f242 | periodWithoutBufferCannotHoldTheAudioThread (fake halfhandshake) | FAIL (the period waited for the client) | PASS |
| 2 blocker: shm depth counter ridden by a thread that does not hold the lock | FIXED: the data lock has an owner key; only the owner re-enters without waiting; a timed try never rides another thread's hold. Audit: every host-side shm receive is under the communication lock (waitForMessage, waitForReplyWithin, the audio collect); main-thread sends serialise on the data lock and the audio thread only try-locks and skips | 516f3f242 | none deterministic (ownership is enforced by the lock itself) | not reproduced | compiled |
| 3 POSIX tryAcquire used wall-clock time | FIXED: steady-clock deadline with sem_trywait polling (no wall-clock deadline) | 516f3f242 | SystemSemaphoreTest (4 cases) | not reproduced (needs a clock step) | PASS |
| 4 main-thread silent gaps: saveSettings and the other waits | PARTIAL: saveSettings reads the cached visibility; isUIVisible and updateSampleRate wait at most 100 ms and leave a late reply in the connection (processMessage caches visibility and ignores other replies). The VST program, parameter and settings queries stay unbounded (they carry data the caller needs) | 516f3f242 | hungQueryCannotFreezeTheMainThread (fake deafquery) | FAIL (froze more than 1 s) | PASS |
| 5 shm read() spin under the data lock | FIXED: about 5 ms of spins, then the FIFO is given up (the writer posts only whole messages) | 516f3f242 | not tested (the invariant makes it unreachable without a broken peer) | - | compiled |
| 5 socket partial-message spin on the audio thread | FIXED: 50 us sleeps while bytes arrive, poll only when nothing is readable | 516f3f242 | not measured | - | compiled |
| 5 N6 test name and size | FIXED: 900 KB message near the 1 MB window (was 300 KB, misnamed). A frame that has started on the socket is finished on the main thread within the query bound per piece | 516f3f242 | messageNearTheOneMegabyteWindowDoesNotFailThePlugin | FAIL before the started-frame reader (the 900 KB message could not complete) | PASS |
| N2 remainder: stamp read after the refusal | FIXED: the refusal position comes from the failing push | 516f3f242 | existing ordering tests | - | PASS |

Suites on the final tree (all pass): RemotePluginClientE2ETest (21), MidiHandoffQueueTest (9), SystemSemaphoreTest (4),
VstMidiEventBatchTest, AllocationProbeTest, RemotePluginAudioPortsTest, AudioPortsTest, AudioPortsModelTest,
ZynSeparateProcessTest, OutOfProcessHostTest, OutOfProcessHostClientLoopTest. Client executables RemoteZynAddSubFx and
NativeLinuxRemoteVstPlugin64 build. Gates complexity, file-length, fork-sources and no-upstream-regression exit 0.
Syntax checks pass with and without -DSYNC_WITH_SHM_FIFO for RemotePluginBase.cpp, RemotePlugin.cpp and SystemSemaphore.cpp.

Not run: the shm FIFO end to end; the Win32 backend; Vestige and Carla note paths (no host or note-capable VST2 here);
the VST program/parameter/settings waits stay unbounded.

## Residuals (open, tracked)

- Windows shm transport: bounded in design (rounds 3-4), but only syntax-checked (-DSYNC_WITH_SHM_FIFO) and the POSIX
  semaphore backend unit-tested; the shm FIFO path and the Win32 backend are not run here. Hosted Windows CI is the first run.
- Main-thread VST program, parameter and settings queries wait without a bound (round 4 item 4).
- `receiveMessage` still allocates for messages WITH arguments on the audio thread; the no-allocation proof covers
  the steady-state messages without arguments.
- Main-thread waits (`updateSampleRate`, `waitForInitDone`, `isUIVisible`) hold the communication lock without a
  bound; the audio thread skips periods (silence) meanwhile.
- Refusal counters are not in the control surface; the instrument-level queues (Vestige, Zyn, Carla) have no
  counter accessor or warning.
- Vestige and Carla: compile-only at runtime (see round 3). ZynAddSubFx's out-of-process suites pass but send no notes.
- Messages above 1 MB on the audio thread still fail the plugin (N6 bound).
- Main-thread waits now hold the communication lock for their whole wait (N1); the audio thread skips periods
  meanwhile, so a slow client during a main-thread wait is a silence gap.
- The resize guard is verified for the render path only; offline export is unchecked.
- The socket room check uses Linux SIOCOUTQ/SO_SNDBUF; elsewhere the short-write tail (round 3 N4) is the only guard;
  not run off Linux.
