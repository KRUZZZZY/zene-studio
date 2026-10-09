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
| 4 main-thread silent gaps: saveSettings and the other waits | PARTIAL: saveSettings reads the cached visibility; isUIVisible and updateSampleRate wait at most 100 ms. Round 4 claimed a late reply was discarded; it was not: the next same-id query took it as its answer (corrected and fixed in round 5). The VST program/parameter/settings queries stay unbounded | 516f3f242 | hungQueryCannotFreezeTheMainThread | FAIL (froze > 1 s) | PASS |
| 5 shm read() spin under the data lock | Round-4 spin, since replaced in round 5 by a fail-fast read (the spin could never succeed) | 516f3f242 | not tested | - | compiled |
| 5 socket partial-message spin on the audio thread | FIXED: 50 us sleeps while bytes arrive, poll only when nothing is readable; the audio thread still cannot finish a frame larger than the socket buffer (only the main thread does) | 516f3f242 | not measured | - | compiled |
| 5 N6 test name and size | FIXED: 900 KB message near the 1 MB window (was 300 KB, misnamed). A frame that has started on the socket is finished on the main thread within the query bound per piece | 516f3f242 | messageNearTheOneMegabyteWindowDoesNotFailThePlugin | FAIL before the started-frame reader (the 900 KB message could not complete) | PASS |
| N2 remainder: stamp read after the refusal | FIXED: the refusal position comes from the failing push | 516f3f242 | existing ordering tests | - | PASS |

Suites on the final tree (all pass): RemotePluginClientE2ETest (21), MidiHandoffQueueTest (9), SystemSemaphoreTest (4),
VstMidiEventBatchTest, AllocationProbeTest, RemotePluginAudioPortsTest, AudioPortsTest, AudioPortsModelTest,
ZynSeparateProcessTest, OutOfProcessHostTest, OutOfProcessHostClientLoopTest. Client executables RemoteZynAddSubFx and
NativeLinuxRemoteVstPlugin64 build. Gates complexity, file-length, fork-sources and no-upstream-regression exit 0.
Syntax checks pass with and without -DSYNC_WITH_SHM_FIFO for RemotePluginBase.cpp, RemotePlugin.cpp and SystemSemaphore.cpp.

Not run: the shm FIFO end to end; the Win32 backend; Vestige and Carla note paths (no host or note-capable VST2 here);
the VST program/parameter/settings waits stay unbounded.

## Round 5 (re-review round 4)

Re-review verdicts: the round-3 blockers stay closed. Four items were REWORK: the late-reply mis-assignment, the shm
read spin, the silent lost connection, and the test's global environment variable. All four are fixed here. Two
round-4 claims were false and are corrected: a late reply was NOT discarded (it answered the next same-id query), and
a lost connection was NOT a clean failure (the instrument went silent with no report). Windows stays PARTIAL.

| Item | Verdict | Commit | Test | Red on round 4 | Green now |
|---|---|---|---|---|---|
| 1 late replies taken as the next same-id answer | FIXED, with a caveat (corrected in round 6): per-id count of abandoned queries. A reply that never arrives used to leave the count forever, so every later query timed out; the count now expires after 2 s; each later same-id reply is consumed as abandoned, once; processMessage consumes one found by any other wait; the deadline is checked after each non-matching message | 18ae528ff | lateQueryReplyDoesNotAnswerTheNextQuery (fake latequery) | FAIL (the late answer returned) | PASS |
| 2 shm read() spin under the data lock | FIXED: a short read fails the FIFO at once; no spin | 18ae528ff | not forced (unreachable without a broken peer) | - | compiled |
| 3 lost connection silent | FIXED: every audio-thread loss invalidates the connection; process() flags it; applyDeferred logs (qCritical) and sets m_failed. A loss found during a main-thread wait is reported by the next period | 18ae528ff | lostConnectionIsReportedOnTheMainThread (fake garbage: negative argument count) | FAIL (plugin not failed) | PASS |
| 4 test leaves LMMS_PLUGIN_DIR set | FIXED: ScopedPluginDir restores the previous value (or unsets it) when the test ends | 18ae528ff | no-buffer and pre-buffer tests | - | PASS |
| 5 pre-buffer handshake needs a main-thread wait | FIXED: the deferred timer reads complete messages while there is no shared block and activates the ports on IdInitDone | 18ae528ff | preBufferHandshakeCompletesWithoutAMainThreadWait (fake halfhandshake) | FAIL (never completed) | PASS |
| 5 m_uiVisible data race | FIXED: std::atomic<int> | 18ae528ff | - | - | compiled |
| 5 refresh cached visibility on the plugin's own UI close | NOT POSSIBLE on the current protocol: the client does not report its own editor closing; its visibility messages only answer host requests. The cache can be stale after a close inside the plugin's UI | - | - | - | - |
| 5 started-frame total deadline | FIXED: StartedFrameTimeout (500 ms) on top of the per-piece bound | 18ae528ff | messageNearTheOneMegabyteWindowDoesNotFailThePlugin | - | PASS |
| 5 saveSettings before any query | Behaviour change, stated: the cache is -1 until a query, show or hide, so a project saved before any of those writes no guivisible attribute | 18ae528ff | - | - | - |

Suites on the final tree (all pass): RemotePluginClientE2ETest (24), MidiHandoffQueueTest (9), SystemSemaphoreTest (4),
VstMidiEventBatchTest, AllocationProbeTest, RemotePluginAudioPortsTest, AudioPortsTest, AudioPortsModelTest,
ZynSeparateProcessTest, OutOfProcessHostTest, OutOfProcessHostClientLoopTest. Client executables RemoteZynAddSubFx and
NativeLinuxRemoteVstPlugin64 build. Gates complexity, file-length, fork-sources and no-upstream-regression exit 0.
Syntax checks pass with and without -DSYNC_WITH_SHM_FIFO for RemotePluginBase.cpp, RemotePlugin.cpp and SystemSemaphore.cpp.

Windows: still PARTIAL. Compiled for Linux only (shm syntax check; POSIX semaphore unit test). The Win32 backend
(WaitForSingleObject with a timeout) is not compiled here (no mingw), and the shm FIFO path is not run end to end. A
Win32 compile break would surface on the hosted Windows job after merge; the owner decides whether to run it first.

## Round 6 (re-review round 5)

Re-review verdict: no open audio-thread blocker on Linux. Three must-fix items, two follow-ups, and the Windows list.
Round-5 row 1 is corrected above: a reply that never arrives poisoned its id for the life of the connection (the count
never came down). Fixed here by expiry.

| Item | Verdict | Commit | Test | Red on round 5 | Green now |
|---|---|---|---|---|---|
| 1 a reply that never arrives poisons its id | FIXED: the abandoned count expires StaleQueryExpiry (2 s) after its first abandonment. Caveat: a reply later than 2 s is taken as the answer to the next query of its id | 380e23fa8 | neverArrivingReplyExpiresAndLaterQueriesWork (fake dropquery) | FAIL (every later query returned -1) | PASS |
| 2 m_failed written by the timer, read by the audio thread | FIXED: std::atomic<bool> | 380e23fa8 | - | - | compiled |
| 3 per-connection state across init() on a reused object | FIXED: init()'s reuse branch clears the abandoned counts, the owed-reply count, the cached visibility, the pending MIDI frame and the lost-connection flag. Code-verified; no test (the reuse path is only reached after a failure) | 380e23fa8 | - | - | - |
| Pump against a flooding client | FIXED: at most 64 messages per tick | 380e23fa8 | - | - | compiled |
| Started-frame total bound 500 ms | CHANGED to 2 s (a loaded machine would otherwise kill a plugin over a large message) | 380e23fa8 | messageNearTheOneMegabyteWindowDoesNotFailThePlugin | - | PASS |

Suites on the final tree (all pass): RemotePluginClientE2ETest (25), MidiHandoffQueueTest (9), SystemSemaphoreTest (4),
VstMidiEventBatchTest, AllocationProbeTest, RemotePluginAudioPortsTest, AudioPortsTest, AudioPortsModelTest,
ZynSeparateProcessTest, OutOfProcessHostTest, OutOfProcessHostClientLoopTest. Client executables RemoteZynAddSubFx and
NativeLinuxRemoteVstPlugin64 build. Gates complexity, file-length, fork-sources and no-upstream-regression exit 0.
Syntax checks pass with and without -DSYNC_WITH_SHM_FIFO for RemotePluginBase.cpp and RemotePlugin.cpp.

### What hosted Windows CI must verify (none of this runs on Linux)

The hosting E2E tests use the POSIX fake client and QSKIP on Windows, so a green Windows job proves compilation only.
Verify in this order:
1. Compile the Win32 backend (SystemSemaphore.cpp: WaitForSingleObject with a timeout, DWORD cast), and the shm variant
   (RemotePluginBase.h: owner key via std::hash<std::thread::id>, tryLockFor, freeSpace; RemotePluginBase.cpp:
   readFrameLocked, receiveFrameBefore shm branch, sendFrameNonBlocking shm branch; RemotePlugin.cpp).
2. SystemSemaphoreTest on Windows: it is POSIX-only in practice; either run it on Win32 or record it as unrun.
3. A real shm run: a Windows client plus a host, a short session (activate, process periods, one isUIVisible query,
   one save), with the FIFO in both directions. Nothing in CI does this today.
4. Win32 tryAcquire timeout behaviour: a zero timeout returns at once; 100 ms returns after about 100 ms.
5. The shm owner key under MSVC: two threads never share a key.
6. The Windows main-thread waits (VST program and parameter queries) remain unbounded, as on Linux; a hung plugin still
   freezes the GUI on those actions. Known; owner to schedule.

Until items 1 to 3 have run, Windows must not be described as fixed. Whether to merge before the Windows job runs is the
owner's decision: a compile break would redden the hosted Windows job on the live line right after merge.

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
- Vestige and Carla: compile-only at runtime (see round 3).
- The plugin's own UI close is not reported by the client protocol: the cached visibility can be stale after it.
- A frame larger than the socket buffer is only completed by the main thread; during playback such a message leaves the
  plugin silent until the next main-thread wait.
- Main-thread VST program, parameter and settings queries wait without a bound (owner to schedule).
- A reply later than StaleQueryExpiry (2 s) after its query is taken as the answer to the next query of its id. ZynAddSubFx's out-of-process suites pass but send no notes.
- Messages above 1 MB on the audio thread still fail the plugin (N6 bound).
- Main-thread waits now hold the communication lock for their whole wait (N1); the audio thread skips periods
  meanwhile, so a slow client during a main-thread wait is a silence gap.
- The resize guard is verified for the render path only; offline export is unchecked.
- The socket room check uses Linux SIOCOUTQ/SO_SNDBUF; elsewhere the short-write tail (round 3 N4) is the only guard;
  not run off Linux.
