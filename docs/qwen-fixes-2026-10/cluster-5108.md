ALREADY-FIXED

# Cluster 5108: loudness bins collapse everything above +5 LUFS

The finding describes the old `binIndex()` that clamped every value above +5 LUFS to the last bin. That code is gone from this branch: commits `ea5daa8f1f` and `23f33a57c8` (cluster new-p9-233, both ancestors of HEAD, checked with `git merge-base --is-ancestor`) replaced the clamp with fixed 0.25 LU overflow bands from +5 LUFS up to +805 LUFS.

## Evidence (read this session)

- `include/LufsMeter.h:195-203`: 0.05 LU bins through +5 LUFS (`BinCount = 1501`), then `OverflowBinCount = 3200` bands of `OverflowBinWidthLu = 0.25`; `LastBinIndex = BinCount + OverflowBinCount - 1`. Both `m_gating` and `m_shortTerm` are sized `BinCount + OverflowBinCount` (`LufsMeter.h:275,280`).
- `src/core/LufsMeter.cpp:290-303`: `binIndex()` maps a value at or above +5 LUFS to `OverflowBinIndex + offset` (only at or above +805 LUFS does it clamp to `LastBinIndex`).
- `src/core/LufsMeter.cpp:305-315`: `binLoudness()` returns the band centre for overflow bins, so the relative gate (`gatedLoudness`, 317-343) and the LRA percentiles (`shortTermSpreadAbove`, 400-424) keep level differences above +5 LUFS and no longer skip every bin.
- Regression tests for the finding's two scenarios are in `tests/src/core/LufsMeterTest.cpp`:
  - `:290` `relativeGateKeepsLevelsAboveHistogramRange` (steady +15 dBFS 5.1 signal, about +19.6 LUFS; integrated must not be -inf).
  - `:313` `relativeGateExcludesTheLowerHotPassage` (+5.1 and +25.1 dBFS passages).
  - `:327` `loudnessRangePreservesVaryingHotLevels` (hot passages must show an LRA spread).
- The earlier fix's own red/green reversal proof is recorded in `docs/qwen-fixes-2026-10/cluster-new-p9-233.md`. I did not repeat it this session. I changed no product code, so there was nothing to reverse.

## Commands run this session

| Command | EXIT | Result |
|---|---:|---|
| `cmake --build build --target LufsMeterTest LoudnessReportTest MeterTapTest -j4 > .../5108.r1/logs/build-1.log 2>&1` | 0 | built |
| `cd build/tests && ctest -R '^(LufsMeterTest\|LoudnessReportTest\|MeterTapTest)$' --output-on-failure > .../5108.r1/logs/test-1.log 2>&1` | 0 | `100% tests passed out of 3` (log read by me) |

Delegation: Haiku ran the build and ctest commands above and reported the exit codes. I re-read the tail of both logs.
