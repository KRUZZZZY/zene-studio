FIXED
new-p10-123: FIXED
new-p20-45: FALSE-POSITIVE
new-p21-153: FALSE-POSITIVE

# Cluster new-p10-123 / new-p20-45 / new-p21-153 (same-file batch in src/core/ExportNoiseShaper.cpp)

Same-file batch of three noise-shaper findings. One commit per bug (see Self-check).
Build/test logs: /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-123.r2/logs/
(numbered; commands reproduce them).

## new-p10-123: FIXED

REAL. A NaN sample poisoned the rest of the channel:

- `include/AudioEngine.h:369-380` - `AudioEngine::clip` returns NaN unchanged (both
  `s > 1` and `s < -1` guards are false for NaN), so `clip(NaN)` is NaN.
- Pre-fix `src/core/ExportNoiseShaper.cpp:55-60`: `target = clip(sample) * 32767` is NaN,
  `lround(NaN)` yields the long minimum and `std::clamp(..., -32768, 32767)` pins it to
  -32768, and `(float)rounded - shaped` is NaN, so `std::clamp(NaN, -2, 2)` stores NaN in
  the channel's error history `m_error[channel]`. Every later sample then computes a
  NaN `shaped`, NaN `dithered`, and quantises to -32768 again. Proven locally: a standalone
  probe of the compiled libm call printed `lround(nan)=-9223372036854775808` and the next
  simulated sample also rounded to -32768 despite target 0.

Fix (`src/core/ExportNoiseShaper.cpp:52-73`, `ExportNoiseShaper::quantise16`):

- `clipped = AudioEngine::clip(sample)`; if `!std::isfinite(clipped)`, reset that channel's
  history to `{0,0,0}` and return silence (0). No dither, no feedback write.
- Defensive: also reset a channel history that already holds a non-finite element, so a
  pre-fix-corrupted history can never feed NaN back in.
- Infinities are deliberately unaffected: `clip(+-Inf) = +-1` is finite, so they take the
  normal clipped path exactly as before. Checking the *clipped* value (rather than
  `std::isfinite(sample)`) is what preserves them; a raw-sample finite check would have
  redefined Inf to silence.
- The frame path (`quantiseFrames`, lines 83-96) routes through `quantise16`, so it
  inherits the guard; the out-of-range-channel silence precedent (line 51) uses the same
  "silence, no state advance" rule.

Regression test: `tests/src/core/ExportNoiseShaperTest.cpp`
`aNonFiniteSampleDoesNotPoisonLaterSamples` - deterministic, no sleeps/races. It drives
`quiet_NaN()` and asserts the NaN is 0, that Inf still equals a fresh reference, that 64
samples at 0.25 after the NaN reproduce a fresh run sample-for-sample (victim vs reference
shaper), and that a NaN frame in `quantiseFrames` is silence with later frames matching a
fresh reference. Injecting a fresh run rather than an injected timestamp keeps it
deterministic; dither draws are the only randomness and both shapers draw them in lockstep.

RED/GREEN (step 4b, product patch only):

- RED: reverse the product change, rebuild, run. Build `cmake --build build --target ExportNoiseShaperTest` EXIT=0 (`build-07-red-p10.log`), test `ctest -R '^ExportNoiseShaperTest$' --output-on-failure` EXIT=8 (`test-07-red-p10.log`): 11 passed, 1 failed - `aNonFiniteSampleDoesNotPoisonLaterSamples` at the `victim.quantise16(nan, 0) == 0` assertion. Detail log `test-07-red-p10-detail.log` EXIT=1.
- GREEN: restore the product patch, rebuild, run. Build EXIT=0 (`build-08-green-p10.log`), test EXIT=0 (`test-08-green-p10.log`): 12/12 passed. Re-confirmed at the final tree (`run-11-green-restored.log` EXIT=0, 12 passed).
- Family GREEN: `ctest -R 'ExportWavDitherTest|ExportDitherTest|ExportNoiseShaper'` EXIT=0 (`ctest-06-family.log`): 3/3 passed (12 + ExportDitherTest + ExportWavDitherTest). Baseline family BEFORE the test was green after building those targets (`test-01-baseline.log` only covered NoiseShaper; the dither family was verified after the NaN fix).

No new allocation/lock/log on the audio path: the guard is two finite checks and an
array assign - no heap, no mutex, `noexcept` preserved.

## new-p20-45: FALSE-POSITIVE

The finding claims the feedback error `rounded - shaped` (src/core/ExportNoiseShaper.cpp:79)
wrongly feeds the dither offset back, and should be `rounded - dithered`. That is a
convention change, not a bug in this codebase:

- `include/ExportNoiseShaper.h:5-21` documents the NTF 1 - 1.623 z^-1 + 0.982 z^-2 - 0.109 z^-3
  with -12 dB at DC and +11.4 dB at Nyquist, and the introduction commit `927e368723`
  records the *measured* spectrum of the current code: -8.4 dB low band / +9.0 dB high band
  versus flat TPDF. The existing test `theNoiseMovesOutOfTheLowBand`
  (tests/src/core/ExportNoiseShaperTest.cpp) requires `lowShaped < 0.5 * lowFlat` (-3 dB).
- Algebra: with `shaped = target - H(e_prev)`, the current stored error is
  `rounded - shaped = (dither + quantiser noise)`, so the NTF shapes the *total* noise,
  which is exactly the documented spectrum. The proposed `rounded - dithered` shapes only
  the quantiser residue and leaves the white TPDF dither unshaped, so the delivered
  spectrum cannot approach the documented -12 dB at DC.
- Trial of the proposed fix (working tree only, then reverted): building with
  `rounded - dithered` yields `EVIDENCE low band: flat 0.0079718 shaped 0.005598 (-1.5 dB)`
  and `theNoiseMovesOutOfTheLowBand` FAILS (`lowShaped < 0.5 * lowFlat` false) - log
  `test-09-trial-p20.log` EXIT=3. The fix therefore breaks the documented behaviour; the
  existing spectrum is the intended design.

Pinning test added (same test class):
`theFeedbackShapesTheDitherOffsetAsDocumented` replays the identical float arithmetic
(coefficients, seeded `ExportDither`, clamps) for the 48k-sample `quietSine` and requires
bit-exact equality with `quantise16`; it also counts divergence from the alternative
convention (>0 samples), so the test provably distinguishes the conventions. It PASSES on
the current code (`test-02-red.log` / `run-11-green-restored.log`).

## new-p21-153: FALSE-POSITIVE

The finding says rounding (`std::lround`, src/core/ExportNoiseShaper.cpp:78) breaks the
"same scale and clip" contract versus truncating `AudioDevice::convertToS16`. Both
dithered paths round, by design:

- `src/core/audio/AudioFileWave.cpp:63-77` - `sampleRoundedToS16` does
  `std::lround(AudioEngine::clip(value) * OUTPUT_SAMPLE_MULTIPLIER)`, with the documented
  reason: `convertToS16` truncates toward zero, which quantises every sub-LSB dither offset
  straight back out. The plain dithered render therefore rounds (`convertDitheredToS16`,
  ibid. line 221), and the shaper rounds the same way. `AudioDevice::convertToS16`
  (src/core/audio/AudioDevice.cpp:200,215) is the *undithered* path; it is not the correct
  comparator, and the shaper already matches it on scale and clip (lines 60, 74).
- Trial of the proposed fix (working tree only, then reverted): `static_cast<long>(dithered)`
  (truncation) FAILS `silenceIsDitheredButStaysSmall` with `nonZero > 0` false and
  degrades the low band to -4.1 dB (`test-10-trial-p21.log` EXIT=3) - truncation kills the
  dither, exactly as the AudioFileWave comment predicts. The finding's own alternative
  ("document and test the rounding choice") is therefore the right closure.

Pinning test added (same test class): `theQuantiserRoundsLikeTheDitheredPath` replays the
shaper arithmetic with `std::lround` and requires bit-exact equality over the 48k `quietSine`,
and counts truncation divergence (>0 samples), proving the test distinguishes rounding from
truncation. It PASSES on the current code.

## Self-check

- The regression test FAILS with your product change reverted and PASSES with it (exit codes). new-p10-123: RED `ctest -R '^ExportNoiseShaperTest$'` EXIT=8 (11 passed, 1 failed, `aNonFiniteSampleDoesNotPoisonLaterSamples`) and direct binary EXIT=1 (`test-07-red-p10-detail.log`); GREEN EXIT=0, 12 passed (`test-08-green-p10.log`, re-confirmed `run-11-green-restored.log`). Revert/restore via `git diff -- src/core/ExportNoiseShaper.cpp > fix-p10.patch && git apply -R fix-p10.patch` (EXIT=0) … `git apply fix-p10.patch` (EXIT=0).
- The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses). Deterministic: fixed `DefaultSeed` dither, fixed 0.25/quietSine input vectors, lockstep victim-vs-reference shaper runs; no time or threads.
- The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent. `quantiseFrames` (the only other caller of the changed arithmetic) inherits the guard via `quantise16`; sibling Inf/+-clip behaviour preserved (Inf tested). No other call sites quantise via `ExportNoiseShaper` (`rg`).
- No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it. Finite and Inf inputs produce byte-identical outputs to before (guard is NaN-only in effect; Inf test asserts this); no save-format change. ExportDither/ExportWavDither family green (3/3).
- If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it. new-p20-45 cites `ExportNoiseShaper.h:5-21` NTF promise + `927e368723` measured -8.4 dB + trial `test-09` -1.5 dB FAIL; new-p21-153 cites `AudioFileWave.cpp:63-77,221` rounding rationale + `AudioDevice.cpp:200,215` as the undithered comparator + trial `test-10` dither-kill FAIL.
- Registration and gates: `feature_check.py` RESULT PASS (EXIT=0, `feature-check-01.log`). No new files, so no `fork-sources.txt`/`upstream-modifications.txt` entries needed. No new control commands. Complexity/file-length: no new items in changed files.

## Commits and gates

- `git add src/core/ExportNoiseShaper.cpp tests/src/core/ExportNoiseShaperTest.cpp docs/qwen-fixes-2026-10/cluster-new-p10-123.md` with messages naming the clusters (one per bug; see `git log`).
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-123` EXIT=0 (`feature-check-01.log`): 8/8 PASS.
- `ctest -R '^ExportNoiseShaperTest$'` from `build/tests` EXIT=0, 1/1, single Qt test case with 12 passed / 0 failed (`test-11-green-restored.log`); family `ctest -R 'ExportWavDitherTest|ExportDitherTest|ExportNoiseShaper'` EXIT=0, 3/3 (`ctest-06-family.log`).
