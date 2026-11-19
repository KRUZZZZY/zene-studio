FIXED

# Cluster new-p14-273: pitch-preserving output ignored the clip's sample amplification

Product commit: 540f980dc7 (fix(stretch): apply sample amplification to pitch-preserving output).
Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-273.r1/logs/` (called L below).

## 1. Evidence

- The resample path applies the clip gain: `src/core/Sample.cpp:243-245` multiplies each frame by `m_amplification`.
- The resample path reaches `Sample::play` at `src/core/SamplePlayHandle.cpp:602`, inside the branch that also calls `m_compSlip->render` at line 607.
- The fix is in `renderPreservingPitch`, `src/core/SamplePlayHandle.cpp:738-745`. The stretcher still reads the raw input, so WSOLA and Rubber Band share one gain multiply. The gain is applied only to the `written` frames. The zero-filled tail is untouched (line 746).
- `amplification()` is a relaxed atomic load (`include/Sample.h:123`), so the new read adds no shared mutable state.
- Sibling: `renderTransientRepeat` already applies amplification (`src/core/TransientRepeat.cpp:284`, grep finds 3 references in that file). It is unchanged.
- `src/core/CompSlip.cpp` has 0 references to amplification (verified by grep). See section 6.

## 2. Test matrix

New slot: `aPreservedPitchClipHonoursTheSampleAmplification`, `tests/src/tracks/SampleClipStretchTest.cpp:609-654`.
- It renders a 2x-warped clip in PreservePitch mode at gains 1.0, 0.5 and 2.0.
- It measures the level at 440 Hz (baseline about 0.5) and at 660 Hz (baseline about 0.3) over the middle half of the render.
- It checks the baselines first, then requires the gain 0.5 and gain 2.0 ratios to match 0.5 and 2.0 within 5%.

Families run by ctest (4 of 4): AudioStretcherTest, SampleClipStretchTest, TakeLaneCompPlaybackTest, SampleClipRubberBandTest.

Mutants:
- Reversed fix: the slot fails with the expected message.
- Gain applied twice: ratio 0.25, the slot fails. This run is reported by the lead's packet (p3 worker); see section 3.
- Frames-span mutant: not run, because it is equivalent to the shipped code.

## 3. Commands and exit codes

Exit codes are listed only where a log or tool output shows them. Where the log has no exit line, it says so.

| Run | Log | Exit code | What the log shows |
|---|---|---|---|
| RED (fix absent) | `L/test-red.log` (build `L/build-1.log`) | not recorded in the log | `Test #636 ***Failed`; 9 PASS lines; 1 FAIL: `ratioMatches(lowLevel[1], lowLevel[0], 0.5)` returned FALSE, "gain 0.5 at 440 Hz is 1 of gain 1.0". Evidence rows show 0.4992 at all three gains. |
| Reversed fix | `L/test-reversed.log` (command: `ctest -R SampleClipStretchTest --output-on-failure`, header in `L/002-test-reversed.log`; build `L/build-3-reversed.log`, "Built target SampleClipStretchTest") | not recorded in the log | Same failure and same evidence rows as RED. `cmp` first differs at line 3. |
| Reapplied GREEN, ctest | `L/test-green.log` (header in `L/004-test-green.log`) | not recorded in the log | `100% tests passed out of 1`. |
| Reapplied GREEN, binary | `L/test-green-slot.log` (run as `./SampleClipStretchTest`) | not recorded in the log | `Totals: 10 passed, 0 failed, 0 skipped`. Gain 0.5 rows 0.2496 / 0.1495 (ratio 0.500). Gain 2.0 rows 0.9983 / 0.5981 (ratio 2.00). |
| Four families | `L/test-families.log` (header in `L/005-test-families.log`) | not recorded in the log | `100% tests passed out of 4`. |
| Fresh re-check at HEAD (this report) | `duet/14-worker/tmp/001-green-recheck-p5.log` under the run folder | **0** (quiet-run) | `ctest -R SampleClipStretchTest`: `100% tests passed out of 1`. |
| Mutant (gain applied twice) | packet says `duet/06-worker/tmp` | not verified | That path does not exist under the run folder, so this result is **unverified here**. |
| Gates | `L/gates.log` | per-gate exit 0 on all 8 lines; the runner's own exit is not printed | 8 of 8 gates PASS; `RESULT: PASS`. |
| Gates without lizard | `L/gates-nolizard.log` | per-gate exit 0 on all 8 lines; runner exit not printed | Same 8 PASS lines and `RESULT: PASS`. |
| submit_check | `L/submit-check.log` (run after the report commit `8d953131d2`) | **0** (`EXIT=0`) | 5 PASS (commits, clean tree, report, tests, gates); no FAIL; `RESULT: PASS - submit`; 5 `?` questions, answered in the Self-check section. |

The packet's RED and reversed exit code of 8 does not appear in any log in the run folder, so it is not claimed here.

## 4. Gate caveats

- The packet says the complexity gate was measured with lizard from a temporary venv, since deleted. The `complexity` line in `L/gates.log` reads "no new item in your files", which is identical to `L/gates-nolizard.log`. Neither log mentions lizard, so the logs do not show that the gate measured anything.
- The packet says `renderPreservingPitch` CCN went from 7 to 9. No CCN values appear in these logs, so this is unverified here.

## 5. Unverified

- No TSan on this machine. The packet's argument is that the change adds one relaxed atomic float load and no shared state, so it adds no race. That is an argument, not a TSan run.
- The `m_rubberBand` branch has no amplification test. `tests/src/tracks/SampleClipRubberBandTest.cpp` has 0 references to amplification (verified by grep). That branch relies on the shared code after the ternary, `SamplePlayHandle.cpp:738-745`.
- The frames-span mutant was not run, because it is equivalent.
- The `m_compSlip->render` call at line 607 is in the resample branch; the packet says it is in that branch only. The whole branch structure was not re-read for this report.

## 6. Other findings

- The complexity gate passes vacuously when lizard is not installed. `L/gates-nolizard.log` shows the PASS line with no measurement.
- `src/core/CompSlip.cpp` has no reference to amplification. Whether slipped comp reads on the resample path carry the clip gain is unverified, and may be a separate defect.
- The window-overrun and mute-cursor bugs were not touched.

## Self-check

These answer the five `?` questions that `submit_check.py` printed for this cluster. The first check passed with no FAIL lines (`L/submit-check.log`, `EXIT=0`).

1. **The regression test fails with the product change reverted and passes with it.** Reverted: `L/test-red.log` and `L/test-reversed.log` both show `Test #636 ***Failed`, 9 PASS lines and 1 FAIL line, "gain 0.5 at 440 Hz is 1 of gain 1.0; expected 0.5". Fixed: `L/test-green-slot.log` shows `Totals: 10 passed, 0 failed`, including `aPreservedPitchClipHonoursTheSampleAmplification`. A fresh run at HEAD gave exit 0 (section 3). The RED and reversed logs do not record an exit code, so no exit code is claimed for those two runs.

2. **The test is deterministic.** `tests/src/tracks/SampleClipStretchTest.cpp` has no sleep, thread, clock or random call (grep for `sleep|QThread|std::thread|chrono|rand|time` finds nothing). Its inputs are fixed: synthetic tones at 440 Hz and 660 Hz at `m_rate`, a fixed 2x warp (`applyTwoTimesWarp`), gains 1.0, 0.5 and 2.0, and levels measured over the middle half of the render. The baseline is the same in every log: 0.4992 at 440 Hz and 0.2990 at 660 Hz in RED, reversed and green runs.

3. **The fix is at the cause, and complete only for the pitch-preserving path.** `renderPreservingPitch` has one caller, `src/core/SamplePlayHandle.cpp:596`. Its sibling `renderTransientRepeat` (line 592) already applies the gain (`src/core/TransientRepeat.cpp:284`). The resample path (`Sample::play`, line 602) applies the gain in `src/core/Sample.cpp:243-245`. The new code uses that same gain, and WSOLA and Rubber Band share one multiply. Not shown: `CompSlip.cpp` has 0 references to amplification, and whether its reads need the gain has not been checked (section 6). So "complete" holds only for the paths listed here.

4. **No collateral damage.** Commit `540f980dc7` is additive: `git show --stat` gives 3 files, 59 insertions and 0 deletions (`SamplePlayHandle.cpp` +8, `SampleClipStretchTest.cpp` +50, `tests/upstream-modifications.txt` +1). It adds no save-format field and no limit or check. The multiply is skipped when the gain is exactly 1.0 (`if (gain != 1.0f)`). Unity-gain renders are covered by `aClipWithoutARateChangeRendersIdenticallyInBothModes` (line 359) and `theStretchModeRoundTripsThroughTheProjectFile` (line 391), both PASS in `L/test-green-slot.log`. One behaviour change is intended: a pitch-preserved clip with a non-unity gain now renders at a different level than before. No saved project was loaded to confirm this.

5. **Verdict.** The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so this item does not apply.
