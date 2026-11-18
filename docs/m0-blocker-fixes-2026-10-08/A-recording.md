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

## Round 4 (2026-10-08)

Commits, oldest first: `c96a6a6f3` (P6 blocker: lane creation), `50fc58f40` (P6: mute rule, comping, undo), `913656b9e` (P2: Record then Stop), `aaec04d57` (arm latch under the lock), `e58ed9f74` (drop count atomic), `adc844f93` (test assertions, idle poll).

| Item (review r2) | Verdict | Fix commit | Test | Red (previous commit) | Green |
|---|---|---|---|---|---|
| 1. BLOCKER: lane creation cuts the next loop pass | FIXED. Two causes: `updateTrackClips` reached from the lane clip's construction and its position change, and the lane clip's own `dataChanged` reaching `playbackPositionChanged`. Both removed the track's live handles. Lane creation now suspends the track's clip updates (`setClipUpdatesSuspended`, atomic), and `playbackPositionChanged` does nothing while suspended | c96a6a6f3 | `loopPassesKeepTheirLengthWhileTheReaperRuns`: three passes, a real jump back to the loop start between them (`song->setPlayPos(0)`), the reaper running between periods | exit 1: lanes 9728 / 8943 / 8945 frames | 9728 / 9728 / 9728 (an unbroken recording of 120 periods gives 30208 = 30720 − 512 latency, so each pass loses only the round-trip latency) |
| 2. Mute rule, comping, undo | FIXED. Only the latest pass gives way to a new one, and only while still audible as recording left it; the pass state starts empty on each arming; a muted take still plays where the composite names its lane (the comp decides its spans); arming checkpoints the track, and the lane take and the disarm are not journalled separately | 50fc58f40 | `aUserUnmutedPassIsNotMutedAgain`, `aNewArmingLeavesEarlierTakesAlone`, `aComposedMutedTakePlaysItsSpan` (the comped first take sounds 0.25 of the second), `oneUndoStepTakesBackARecording` (one undo: no lanes) | exit 4 (all four fail) | 31 of 31 |
| 3. Record, then Stop while stopped | FIXED: the latch is cleared before `Song::stop`'s early return | 913656b9e | `recordThenStopWhileStoppedLeavesNoLatch` | exit 1 | green |
| 4. `armRecordTake` writes the playing latch without the lock | FIXED: the write is under `requestChangesGuard` | aaec04d57 | none: the race needs the render thread's read to interleave with the write, which a unit test cannot force | n/a | 32 of 32 |
| 5. `record.get_state` reads `lastTakeDroppedFrames` | FIXED: the field is atomic, so the read is safe on any thread (whether the command runs on the UI thread is not confirmed; it no longer matters) | e58ed9f74 | the existing notice/reply test stays green | n/a (no behaviour change) | green |
| 6a. `clipDeletedBeforeTheTakeIsInstalledIsSafe` asserted `QVERIFY(true)` | FIXED: it asserts the reaper took a queued take (`takesReaped`) and that the deleted clip is gone (`QPointer`). The count is not exactly one: deleting the track also queues the clip's standby takes | adc844f93 | itself | compile error on HEAD (`takesReaped` is round-4 API) | green |
| 6b. `queueFullIsCounted` assumed an empty queue | FIXED: the queue is pumped empty first | adc844f93 | itself | not red-producing | green |
| 6c. Standby drain threads woke about 1000 times a second per armed clip | FIXED: idle poll 20 ms (about 50 a second). Not on the audio thread; the ring holds about 0.34 s | adc844f93 | none: a timing property | n/a | green |

Decisions to check:
- Item 2: a recording is journalled as its arm (the record flag, then the track's checkpoint). One undo takes the track back to its state before the arm, which removes the lanes and the mutes. The arm's own record-flag entry is a second, empty step, which the user can undo too.
- Item 2: the mute rule mutes only the immediately previous pass. Passes two or more back are never touched again, so a user's choice on them persists.
- Item 1: the earlier 392-frame loss in passes two and three was a test artefact: my first version simulated the wrap without moving the transport, so the passes did not start at the loop start. The real jump gives exact passes.

Verification (round 4):
- Targeted suites: 26 of 26 pass from `build/tests` (the recording, clip, render, punch and transport suites from round 3, plus TakeLaneTest, TakeLaneCompTest and TakeLaneCompPlaybackTest, since the composite now reaches muted takes). RecordingRealtimeTest: 32 of 32.
- Gates on the final tree, all exit 0: complexity, file-length, fork-sources, no-upstream-regression (also run at each commit).
- No command changed in round 4, so the commands snapshot is unchanged.
- Not run: the full `ctest` bar, `run-all-gates.sh`, `tests/rt-safety-sweep.py`.

Open:
- The GUI notice is still not covered by a test (round 3).
- The main-thread join at take end can wait up to the idle poll (20 ms) now.
- Rp3 windows and B6 (round 2 notes) are closed as far as the tests can show; the mute rule is tested with at most two or three passes.
- Pre-existing `AudioBusHandle::addPlayHandle` audio-thread append is outside this batch.

## Round 5 (2026-10-09)

Commits, oldest first: `6afd3b403` (row 5: take-lane edits under the change lock), `84164b78b` (rows 2, 6, 7: the arm is one undo step with the record flag; lane-creation suspension as an RAII guard), and this report.

| Item (review r3) | Verdict | Fix commit | Test | Red (previous commit) | Green |
|---|---|---|---|---|---|
| 6. First-pass install not journalled; one undo leaves the audio | NOT REPRODUCED as a defect. On the round-4 code one undo already restores the earlier audio: the arm's track snapshot holds the pre-record audio (the clip saves its buffer as base64 when it has no file). Added as guards: a plain recording is one undo step (and redo brings it back), and a recording over an existing take is undone to that take | none (guards) | aPlainRecordingIsOneUndoStep, aRecordingOverAnExistingTakeIsUndoneToIt | pass on the previous commit too | green |
| 7. One undo leaves the clip armed with takes prepared | FIXED. The record flag is not serialised, so no track checkpoint could restore it. The arm now journals one step: a checkpoint of the track and of a small journal object carrying the clip's flag (SampleRecordFlagState). The snapshot records the flag before the arm; restoring sets it through the clip, which releases the prepared takes. The record model no longer journals its own changes. Consistent with the app: clip.set_record is undoable (TrueInverse in the reversibility table) | 84164b78b | anUndoDuringALoopRecordingRestoresTheArmedState (one control.undo while the loop is still armed: lanes gone, clip disarmed, no takes prepared) | exit 1 (the clip stays armed) | green |
| 2 (notes). Suspension leaks if creation throws | FIXED: RAII guard (ClipUpdatesSuspension) | 84164b78b | none needed: normal paths unchanged | n/a | green |
| 5 (notes). Comp edits and the render-thread read of segments | FIXED: the TakeLaneModel mutators (addLane, removeLane, selectSegment, fillGaps, rebuild, clear, loadSettings) hold the engine's change lock. The comp commands run on the UI thread with no lock around their handler, so the lock must be in the model. The GUI comp path goes through the same mutators | 6afd3b403 | none: a race between a render read and a UI write is not forced by a unit test; take-lane and comp suites stay green | n/a | green |
| 16 (notes). Project load with an armed clip | VERIFIED, no change needed. The record flag is not serialised (saveSettings writes no record attribute), so loading never arms a clip and never adds a checkpoint or turns journalling off | none | none | n/a | n/a |

Finding (not in the review): the first version of row 7 journalled the record model itself. Restoring a checkpoint of that model raised a fatal error in AutomatableModel::controllerValue (an unexpected scale type on restore). A journal object of its own, with plain settings hooks, avoids the model path.

Open:
- An undo pressed while a recording is still armed restores the arm's state, but a live take that is still recording keeps going: when the transport stops, that take is installed as a lane take. Not covered; the undo of a live recording should stop it.
- Row 6's audio concern does not reproduce on the code as written; if the owner wants the first-pass install journalled as its own step, that would be a new, separate step and would break the one-undo rule for a recording.
- The GUI notice is still untested (round 3).

Verification (round 5):
- Recording binary: 35 of 35 pass (RecordingRealtimeTest).
- Targeted suites: 26 of 26 pass from `build/tests`: RecordClipTest, RecordingLatencyTest, RecordingPassTest, RecordingInputPathTest, RecordRingBufferTest, ControlRecordingRoutesTest, SampleClipWindowTest, ClipSerialisationTest, SessionAudioRecorderTest, RecordingRealtimeTest, ClipFadesRenderTest, AudioClipLoopTest, SessionPlaybackRenderTest, ClipGainEnvelopeTest, RangeEditsTest, ClipLinkTest, ClipWarpPersistenceTest, PunchWindowTest, SessionAudioRecordTest, SessionGridRecordTest, CollabTransportTest, SessionTransportBarTest, TelemetryTransportTest, TakeLaneTest, TakeLaneCompTest, TakeLaneCompPlaybackTest.
- Gates on the final tree, all exit 0: complexity, file-length, fork-sources, no-upstream-regression.
- Not run: the full `ctest` bar, `run-all-gates.sh`, `tests/rt-safety-sweep.py`.

## Round 6 (2026-10-09)

Commit: `05a6720c4` (test and correction; no product change). Round 6 added no product code: the investigation below showed the behaviour already holds.

| Item | Verdict | Fix commit | Test | Red | Green |
|---|---|---|---|---|---|
| Undo pressed mid-recording: the live take is installed as a lane when the transport stops | NOT REPRODUCED. The undo rebuilds the track's clips, so the armed clip is destroyed. Its destructor removes its live play handle (`updateClips`), and when the reaper finds the take's clip gone it installs nothing. Traced: the live take never reaches an accept call | 05a6720c4 (test only) | anUndoMidPassInstallsNothingAfterwards: loop recording, wrap, undo mid-pass, stop; no lane take, clip back to pre-arm state (no lanes, empty audio, disarmed, no takes prepared) | passes on the round-4 code too (a guard, not a red) | green (36 of 36 in the recording binary) |
| Row 7 (round 5): one undo left the clip armed | Round 5's red was spurious: the test read the old clip pointer after the undo, which points at a destroyed object (the undo rebuilds the clip). Fixed: the test now looks the restored clip up. The round-4 code passes this test too, so there was no defect. The round-5 journal object for the flag is kept as the cleaner design (one undo step carries the flag), but it does not fix a failing test | 05a6720c4 | anUndoDuringALoopRecordingRestoresTheArmedState (fixed to look the clip up) | not a red: round 4 passes | green |
| Row 6 (round 5): one undo step for a recording | Kept as it is: the arm, the content and the flag are one undo step | none | aPlainRecordingIsOneUndoStep, aRecordingOverAnExistingTakeIsUndoneToIt | pass on round 4 too | green |

Decisions and caveats:
- The mid-pass guarantee rests on the undo rebuilding the armed clip. If an undo ever kept the clip object while the recording was live, the live take would still be installed when the transport stops. No such path exists today; an explicit discard would be needed if one is added.
- A discard path for the live take was considered and reverted: it was never reached (the clip is destroyed first), so it would have been dead code.

Verification (round 6):
- Recording binary: 36 of 36 pass (RecordingRealtimeTest).
- Targeted suites: 26 of 26 pass from `build/tests` (the round-5 list).
- Gates on the final tree, all exit 0: complexity, file-length, fork-sources, no-upstream-regression.
- Not run: the full `ctest` bar, `run-all-gates.sh`, `tests/rt-safety-sweep.py`.

Open:
- The GUI notice is still untested (round 3).
- The round-5 red evidence for row 7 is withdrawn (see above); no other round's red evidence depended on a dead pointer, as far as the tests are concerned (round 5's loop-undo test was the only test that read a clip after its undo).

## Round 7 (2026-10-09): final rework before merge

Commits, oldest first: `9eeff710a` (revert the record-flag journal object; the arm is the track's checkpoint again; the live-take comment), `82e5b0783` (audition lane written under the change lock), `1970b9713` (clip.set_record undone by control.undo).

| Review item (r4) | Verdict | Fix commit | Test | Red | Green |
|---|---|---|---|---|---|
| 1. Revert SampleRecordFlagState (84164b78b) | DONE. Removed: the object, its member, its settings hooks, the arm's flag snapshot. The arm is one track checkpoint again. Kept: `m_recordModel.setJournalling(false)` in both constructors, and the ClipUpdatesSuspension RAII guard | 9eeff710a | the round-5/6 undo tests stay green (anUndoDuringALoopRecordingRestoresTheArmedState, anUndoMidPassInstallsNothingAfterwards, aPlainRecordingIsOneUndoStep, aRecordingOverAnExistingTakeIsUndoneToIt) | no red: the object had no observable effect | green |
| 2. setAuditionLane not under the change lock | DONE: out of line, under the same lock helper as the other TakeLane mutators | 82e5b0783 | none: the race needs a render read to interleave with the UI write | no red | take-lane and comp suites green |
| 3. Comment at the undo path (live-take guarantee) | DONE: at journalArmStep. It states the guarantee rests on the rebuild destroying the armed clip, and what happens if an undo ever kept the clip | 9eeff710a | n/a | n/a | n/a |
| 4. clip.set_record followed by control.undo | DONE: control.undo disarms the clip that clip.set_record armed (the command's own action step, keyed by id) | 1970b9713 | clipSetRecordIsUndoneAndRedoneByControl | a guard: the undo predates round 7 | green |

Known limit (recorded, not asserted by a test): a redo does not re-arm a clip. The record flag is not in the clip's serialised state, so the command's redo has nothing to restore. Unchanged since round 5.

Verification (round 7):
- Recording binary: 37 of 37 pass (RecordingRealtimeTest).
- Targeted suites: 26 of 26 pass from `build/tests`: the round-6 list, including TakeLaneTest, TakeLaneCompTest and TakeLaneCompPlaybackTest.
- The four cheap gates on the final tree, all exit 0: complexity, file-length, fork-sources, no-upstream-regression.
- Not run: the full `ctest` bar, `run-all-gates.sh`, `tests/rt-safety-sweep.py`.

Open (not blockers, per the review):
- The MainWindow drop notice needs an on-screen check (the offscreen harness does not show it).
- The gap-table timing shift after 256 unpadded records is accepted (round 2 note).
- Known limit above: redo of an arm does not re-arm the clip.
