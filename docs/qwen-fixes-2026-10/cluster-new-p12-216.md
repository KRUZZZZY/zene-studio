# Cluster new-p12-216: FIXED

## Verdict

The merged-gap timing shift is closed within a bounded, preallocated overflow table:
losses are recorded at their own positions until the combined table is full; only the
surplus merges into the newest record, adding its length (and using that record's
position for silence, so timing is not preserved beyond saturation).

## Evidence

- `include/SampleRecordAccumulator.h`: `SampleRecordGaps` owns a nominal track plus a
  preallocated overflow track (default equal size, or an explicit `overflowCapacity`).
  `note()` fills the nominal track first, then the overflow track, and only merges once
  both are full. `takeDue()` still returns records in position order.
- `tests/src/core/RecordingRealtimeTest.cpp`: adds
  `accumulatorPreservesSampleOrderAcrossSeparatedLosses`, which stages two identifiable
  nonzero bursts separated by a dropped frame (gap capacity 1), finishes the accumulator,
  and asserts the exact take sequence
  (burst1, silence, burst2, silence). `gapTableKeepsEveryLostFrameWhenItOverflows`
  keeps its saturation accounting (512 stored records / 600 losses / 88 merged);
  `gapOverflowPreservesLossPositions` retains its bounded overflow assertions.
- End-to-end size/silence counts remain correct (`droppedFramesArePaddedEvenWhenTheGapTableOverflows`
  stays green), and the direct gap assertions retain bounded saturation behaviour.

## Validation evidence

| Check | Exact command | Result |
|---|---|---|
| RED (reversed product) | `git apply -R .../fix.patch`; `cmake --build build --target RecordingRealtimeTest -j12`; `ctest -R '^RecordingRealtimeTest$' --output-on-failure` (from `build/tests`) | build EXIT=0; tests EXIT=8, `36 passed, 3 failed` — `gapTableKeepsEveryLostFrameWhenItOverflows`, `gapOverflowPreservesLossPositions`, and `accumulatorPreservesSampleOrderAcrossSeparatedLosses` (frame 9) |
| GREEN | `cmake --build build --target zene RecordingRealtimeTest -j12`; same ctest command | build EXIT=0; tests EXIT=0, `100% tests passed out of 1` |
| Rebuild zene | included in GREEN build (`zene`) | EXIT=0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | EXIT=1 (pre-existing unrelated baseline regressions only; no `SampleRecordAccumulator` entry) |
| File-length gate | `bash tests/file-length-gate.sh --check` | EXIT=1 (pre-existing unrelated baseline regressions only; no touched-file entry) |

fix.patch = the product hunk from commit `10d9f55d8` (reversal leaves the overflow
capacity at the original one-track design), generated as
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-216.r3/logs/fix.patch`.

## Files committed

- `tests/src/core/RecordingRealtimeTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-216.md`
    0