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
| 3418 unbounded processing wait | FIXED (completed in round 2, B3/B5) | 3152a763e | stalledClientCannotHoldTheAudioPeriod | 30 s wait | bounded < 500 ms |
| new-p8-197 deferred apply | FIXED | 3152a763e, 7de396b3a | as 3417 | - | pass |
| 3433 / 3434 VST client MIDI queue | FIXED in the client; runtime under a real VST unproven | ca5b95981 | VstMidiEventBatchTest (cap + drop count; sortAllocatesNothing with a control) | no in-process product red | pass |
| 5451 remote MIDI under the lock | FIXED | 72c405a50, f3953cc8e | midiEventDoesNotWaitForTheCommunicationLock | 300 ms wait | pass |
| 5508 Carla, 908 ZynAddSubFx MIDI | FIXED, compile-only | f3953cc8e | (Zyn out-of-process suites pass but send no notes) | - | - |

## Round 2 (review findings)

| Finding | Verdict | Commit | Test | Red on round 1 | Green |
|---|---|---|---|---|---|
| B1 a late client writes the planes the instrument reads | FIXED: the instrument reads a host-private block, copied only after a completed reply; every silent exit zeroes it | ec235fd96 | lateClientCannotReachTheInstrumentOutput | FAIL | PASS |
| B2 buffer resize vs render | FIXED: every resize and ports-view update runs under RemotePluginBufferChangeGuard, render holds the engine mutex for the period | ec235fd96 | bufferResizeWaitsForRenderingToFinish | FAIL | PASS |
| B3 the bound covered only the first byte | FIXED: the audio thread peeks (MSG_PEEK, MSG_DONTWAIT) and reads only complete messages | 008dc0bd9 | clientStoppingMidMessageCannotHoldThePeriod | FAIL | PASS |
| B4 silent MIDI loss, dropped note-off, order | FIXED for loss and order: a refused event records its channel and the consumer sends all-notes-off for it after the queued events; Vestige noteOffAll goes through the queue; refusals counted, reported on the main thread | 4350c56cb | refusedNoteOffBecomesAllNotesOffInOrder; MidiHandoffQueueTest::refusedEventsLeaveTheirChannelRecorded | FAIL | PASS |
| B5 Windows shm allocation, blocking socket sends, budget | FIXED: shm frames in stack buffers; socket sends try-lock + SIOCOUTQ room + MSG_DONTWAIT (no room = period skipped and counted, events stay queued); wait budget ends with the render period (RenderPeriodClock) | 20db042a0 | no-room branch justified, untested; shm branch syntax-checked only | - | - |
| B6 late-reply drain untested | ADDED | 4350c56cb | lateReplyIsDrainedAndNeverUsedForTheNextPeriod | passes on round 1 (guard) | PASS |
| B8 no allocation probe | ADDED | 4350c56cb | realtimePathsAllocateNothingInSteadyState (process, processMidiEvent, idle applyDeferredClientRequests: 0 allocations) | passes on round 1 (guard) | PASS |

Suites passing at the tip: RemotePluginClientE2ETest (15), MidiHandoffQueueTest (6), VstMidiEventBatchTest,
AllocationProbeTest, RemotePluginAudioPortsTest, AudioPortsTest, AudioPortsModelTest, ZynSeparateProcessTest,
OutOfProcessHostTest, OutOfProcessHostClientLoopTest. Gates complexity, file-length, fork-sources and
no-upstream-regression exit 0.

## Residuals (open, tracked)

- Windows shm transport: no peek or timed read; its semaphores still block. Not run on this box.
- `receiveMessage` still allocates for messages WITH arguments on the audio thread; the no-allocation proof covers
  the steady-state messages without arguments.
- Main-thread waits (`updateSampleRate`, `waitForInitDone`, `isUIVisible`) hold the communication lock without a
  bound; the audio thread skips periods (silence) meanwhile.
- Refusal counters are not in the control surface; the instrument-level queues (Vestige, Zyn, Carla) have no
  counter accessor or warning.
- A refused channel's cut-off arrives at the next processed period. Vestige and Carla are compile-only at runtime.
- The resize guard is verified for the render path only; offline export is unchecked.
- The socket room check uses Linux SIOCOUTQ/SO_SNDBUF; elsewhere MSG_DONTWAIT alone, and a partial write marks the
  stream broken.
