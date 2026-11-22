ALREADY-FIXED

# Cluster 5312: recording overflow silently removes audio and shifts the take

Verdict: ALREADY-FIXED on branch `bugs/qwen-5312`, with no product change in this session.

The finding asks for two things: (1) keep the missing duration in the take, and (2) tell the user
about the overflow. Both are already in the code. Earlier commits on this cluster did the work:
`ab938848f8` (cluster 5312: pad dropped capture with silence, recordTakeDropped signal),
`ff37cf2544` (cluster 5312: a full gap table merges the loss into its newest record) and
`10d9f55d86` (bounded preallocated overflow track for gaps).

## Evidence (file:line, read this session)

Duration preserved, so later audio is not shifted:
- `include/SampleRecordAccumulator.h:201-213`: `append()` (audio thread). When `writeBlock()` returns
  fewer frames than offered, the lost run goes into `m_gaps->note(stagedPosition, lost)`. The table
  is preallocated, so this path does not allocate or lock.
- `include/SampleRecordAccumulator.h:74-141`: `SampleRecordGaps`, a bounded SPSC gap table with an
  overflow track. When it is full, a loss merges its length into the newest record, so no lost
  length is dropped.
- `include/SampleRecordAccumulator.h:272-282`: `padDueGaps()` (drain thread) inserts `gap->length`
  silent frames at take index `position + framesPaddedSoFar`, which is where the loss happened.
  Audio after the gap stays at its original time.
- `include/SampleRecordAccumulator.h:314`: at finish, the newest gap is padded too.

The overflow is shown to the user:
- `src/core/SampleRecordHandle.cpp:168-174`: `reapRetiredTakes()` (main thread, after `finish()`
  joins) reads `framesDropped()`, sums the dropped and padded counts, and logs a `qWarning`.
- `src/core/SampleRecordHandle.cpp:183`: it passes `framesDropped()` to `SampleClip::acceptRecordedTake`.
- `src/core/SampleClip.cpp:304-305`: stores `m_lastTakeDroppedFrames` and calls
  `Song::reportRecordTakeDropped(droppedFrames)` (`include/Song.h:698`, signal at `include/Song.h:1015`).
- `src/gui/MainWindow.cpp:235-241`: the GUI connects to `Song::recordTakeDropped` and shows a
  TextFloat notice ("Recording dropped audio") plus a status-bar message for 8 s.
- `record.get_state` reports `clip_takes[].last_take_dropped_frames`. The test below asserts this.

Existing regression tests that cover the finding (`tests/src/core/RecordingRealtimeTest.cpp`):
- `droppedFramesArePaddedWithSilenceInTheTake` (line 834): overflows a 1024-frame ring with 4096
  frames. The take must be 4096 frames long: the input first, then silence.
- `gapTableKeepsEveryLostFrameWhenItOverflows` (1053), `gapOverflowPreservesLossPositions` (1076),
  `droppedFramesArePaddedEvenWhenTheGapTableOverflows` (1147).
- `droppedFramesReachTheSongAndTheCommandSurface` (1529): `recordTakeDropped` is emitted with the
  dropped count, and `record.get_state` lists it.

These tests are deterministic. They overflow by offering more frames than the ring holds and use no timing.

## Commands run this session (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5312.r1/logs)

- `cmake --build build --target zene RecordingRealtimeTest -j4 > logs/01-build.log 2>&1`: EXIT=0
- `cd build/tests && ctest -R RecordingRealtimeTest --output-on-failure > logs/02-test.log 2>&1`:
  EXIT=0 ("100% tests passed out of 1")
- `./RecordingRealtimeTest droppedFramesArePaddedWithSilenceInTheTake gapTableKeepsEveryLostFrameWhenItOverflows gapOverflowPreservesLossPositions droppedFramesArePaddedEvenWhenTheGapTableOverflows droppedFramesReachTheSongAndTheCommandSurface > logs/03-test-cases.log 2>&1`:
  EXIT=0 ("Totals: 7 passed, 0 failed")
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/04-feature-check.log 2>&1`:
  EXIT=0, every row PASS, "RESULT: PASS"

No product code changed, so there is no new regression test and no red/green patch cycle.
Commit `ab938848f8`'s message says its tests failed against the earlier product code. I did not
re-run that red check this session.

## Other findings

None.

## Self-check

- Red/green: not applicable. The verdict is ALREADY-FIXED and this session changes no product code,
  so there is nothing to revert. I did not re-run a red check this session. The covering tests pass
  on this branch: `ctest -R RecordingRealtimeTest` EXIT=0 (logs/02-test.log), and the five named
  cases EXIT=0 with "7 passed, 0 failed" (logs/03-test-cases.log).
- Deterministic: yes. `droppedFramesArePaddedWithSilenceInTheTake` overflows by offering 4096
  frames to a 1024-frame ring in one `append()` before `finish()`. The drain thread cannot read
  until the append returns, so the overflow happens whatever the drain thread is doing. The output
  check is exact (length 4096, first `offered - framesDropped()` frames equal the input, the rest
  silent). `droppedFramesReachTheSongAndTheCommandSurface` calls `acceptRecordedTake(nullptr, 5, ...)`
  directly, with a QSignalSpy and no timing.
- At the cause and complete: the cause is `append()` dropping frames. That path records gaps
  (`include/SampleRecordAccumulator.h:208-212`) and the drain thread pads them (`:272-282`). Every
  user of the accumulator's take in the clip path (`src/core/SampleRecordHandle.cpp:168-183`)
  passes the dropped count on, and the user notice is at `src/gui/MainWindow.cpp:235-241`. This
  session changed nothing.
- No collateral damage: no file changed except this report. Project loading and saved data are not touched.
- ALREADY-FIXED evidence: `include/SampleRecordAccumulator.h:201-213, 272-282`,
  `src/core/SampleRecordHandle.cpp:168-183`, `src/core/SampleClip.cpp:304-305`,
  `include/Song.h:698,1015`, `src/gui/MainWindow.cpp:235-241`, and the tests in
  `tests/src/core/RecordingRealtimeTest.cpp:834,1053,1076,1147,1529`.
- Sibling commands (app.*, arrangement.*): not affected. This session changed no command file. The
  check lists them from earlier commits already on the branch, not from this report's commit
  (`git show --stat HEAD` for this commit lists only the report).
- "Tests added or changed" in the check (tests/control-*.py) comes from earlier branch commits,
  not from this session.
