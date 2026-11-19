FIXED

# Cluster new-p14-273: pitch-preserving output ignored the clip's sample amplification

Product commit: 540f980dc7 (fix(stretch): apply sample amplification to pitch-preserving output).
Baseline HEAD for this round: 9ee79d903a (`L/head-01-baseline.txt`).
Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-273.r2/logs/` (called L below). This report cites round r2 only.

## 1. Decision and evidence

Decision: `renderPreservingPitch` multiplies the frames the stretcher wrote by the clip's sample amplification, the same gain `Sample::render` applies on the resample path.

- Single caller: `src/core/SamplePlayHandle.cpp:596` calls `renderPreservingPitch`. The definition is at `src/core/SamplePlayHandle.cpp:704`, and no other call site exists in `src/` or `include/` (grep).
- Fix block: `src/core/SamplePlayHandle.cpp:741-745` reads `m_sample->amplification()` and scales `dst[0..written)` when the gain is not 1.0. Line 746 zero-fills the unwritten tail, and the fix leaves it alone.
- Resample-path gain: `src/core/Sample.cpp:243-245` multiplies each frame by `m_amplification`. The fix copies this behaviour.
- Gain sibling: `src/core/TransientRepeat.cpp:284` multiplies each frame by `source.amplification`. `renderTransientRepeat` was already correct and is unchanged.
- Atomic read: `include/Sample.h:123` `amplification()` is a relaxed atomic load.
- The stretcher input stays raw, so WSOLA and Rubber Band share one multiply.

## 2. Test matrix

Regression slot: `aPreservedPitchClipHonoursTheSampleAmplification`, `tests/src/tracks/SampleClipStretchTest.cpp:609-654`.
It renders a 2x-warped clip in PreservePitch mode at gains 1.0, 0.5 and 2.0 (one clip, gain changed between renders). It measures the 440 Hz and 660 Hz levels (`kLowTone` line 79, `kHighTone` line 80) over the middle half of each render. It checks the gain 1.0 baselines first (440 Hz in 0.35-0.65, 660 Hz in 0.20-0.40), then requires gain 0.5 at 642 and 645 and gain 2.0 for both tones to match within 5%.

| Area | Coverage in this round | Evidence |
|---|---|---|
| Normal gains 1.0 / 0.5 / 2.0 | Covered by the slot. The per-gain values appear in the r2 RED log only; the r2 GREEN logs hold the ctest summary only. | `L/test-02-red.log` rows 23-25: 440 Hz 0.4992 and 660 Hz 0.2990 at all three gains, which is the unfixed state. |
| Failure (fix absent) | Covered: the RED run fails at the gain 0.5 check, line 642. | `L/test-02-red.log`: `FAIL!` "gain 0.5 at 440 Hz is 1 of gain 1.0; expected 0.5"; `Totals: 9 passed, 1 failed`. |
| Reversal | `git apply -R` removes the fix. | `L/exit-02-reverse.txt` EXIT=0; `L/diff-02-reversed.txt`: 8 deletions in `src/core/SamplePlayHandle.cpp`. |
| Double-gain mutant | Covered: the mutant fails at the same check with ratio 0.25. | `L/test-03-mutant.log`: "gain 0.5 at 440 Hz is 0.25 of gain 1.0". |
| Tail / exhaustion | Not tested. The code scales only the `written` frames, and line 746 zero-fills the tail. No test checks a window that runs out. | Code reading only. |
| Reconfiguration between renders | Covered in part: the slot changes the gain between three renders of the same clip. A change made in the middle of a render is not tested. | Slot loop, `tests/src/tracks/SampleClipStretchTest.cpp:609-654`. |
| Audio-thread constraint | Argued (section 4). The stretched-render allocation test passes in the RED and mutant runs. | `L/test-02-red.log` and `L/test-03-mutant.log`: `aStretchedClipRendersWithoutAllocating` PASS. |
| Unity-gain behaviour | Covered: unchanged renders and the project round trip pass in the RED and mutant runs. | `aClipWithoutARateChangeRendersIdenticallyInBothModes` (line 359) and `theStretchModeRoundTripsThroughTheProjectFile` (line 391) PASS in `L/test-02-red.log`. |
| Linux / GUI-less build | Ran with ctest on this Linux machine (`build/tests`). A GUI-less build was not separately verified. | `L/test-02-red.log` header: `Test project .../kads-new-p14-273/build/tests`. |
| Four test families | 4 of 4 pass. | `L/test-03-families.log`: AudioStretcherTest (1/4), SampleClipStretchTest (2/4), TakeLaneCompPlaybackTest (3/4), SampleClipRubberBandTest (4/4), `100% tests passed out of 4`. |

## 3. Commands and exit codes

Command lines for the rows from the baseline build through the gates were not recovered. No record of them exists in the r2 run folder: `duet/14-worker/session.log` covers only the p5 session, and the build and test logs hold tool output, not command text. Those rows give the log and exit file only, and their exit codes are read from the exit files. The determinism grep was rerun in p5 and its exact command is given below.

| Step | Command | Log | Exit file | Exit | Key output |
|---|---|---|---|---|---|
| Baseline build | not recovered | `L/build-01-baseline.log` | `L/exit-01-baseline.txt` | 0 | build |
| Baseline test | not recovered | `L/test-01-baseline.log` | `L/exit-01-baseline-test.txt` | 0 | 1/1 passed |
| Status at baseline | not recovered | - | `L/exit-01-status.txt` | 0 | `L/status-01-baseline.txt` |
| RED build (fix absent) | not recovered | `L/build-02-red.log` | `L/exit-02-build-red.txt` | 0 | build |
| RED test | not recovered | `L/test-02-red.log` | `L/exit-02-red.txt` | **8** | 9 passed, 1 failed; ratio 1 of 1.0 at 440 Hz |
| Reverse patch (`git apply -R`) | not recovered | `L/diff-02-reversed.txt` | `L/exit-02-reverse.txt` | 0 | 8 deletions |
| Generate `fix.patch` | not recovered | - | - | - | `L/fix.patch` (8 added lines) |
| Apply `fix.patch` (step 02) | not recovered | - | `L/exit-02-patch.txt` | 0 | `L/fix.patch` (8 added lines) |
| GREEN build | not recovered | `L/build-03-green.log` | `L/exit-03-build-green.txt` | 0 | build |
| GREEN test | not recovered | `L/test-03-green.log` | `L/exit-03-green.txt` | **0** | 1/1 passed |
| Mutant build | not recovered | `L/build-03-mutant.log` | `L/exit-03-build-mutant.txt` | 0 | build |
| Mutant apply / reverse | not recovered | - | `L/exit-03-mutant-patch.txt`, `L/exit-03-mutant-reverse.txt` | 0, 0 | `L/mutant.patch` |
| Mutant test (gain applied twice) | not recovered | `L/test-03-mutant.log` | `L/exit-03-mutant.txt` | **8** | ratio 0.25 |
| Re-apply fix | not recovered | - | `L/exit-03-reapply.txt` | 0 | `L/status-03-reapply.txt` |
| GREEN again build | not recovered | `L/build-03-green2.log` | `L/exit-03-build-green2.txt` | 0 | build |
| GREEN again test | not recovered | `L/test-03-green2.log` | `L/exit-03-green2.txt` | 0 | 1/1 passed |
| Four families | not recovered | `L/test-03-families.log` | `L/exit-03-families.txt` | 0 | 4/4 passed |
| Determinism grep (r2, p3) | pattern `sleep\|QThread\|std::thread\|chrono\|rand\(\|time\(\|QTime\|clock\|QElapsed` on `tests/src/tracks/SampleClipStretchTest.cpp`; exact command not recovered | `L/grep-03-nondeterminism.txt` | (in the file) | `GREP_EXIT=1` | no matches |
| Determinism grep (p5 rerun) | `grep -nE 'sleep\|QThread\|std::thread\|chrono\|rand\(\|time\(\|QTime\|clock\|QElapsed' tests/src/tracks/SampleClipStretchTest.cpp > L/grep-05-nondeterminism.txt; echo GREP_EXIT=$? >> L/grep-05-nondeterminism.txt` | `L/grep-05-nondeterminism.txt` | (in the file) | `GREP_EXIT=1` | no matches |
| Gates | not recovered | `L/gates-04.log` | `L/exit-04-gates.txt` | 0 | 8 PASS, `RESULT: PASS` |

The RED, mutant and GREEN codes are 8, 8 and 0, matching `L/exit-02-red.txt`, `L/exit-03-mutant.txt` and `L/exit-03-green.txt`.

Determinism grep, rerun this session on the worktree test file with the call-level pattern `sleep|QThread|std::thread|chrono|rand\(|time\(|QTime|clock|QElapsed`: no matches, exit 1. A looser pattern matches comments such as "time stretch", so it is not used.

## 4. Realtime argument

`renderPreservingPitch` runs on the audio thread, inside `SamplePlayHandle::play`. The change adds one relaxed atomic float load (`include/Sample.h:123`), one comparison with 1.0, and a loop of at most `frames` float multiplies over a buffer the stretcher has already filled. It takes no lock, allocates nothing, adds no shared mutable state, and blocks on nothing. The load is relaxed, so it adds no ordering requirement on the audio thread. This is an argument from reading the code. No TSan or realtime profiler was run.

## 5. Collateral analysis

- No save-format field, limit or check is added. The commit adds 8 lines to `src/core/SamplePlayHandle.cpp`, 50 test lines and 1 registration row (`git show --stat 540f980dc7`: 3 files, 59 insertions, 0 deletions).
- The multiply is skipped when the gain is exactly 1.0 (`if (gain != 1.0f)`), so unity-gain renders are unchanged.
- The one intended behaviour change: a pitch-preserved clip with a non-unity gain now renders at that gain. Before, it ignored the gain. Projects saved with such a clip will sound quieter or louder than before. Not verified on a real saved project.
- Callers and siblings: the only caller is `SamplePlayHandle.cpp:596`; the sibling `renderTransientRepeat` already applies the gain (`TransientRepeat.cpp:284`).

## 6. Gate caveats

- `L/gates-04.log` shows 8 PASS lines. The `complexity` and `file-length` lines read "no new item in your files". The log does not show that lizard ran, so the complexity gate may pass without measuring anything.
- No lizard figures are repeated this round. Earlier CCN numbers from round r1 were not re-measured.

## 7. Unverified

- No TSan run. The realtime point in section 4 is an argument.
- The r2 GREEN logs hold only the ctest summary, so the per-gain GREEN ratios are not in the r2 logs. The GREEN pass is shown by exit codes only.
- No tail or exhaustion test, and no test for a gain changed in the middle of a render.
- The `SampleClipRubberBandTest.cpp` file has 0 references to amplification (grep). The Rubber Band branch is covered only by the shared multiply at `SamplePlayHandle.cpp:741-745` and by the 4/4 family run.
- GUI-less build not verified separately.

## Self-check

`submit_check.py` was run once on this worktree before this report was written: `L/submit-check-05-pre.log`, EXIT=0, 5 PASS, `RESULT: PASS - submit`. The five questions it printed are answered below.

1. **The regression test fails with the product change reverted and passes with it (exit codes).** Reverted: `L/exit-02-red.txt` is EXIT=8 and `L/test-02-red.log` shows `FAIL!` at line 642 with `Totals: 9 passed, 1 failed`. Reverted with `git apply -R`: `L/exit-02-reverse.txt` EXIT=0 and `L/diff-02-reversed.txt` 8 deletions. Fixed: `L/exit-03-green.txt` is EXIT=0 and `L/exit-03-green2.txt` is EXIT=0 after the re-apply. The slot is the only failing test in the RED and mutant runs.

2. **The test is deterministic (name the seam or injected values).** The determinism grep (section 3) finds no sleep, thread, clock or random call in `tests/src/tracks/SampleClipStretchTest.cpp` (`L/grep-05-nondeterminism.txt` GREP_EXIT=1, the p5 rerun with the exact command in section 3; `L/grep-03-nondeterminism.txt` GREP_EXIT=1, from r2). The inputs are fixed: the synthetic tones at `kLowTone` 440 Hz (line 79) and `kHighTone` 660 Hz (line 80), a fixed 2x warp (`applyTwoTimesWarp`, line 116, called at line 613), fixed gains 1.0 / 0.5 / 2.0, and levels measured over the middle half of each render.

3. **The fix is at the cause and complete for every caller and sibling.** The only caller of `renderPreservingPitch` is `SamplePlayHandle.cpp:596`. The gain that the resample path applies is at `Sample.cpp:243-245`, and the sibling `TransientRepeat.cpp:284` already applies it. The fix uses the same gain, and WSOLA and Rubber Band share one multiply. Not checked: `CompSlip.cpp` has 0 references to amplification (grep), so whether slipped comp reads need the gain is unverified (see Other findings).

4. **No collateral damage.** No save format, limit or check is changed (section 5). The unity-gain tests at lines 359 and 391 are in the RED and mutant runs and pass. The only behaviour change is a non-unity gain on a pitch-preserved clip, which is intended. Not verified on a real saved project.

5. **Verdict.** The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so the file:line evidence for those verdicts does not apply.

## Other findings

- The complexity gate passes with "no new item in your files" and no lizard output. It may pass without measuring anything (section 6).
- `src/core/CompSlip.cpp` has no reference to amplification. Whether slipped comp reads on the resample path carry the clip gain is unverified and may be a separate defect.
- The window-overrun and mute-cursor bugs were not touched.
- Registration row: `tests/upstream-modifications.txt:5685` records the change to `src/core/SamplePlayHandle.cpp`.
