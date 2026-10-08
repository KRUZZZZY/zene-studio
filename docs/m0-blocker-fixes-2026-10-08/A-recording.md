# Batch A-recording: clip recording on the audio thread (2026-10-08)

Branch `bugs/m0-blk-recording`, base `28cc3a0b5`. One coherent change (commit `e3083dce0`).

## Design

- `SampleClip::prepareRecordTake()` (main thread; on `playbackStateChanged` and the record model's
  `dataChanged`) builds the clip's next `SampleRecordHandle`: its accumulator and drain thread.
- `SampleClip::claimRecordHandle()` (audio thread) hands that handle to `SampleTrack::play`. It
  allocates nothing; a clip with no prepared take is counted (`SampleRecordHandle::unpreparedTakes()`)
  and not recorded.
- `SampleRecordHandle::~SampleRecordHandle()` (any thread, runs on the audio thread when the engine
  removes the handle) only calls `requestFinish()` on the accumulator and pushes the take onto a
  bounded lock-free `MpscQueue` (256 slots). It does not join, allocate or touch the clip.
- `SampleRecordHandle::reapRetiredTakes()` (main thread, 10 ms `QTimer` started by the first handle)
  finishes each queued take, logs and counts dropped frames, then calls `setSampleBuffer` and
  `setRecord(false)` on the clip. A clip deleted meanwhile is detected through a `QPointer`.
- `SampleTrack::play` schedules clips into a fixed `std::array<ScheduledClip, 64>`.

## Items

| Cluster | Verdict | Fix commit | Test | Red exit | Green exit | Notes |
|---|---|---|---|---|---|---|
| 3508 | FIXED | e3083dce0 | RecordingRealtimeTest::armedClipClaimsItsPreparedTakeWithoutAllocation (claim probe) and ::recordHandleDestructionPerformsNoAllocation | 8 (destruction test; the claim test does not compile against the pre-fix API) | 0 | Construction now on the main thread. Residual audio-thread allocation: `AudioEngine::addPlayHandle` -> `AudioBusHandle::addPlayHandle` appends to a list (pre-existing, shared with every play handle, not changed here). |
| 4411 | FIXED | e3083dce0 | as 3508 | as 3508 | 0 | Same defect as 3508. |
| 5308 | FIXED | e3083dce0 | recordHandleDestructionPerformsNoAllocation (60 allocations in the pre-fix destructor, measured) | 8 | 0 | The clip is updated by the reaper on the main thread; the test waits for `!isRecord()` with QTRY. |
| 5312 | PARTIAL | e3083dce0 | none at handle level (see note) | n/a | n/a | Drops are now counted per take, logged on the main thread and summed in `framesDroppedTotal()`. The gap is NOT padded with silence and the take is still shortened. `record.get_state` does not show the count (that needs a control-command change and a snapshot regeneration, not done). Accumulator-level overflow count is covered by the existing `ringBufferIsBoundedAndCountsOverflow`. |
| 3510 | PARTIAL | e3083dce0 | as 5312 | n/a | n/a | As 5312. |
| new-p8-208 | PARTIAL | e3083dce0 | as 5312 | n/a | n/a | As 5312. Fixed-visibility only; no silence insertion or user-visible error. |
| 3506 | FIXED | e3083dce0 | covered by the existing SampleTrack/clip render suites (below) | n/a | n/a | Behaviour change confined to a track with more than 64 clips containing one instant: the extra clips are not started. Unreachable in practice (only overlapping clips are scheduled), but it is a stated limit. |

## Verification

- Red: the pre-fix product sources (from `28cc3a0b5`) were restored temporarily and the claim test
  was removed from the test file for that build only. `ctest -R '^RecordingRealtimeTest$'` exit 8:
  `recordHandleDestructionPerformsNoAllocation` got 60 allocations (expected 0). Sources restored
  byte-identically (`git diff` compared with the saved fix diff), rebuilt.
- Green (at the commit): `ctest -R '^RecordingRealtimeTest$'` exit 0 (4 of 4 test cases incl. the two new ones).
- Existing suites, all exit 0 (`ctest` from `build/tests`): RecordClipTest, RecordingLatencyTest,
  RecordingPassTest, RecordingInputPathTest, RecordRingBufferTest, ControlRecordingRoutesTest,
  SampleClipWindowTest, ClipSerialisationTest, SessionAudioRecorderTest, ClipFadesRenderTest,
  AudioClipLoopTest, SessionPlaybackRenderTest, ClipGainEnvelopeTest, RangeEditsTest, ClipLinkTest,
  ClipWarpPersistenceTest.
- Gates at the commit, all exit 0: `tests/complexity-gate.sh --check`, `tests/file-length-gate.sh --check`,
  `tests/fork-sources-gate.sh`, `tests/no-upstream-regression-gate.sh`.
- Manifest: `tests/upstream-modifications.txt` gained five entries (SampleClip.h, SampleClip.cpp,
  SampleTrack.cpp, SampleRecordHandle.h, SampleRecordHandle.cpp) with reasons.
- Not run: the full `ctest` bar and `run-all-gates.sh` (the lane rule runs them at merge time).
  The RT-safety sweep (`tests/rt-safety-sweep.py`) was not run.

## Open

- Drop surfacing is partial (5312/3510/new-p8-208): no silence padding, no record.get_state field, no
  handle-level drop test.
- The audio-thread `AudioBusHandle::addPlayHandle` append is outside this batch.
