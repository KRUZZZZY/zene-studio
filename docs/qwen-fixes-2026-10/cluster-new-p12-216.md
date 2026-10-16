# Cluster new-p12-216: FIXED

## Verdict

The merged-gap timing shift is fixed: the gap table now has a bounded, preallocated
overflow capacity (default equal to the nominal table size) so that losses beyond
the nominal capacity are still recorded at their own positions until that bound is
reached. Only once both the nominal and overflow capacity are full do later
losses merge into the newest record. The audio-thread `note()` path stays
allocation-free and nonblocking, and the drain still inserts silence in position
order.

## Evidence

- `include/SampleRecordAccumulator.h`: `SampleRecordGaps` gained an overflow track
  whose slots are preallocated in the constructor (`capacity` nominal + equally
  large overflow by default, or an explicit `overflowCapacity`). `note()` fills the
  nominal track first, then the overflow track, and only merges once both are
  full. `takeDue()` returns records in position order, so silence is padded where
  each loss occurred rather than at an older record's position.
- End-to-end length/silence-count behaviour is unchanged (`droppedFramesArePaddedEvenWhenTheGapTableOverflows`
  stays green).

Regression: `tests/src/core/RecordingRealtimeTest.cpp` now contains
`gapOverflowPreservesLossPositions`, which fills a 2+2-slot table, stages
identifiable loss positions (0, 10, 11, 20, 30), and asserts the stored records
keep positions 0, 10, 11, 20 with only the truly-beyond-bound loss merged.
`gapTableKeepsEveryLostFrameWhenItOverflows` was updated to the doubled bound
(512 stored records, 600 losses, 88 merged).

## Validation evidence

| Check | Exact command | Result |
|---|---|---|
| RED (reversed product) | `git apply -R .../fix.patch`; `cmake --build build --target RecordingRealtimeTest`; `.../RecordingRealtimeTest` | FAILED: `gapTableKeepsEveryLostFrameWhenItOverflows` (merged 344 vs 88) and `gapOverflowPreservesLossPositions` (merged 3 vs 1) |
| GREEN | `cmake --build build --target zene` ; `ctest -R '^RecordingRealtimeTest$'` from `build/tests` | `EXIT=0`, `100% tests passed out of 1`, `38 passed, 0 failed` |
| Rebuild zene | `cmake --build build --target zene` | `EXIT=0` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | `EXIT=1` (pre-existing unrelated regressions only; no SampleRecordAccumulator entry) |
| File-length gate | `bash tests/file-length-gate.sh --check` | `EXIT=1` (pre-existing unrelated regressions only; no SampleRecordAccumulator.h entry) |

## Files committed

- `include/SampleRecordAccumulator.h`
- `tests/src/core/RecordingRealtimeTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-216.md`
