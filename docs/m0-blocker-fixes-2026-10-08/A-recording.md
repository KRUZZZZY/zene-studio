# Batch A-recording: clip recording on the audio thread (2026-10-08)

Branch `bugs/m0-blk-recording`, base `28cc3a0b5`.
Round 1: `e3083dce0` (fix), `30b0072ae` (report). Round 2: `ab938848f` (fix, tests, manifest, commands snapshot) and this report commit.

## Design (after round 2)

- `SampleClip` builds its takes on the main thread: a ready take and a standby take. `prepareRecordTake()` reads its slots under the change lock and builds outside it. `armRecordTake()` clears the clip's playing latch on the arm transition.
- `claimRecordHandle()` (audio thread) moves the standby into the ready slot and marks the take claimed. It allocates nothing.
- The handle destructor (audio thread) only requests the accumulator to finish and pushes the take onto a 256-slot MPSC queue. A full queue is counted (`queueOverflows`).
- `SampleRecordHandle::pumpTakes()` runs every 10 ms on the main thread. It reaps the queue, then tops up the takes of every armed clip (`watchArmedClip`).
- Reap: the take is finished (joins the drain thread), its drops are counted and padded, and the take is routed:
  - the transport still records (a loop wrap or jump) -> the take becomes a new take clip on a new take lane (`TakeLaneModel::addLane`) at the armed clip's position; the clip stays armed;
  - the transport stopped -> the first take of an arming is the clip's own content (as before); after an earlier pass it becomes a lane take; the clip is disarmed.
  - an unclaimed take is discarded and never touches the clip.
- Disarm (`setRecord(false)`) releases both takes; their drain threads stop at the next reap.
- Overflow: the accumulator records each lost run as a gap (position, length, 256-slot SPSC). The drain thread inserts that many silent frames at that position, so the take keeps the length and timing of the input. Lost frames are counted per take.
- Song: `record()` latches the request; the next Play takes it (`startSongPlayback`), `stop()` ends it.
- Track: a record-armed clip is scheduled only while the song records. The per-pass start cap defers the extra starts and counts them.

## Items (round 2)

| Item | Verdict | Fix commit | Test | Red (round-1 code) | Green |
|---|---|---|---|---|---|
| R3 release prepared takes on disarm and on clip deletion | FIXED | ab938848f | disarmReleasesThePreparedTakes; clipDeletedBeforeTheTakeIsInstalledIsSafe | compile error (recordTakesReady not in round-1 API); clip-deleted test passes on both | 0 |
| R4 read m_preparedRecord inside its guard | FIXED (code review, no test) | ab938848f | none | n/a | n/a |
| rows 4-6 (5312, 3510, new-p8-208): silent shortening | FIXED for padding and the agent surface; GUI widget NOT wired | ab938848f | droppedFramesArePaddedWithSilenceInTheTake | exit 6, take length mismatch | 0 |
| R2 queue-full count | FIXED (counted and surfaced) | ab938848f | queueFullIsCounted | compile error (queueOverflows not in round-1 API) | 0 |
| T2 behavioural claim test | DONE; not red on round 1 | ab938848f | claimedTakeRecordsItsInput (records 40 periods; sample >= 2560 frames) | passes on round 1 (round-1 records correctly); the compile red is gone | 0 |
| T3 tests: non-empty install, padding, unprepared take, clip deleted before reap, release on disarm | non-empty install and clip deleted: covered; padding: covered; release: covered (compile red on round 1); unprepared take end-to-end: NOT covered (only the claim returns null after both takes are used) | ab938848f | see above | see above | 0 |
| comment at SampleTrack.cpp (cap is starts per pass, not overlaps) and deferred counts | FIXED | ab938848f | clipStartsBeyondThePerPassCapAreDeferredAndCounted | compile error (deferredClipStarts not in round-1 API) | 0 |
| P2 Record then Play records nothing | FIXED | ab938848f | recordPressedThenPlayRecords | exit 6, isRecording() false after Play | 0 |
| P3 armed clip in span waits for the span to end (latch) | FIXED | ab938848f | clipArmedWhileRollingRecordsOnTheNextPass | exit 6, clip still armed, no take | 0 |
| P6 loop record keeps only the first pass | FIXED (design below) | ab938848f | loopWrapMakesEachPassATakeOnALane | exit 6, clip disarmed at the wrap | 0 |

## Design decisions the owner should check (P6)

- A pass is a take on a take lane, as docs/COMPING.md describes (a take lane is a track's clip with a lane tag). Passes are not merged or comped automatically.
- Each lane take is placed at the armed clip's start position, not at the loop start. For a loop that starts later than the clip, takes land early. Limitation.
- Lane takes are all unmuted: earlier passes play together with later ones until the user comps them (`comp.select`, `comp.assign`). Earlier passes are not auto-muted.
- A jump or a mute toggle during recording also ends a pass (it removes the live take), so it also makes a lane take.

## Verification

- Red, round 2: round-2 product files restored to HEAD (`30b0072ae`, the round-1 state) and the three tests that use round-2-only API removed from a scratch copy. `RecordingRealtimeTest` exit 6, six behavioural tests fail: standby take, Record then Play, armed while rolling, loop-wrap disarm, unclaimed take disarm, drop padding. The destruction test still passes there, as expected because round 1 already fixed the destructor. Round-2 files restored byte-identical (`git diff` compared with the saved diff).
  - Honest caveat: this is one combined reversal of the round-2 product code, not one reversal per fix.
- Green, round 2: `RecordingRealtimeTest` exit 0, 20 of 20 cases.
- Existing suites, all exit 0 (`ctest` from `build/tests`): RecordClipTest, RecordingLatencyTest, RecordingPassTest, RecordingInputPathTest, RecordRingBufferTest, ControlRecordingRoutesTest, SampleClipWindowTest, ClipSerialisationTest, SessionAudioRecorderTest, ClipFadesRenderTest, AudioClipLoopTest, SessionPlaybackRenderTest, ClipGainEnvelopeTest, RangeEditsTest, ClipLinkTest, ClipWarpPersistenceTest, PunchWindowTest, SessionAudioRecordTest, SessionGridRecordTest; CollabTransportTest, SessionTransportBarTest, TelemetryTransportTest.
- Gates at the commit, all exit 0: `tests/complexity-gate.sh --check`, `tests/file-length-gate.sh --check`, `tests/fork-sources-gate.sh`, `tests/no-upstream-regression-gate.sh`.
- Commands snapshot regenerated from a live instance (`tools/mcp-zene-control/snapshot_commands.py` through the harness): 822 commands, 76 groups, unchanged id set; the diff covers `record.get_state` only (the new `clip_takes` result field).
- Not run: the full `ctest` bar and `run-all-gates.sh`; `tests/rt-safety-sweep.py`.

## Open

- Rp3: a clip re-armed in the 10 ms before the reaper installs an old claimed take is disarmed by that take. Not fixed.
- The GUI does not show the drop warning: `SampleClip::recordTakeDropped` is emitted but nothing connects to it.
- Per-clip metadata (`SampleClip::lastTakeDroppedFrames()`) is not reported by any command yet. The surfaced count is the process total in `record.get_state`.
- Gap records are capped at 256 pending per take. Beyond that the lost frames are counted (`framesUnpadded()`) but not padded and not surfaced.
- The audio-thread `AudioBusHandle::addPlayHandle` append (pre-existing, every play handle) is outside this batch.
- Unprepared take in `SampleTrack::play` end to end: not tested.

## Round 3 (2026-10-08)

Commits, oldest first: `ff37cf254` (item 2), `826ad30ad` (item 3), `06a6db8c7` (item 4), `b25936a46` (item 5), `a8107edf2` (item 1, with the manifest and the regenerated commands snapshot).

| Item | Verdict | Fix commit | Test | Red (previous commit) | Green |
|---|---|---|---|---|---|
| 1. Drops reach the user | FIXED: non-modal notice (TextFloat + status bar) from a Song signal on the main thread; `record.get_state` `clip_takes.clips` lists each clip with `last_take_dropped_frames`; snapshot regenerated from a live instance | a8107edf2 | droppedFramesReachTheSongAndTheCommandSurface (signal -> count, command reply field) | exit 1 (signal and reply field missing) | 26/26 |
| 2. Gap records beyond 256 | FIXED: full gap table merges the loss into its newest record; the newest record stays open until a newer one exists or the take finishes, so the drain never reads a record that can still grow; padding is placed at each record's position | ff37cf254 | gapTableKeepsEveryLostFrameWhenItOverflows (300 losses: 256 records, 300 frames); droppedFramesArePaddedEvenWhenTheGapTableOverflows (2 slots, take length and silence count) | compile error (SampleRecordGaps and the gap-slot argument are round-3 API) | 22/22 |
| 3. Rp3: re-armed clip disarmed by an earlier take | FIXED: each arming has a generation; a take carries the generation it was prepared under; a take of an earlier arming is installed as a lane take and does not touch the current arming | 826ad30ad | reArmedClipIsNotDisarmedByAnEarlierTake | exit 1 (clip disarmed) | 23/23 |
| 4. P6 passes sounding together | FIXED: the newest pass is the audible take; earlier passes stay on their lanes with their take clip muted | 06a6db8c7 | onlyTheNewestLoopPassIsAudible (takes muted 1 and 2, not 3; render with 1 and 2 unmuted is 1.6-1.9x the render with only 3, expected 0.35/0.2 = 1.75) | exit 1 (first take not muted) | 24/24 |
| 5. Unprepared take, end to end | DONE: a pass on an armed clip with no prepared takes is counted by SampleTrack::play, records nothing, and makes no lane take | b25936a46 | unpreparedTakeIsCountedAndRecordsNothing | passes on the previous commit (the path existed; this is its first end-to-end test, not a red-producing fix) | 25/25 |

Decisions to check:
- Item 4 uses the per-take-clip muted flag, as docs/COMPING.md says audibility is (a lane has no mute of its own). The composite was not used: `comp.select` would need a segment per pass, and the repo's composite does not define an active take for a whole track. Newer passes mute earlier ones automatically, so a user who unmutes an earlier pass hears it until the next pass.
- Item 2: when the gap table is full and frames were staged after the newest record, the merged silence lands at that record's position. The take keeps its length; timing is off by those frames. This happens only after 256 unpadded gap records, i.e. after the drain has stalled for well over a second of overflow. Below 256 the placement is exact.
- Item 1: the GUI notice is a MainWindow lambda and is not unit-tested (no headless MainWindow test). The `clip_takes` result is declared as a generic object, so the new keys do not show in the declared schema; the regenerated snapshot differs only in its capture timestamp and socket path.

Verification (round 3):
- Red/green per item: as in the table, each red is the previous commit's product files restored, with the test file unchanged where possible (item 2 is a compile error; item 5 has no red).
- Targeted suites on the final tree: 23 of 23 pass (`ctest` from `build/tests`): RecordClipTest, RecordingLatencyTest, RecordingPassTest, RecordingInputPathTest, RecordRingBufferTest, ControlRecordingRoutesTest, SampleClipWindowTest, ClipSerialisationTest, SessionAudioRecorderTest, RecordingRealtimeTest (26), ClipFadesRenderTest, AudioClipLoopTest, SessionPlaybackRenderTest, ClipGainEnvelopeTest, RangeEditsTest, ClipLinkTest, ClipWarpPersistenceTest, PunchWindowTest, SessionAudioRecordTest, SessionGridRecordTest, CollabTransportTest, SessionTransportBarTest, TelemetryTransportTest.
- Gates on the final tree, all exit 0: complexity, file-length, fork-sources, no-upstream-regression.
- Not run: the full `ctest` bar, `run-all-gates.sh`, `tests/rt-safety-sweep.py`.

Open:
- The GUI notice is not covered by a test.
- Round-2 open items still stand: GUI wiring of `lastTakeDroppedFrames` per clip beyond the command reply, the audio-thread `AudioBusHandle::addPlayHandle` append (pre-existing), and the one-combined-reversal caveat for the round-2 red evidence.
