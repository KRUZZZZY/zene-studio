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

## Self-check

- Regression test fails without the fix and passes with it: not re-run this session, because I changed no product code and the verdict is ALREADY-FIXED. The reverse/forward proof (RED exit 8 with the product patch reversed, GREEN exit 0) is in `docs/qwen-fixes-2026-10/cluster-new-p9-233.md`. This session the three tests pass on the current code (ctest EXIT=0, `100% tests passed out of 3`, `logs/test-1.log`).
- Deterministic: the tests feed generated sine buffers (`makeSine`, `planarChannel`) at a fixed `SampleRate` straight into `LufsMeter` and read the result. There is no clock, sleep or thread.
- Complete, siblings checked: `binIndex()` has only two callers (`LufsMeter.cpp:232` integrated histogram, `:244` short-term histogram). Both histograms are sized `BinCount + OverflowBinCount` (`LufsMeter.h:275,280`). Every scan loop (`gatedLoudness` 323/334, `shortTermRelativeGate` 387, `shortTermSpreadAbove` 403, `shortTermBinAtCount` 430) runs to `LastBinIndex`, and `binLoudness()` (305-315) handles overflow indices.
- No collateral damage: this session changed no product code, only the report. The bins up to +5 LUFS are unchanged, and no saved data holds the histogram.
- ALREADY-FIXED evidence: `src/core/LufsMeter.cpp:290-303`, `include/LufsMeter.h:195-203`, and the tests at `tests/src/core/LufsMeterTest.cpp:290,313,327` (see Evidence above).
