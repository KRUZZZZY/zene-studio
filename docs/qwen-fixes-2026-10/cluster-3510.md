ALREADY-FIXED

# Cluster 3510: ring overflow silently removes recorded audio

## Verdict
ALREADY-FIXED on `bugs/qwen-3510`. Commit `ab938848f8` ("fix(recording): ... pad dropped capture
with silence ... (cluster 5312, 3510, ...)") fixed both parts of the finding: the take is no longer
shortened or misaligned, and the dropped-frame count is reported. The earlier SKIPPED-SCOPE attempt
looked at code from before that commit. No product code was changed in this session.

## Evidence (file:line on this branch)
- `include/SampleRecordAccumulator.h:201-213`: `append()` records every frame the ring rejects as a
  gap (`m_gaps->note(position, count - written)`) and does not discard it silently.
- `include/SampleRecordAccumulator.h:272-280` (`padDueGaps`): the drain thread inserts that many
  silent frames at the gap position, so later frames stay at their correct time.
- `src/core/SampleRecordHandle.cpp:171-177` (`reapRetiredTakes`): after `finish()` it reads
  `framesDropped()`, adds it to `s_framesDropped`/`s_framesPadded` and logs a `qWarning` on the
  main thread (not the audio thread).
- `src/core/SampleRecordHandle.cpp:187`: the count is passed to `SampleClip::acceptRecordedTake`.
- `src/core/SampleClip.cpp:303-305`: the clip stores `m_lastTakeDroppedFrames` and calls
  `Song::reportRecordTakeDropped`, which emits the `recordTakeDropped` signal for the GUI notice.
- `src/core/ControlCommandsRecordingRoutes.cpp:180-181`: `record.get_state` returns
  `clip_takes.dropped_frames` from `SampleRecordHandle::framesDroppedTotal()`.

Existing regression tests that cover this (`tests/src/core/RecordingRealtimeTest.cpp`):
- `droppedFramesArePaddedWithSilenceInTheTake` (line 834): after an overflow the take keeps its full
  length (`take->size() == offered`), and the lost frames are silence.
- `droppedFramesArePaddedEvenWhenTheGapTableOverflows` (line 1147) and
  `gapOverflowPreservesLossPositions` (line 1076): padding stays correct when the gap table is full.
- `droppedFramesReachTheSongAndTheCommandSurface` (line ~1530): the drop count reaches the Song
  signal, `lastTakeDroppedFrames()` and `record.get_state`.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/3510.r1/logs)
- `cmake --build build --target zene RecordingRealtimeTest -j4 > logs/01-build.log 2>&1` → EXIT=0
- `cd build/tests && ctest -R RecordingRealtimeTest --output-on-failure > logs/02-test.log 2>&1`
  → EXIT=0 (100% tests passed out of 1)
- `./RecordingRealtimeTest droppedFramesArePaddedWithSilenceInTheTake droppedFramesArePaddedEvenWhenTheGapTableOverflows gapOverflowPreservesLossPositions droppedFramesReachTheSongAndTheCommandSurface > logs/03-test-functions.log 2>&1`
  → EXIT=0 (Totals: 6 passed, 0 failed)
- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/04-feature-check.log 2>&1`
  → EXIT=0, RESULT: PASS (all 8 gates PASS)

No red/green run was done: no product change was made, so there is no fix to reverse. The
regression tests above were added with the fix in `ab938848f8`.

## Other findings
- None.

## Self-check
- **Red/green with the product change reverted:** does not apply. This session made no product
  change (the commit holds only this report), so there is nothing to reverse. The covering tests came
  with `ab938848f8`. On this branch they pass: `03-test-functions.log` EXIT=0, 6 passed, 0 failed.
  I did not reverse `ab938848f8` to show them failing. That commit also changes several other
  recording behaviours (clusters 5312, new-p8-208, 3506, P2, P3, P6), so reversing it here was not
  attempted.
- **Determinism:** `droppedFramesArePaddedWithSilenceInTheTake` overflows the ring on purpose: it
  offers 4096 frames to a 1024-frame ring in one `append()` call, with no drain running. It then
  calls `finish()`, which joins the drain thread, and checks the result. There are no sleeps or timing
  checks. `droppedFramesReachTheSongAndTheCommandSurface` calls
  `acceptRecordedTake(nullptr, 5, ...)` directly, with an injected count of 5.
- **Fix at the cause and complete:** the gap is recorded where the ring rejects frames
  (`SampleRecordAccumulator.h:201-213`) and padded on the drain thread (`:272-280`). Every consumer is
  consistent: `reapRetiredTakes` (`SampleRecordHandle.cpp:171-187`) is the only caller of
  `acceptRecordedTake` (`rg -n acceptRecordedTake src include`: one call site). It reads the final
  count after `finish()` joins the drain thread.
- **No collateral damage:** this session changed no product code, project format or limit
  (`git show --stat HEAD` lists only this report).
- **ALREADY-FIXED evidence:** `include/SampleRecordAccumulator.h:201-213`,
  `src/core/SampleRecordHandle.cpp:171-187`, `src/core/SampleClip.cpp:303-305` and
  `src/core/ControlCommandsRecordingRoutes.cpp:180-181`. Details are in the Evidence section above.
- **Sibling commands (app.*, arrangement.*):** not affected. Those come from the branch's earlier
  merge-train commits compared with master, not from this session. This session changed no command
  file. `record.get_state` already reports `clip_takes.dropped_frames`.
- **Tests added or changed (control-*.py):** those also come from earlier branch commits. This
  session added no tests.
