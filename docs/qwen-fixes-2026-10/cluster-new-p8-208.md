ALREADY-FIXED

# cluster new-p8-208 — capture ring overflow is padded with silence and reported

Status: **ALREADY-FIXED** in this tree. No product or test change is made by this job;
the report records the evidence gathered in p1–p3 and re-runs the feature gate.

Evidence folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-208.r1/logs/`

## Feature gate

`python3 feature_check.py <tree>` → exit **0**, `RESULT: PASS`
(`gate-1.log`):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Code-reference chain (p1)

1. `include/SampleRecordAccumulator.h:201-213` — `append()` writes the block to the
   ring and, when `written < count`, records the shortfall with
   `m_gaps->note(m_framesStaged, count - written)`. The loss is anchored at the exact
   staged frame position, so it can be replaced by silence there.
2. `include/SampleRecordAccumulator.h:272-282` — `padDueGaps()` takes each due gap and
   inserts `gap->length` zero frames at `gap->position + m_framesPaddedLocal`,
   publishing `m_framesPadded` as it goes.
3. `src/core/SampleRecordHandle.cpp:172-188` — the reap path calls `finish()`, and when
   `framesDropped() > 0` accumulates `framesDropped`/`framesPadded`, emits the
   `qWarning` about a full capture ring, and forwards `take->accum->framesDropped()` to
   `SampleClip::acceptRecordedTake`.
4. `src/core/SampleClip.cpp:304-305` — stores `m_lastTakeDroppedFrames` and calls
   `Engine::getSong()->reportRecordTakeDropped(droppedFrames)`.
5. `src/core/ControlCommandsRecordingRoutes.cpp:180-187` — `record.get_state` exposes
   `clip_takes.dropped_frames`, `clip_takes.padded_frames` and per-clip
   `last_take_dropped_frames`.
6. `src/gui/MainWindow.cpp:236-241` — `Song::recordTakeDropped` becomes a non-modal
   `TextFloat::displayMessage` plus a status-bar message; never a modal dialog.

Tests covering the chain:

- `tests/src/core/RecordingRealtimeTest.cpp:834-863`
  `droppedFramesArePaddedWithSilenceInTheTake` — 1024-frame ring, 4096 frames offered,
  asserts the take is 4096 frames long, the first `4096-dropped` frames equal the input,
  and the remaining frames are digital silence.
- `tests/src/core/RecordingRealtimeTest.cpp:1529-1556`
  `droppedFramesReachTheSongAndTheCommandSurface` — calls
  `acceptRecordedTake(..., 5, ...)` and asserts one `recordTakeDropped` notice carrying
  `5`, `lastTakeDroppedFrames() == 5`, and a `record.get_state` clip row with `5`.

## p2 — normal path

| Run | Log | Result |
| --- | --- | --- |
| ctest (`build/tests`, test #234 `RecordingRealtimeTest`) | `test-1.log` | 1/1 passed, 12.00 s, 100% — exit **0** |
| direct `droppedFramesArePaddedWithSilenceInTheTake` | `test-1a.log` | 3 passed, 0 failed — exit **0** |
| direct `droppedFramesReachTheSongAndTheCommandSurface` | `test-1b.log` | 3 passed, 0 failed — exit **0** |

## p3 — failure and exhaustion

Two mutations were applied to prove the tests detect the defect, then reverted
(`fix.patch`, `fix2.patch` in the log folder).

| Mutation | Red log | Red result | Green log | Green result |
| --- | --- | --- | --- | --- |
| `append()` no longer notes the shortfall | `red-1.log` | FAIL at `RecordingRealtimeTest.cpp:851`: actual **1024**, expected **4096**; 2 passed, 1 failed — exit nonzero | `green-1.log` | PASS, 3 passed, 0 failed — exit **0** |
| `acceptRecordedTake` no longer stores/reports loss | `red-2.log` | FAIL at `RecordingRealtimeTest.cpp:1539`: `notices.count()` actual **0**, expected **1**; 2 passed, 1 failed — exit nonzero | `green-2.log` | PASS, 3 passed, 0 failed — exit **0** |

QtTest returns a nonzero status when a test reports `FAIL!`; the logs record the
`FAIL!` line and the `1 failed` total, and the green logs record `0 failed`.

## Sibling check

`RealtimeExport`, `SessionAudioRecorder` and `SamplerTakeRecorder` all own the same
shared accumulator, so the padding and reporting implemented in it applies to them:

- `src/core/RealtimeExport.cpp:62` constructs `SampleRecordAccumulator`; `:100-103`
  exposes `framesDropped()`.
- `src/core/SessionAudioRecorder.cpp:58` constructs `SampleRecordAccumulator`;
  `:103-107` exposes `framesDropped()`.
- `src/core/SamplerTakeRecorder.cpp:45` constructs `SampleRecordAccumulator`;
  `:107-109` exposes `framesDropped()`.

None of the three has its own gap/pad implementation, so there is no duplicate code
path that the fix misses.

## Test matrix

| Requirement | Evidence | Result |
| --- | --- | --- |
| Normal recording | `test-1.log`, `test-1a.log`, `test-1b.log` | pass, exit 0 |
| Failure: loss reaches song and command surface | `red-2.log` / `green-2.log` | fails before, passes after |
| Exhaustion: full 1024-frame ring padded to 4096 | `red-1.log` / `green-1.log` | fails before, passes after |
| Reconfiguration (re-arm / loop-pass take) | not exercised in this log folder | not covered here; the code keeps the generation guard at `SampleClip.cpp:307-311`, no run recorded |
| Audio-thread constraints | `SampleRecordAccumulator.h:201-213` is `noexcept` and only writes the ring / updates atomics; the gap note is a fixed-capacity structure, no allocation on the audio thread | code inspection only; no RT-latency log in this folder |
| Supported-platform build requirements | `build-1.log`, `build-2.log`, `build-red-*.log`, `build-green-*.log` | Linux x86_64, GCC 16.2.1 only; no Windows/macOS build recorded here |

## Self-check (every submit_check ASK)

- **RED/GREEN exits** — red-1 and red-2 each record `FAIL!` and `1 failed` (nonzero);
  green-1 and green-2 each record `0 failed` (exit 0). Logs listed above.
- **Determinism of the 1024-frame ring** — the ring is a fixed 1024 frames and 4096 are
  offered, so exactly 3072 are dropped; the test asserts the exact take length and
  every frame's content. It passed in `test-1a.log` and again in `green-1.log`.
- **Injected `droppedFrames=5`** — `red-2.log` shows 0 notices; `green-2.log` shows one
  notice carrying 5, `lastTakeDroppedFrames() == 5` and the same value on the command
  surface.
- **Cause completeness / siblings** — the chain accumulator → handle → clip → song →
  command surface → GUI is cited above with file:line, and the three sibling recorders
  share the same accumulator.
- **No collateral damage** — this job touches only this report; the feature gate passes
  and `git status --porcelain` is empty after the commit.
- **ALREADY-FIXED code reference** — `SampleRecordAccumulator.h:201-213` still holds the
  `m_gaps->note(...)` block and `SampleClip.cpp:304-305` still stores and reports the
  loss; both mutation patches are reverted in the tree.

## Other findings

None. `feature_check.py` reported no FAIL row.
