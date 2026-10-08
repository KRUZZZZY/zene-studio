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
