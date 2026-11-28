FIXED

# Cluster new-p16-56 - fractional delay read one frame late

Base `fa4a5f2e78`, fix `1cdfc2a6f8` (branch `bugs/qwen-new-p16-56`).

In the base, the four delay setters used `ceil()` for the whole-frame tap,
each `m_delay = static_cast<int>( ceil( delay ) );` at `include/Delay.h:112`
(CombFeedback), `:186` (CombFeedfwd), `:262` and `:265` (CombFeedbackDualtap
delays 1/2), and `:345` (AllpassDelay). A fractional delay therefore read one
frame late.

The fix replaces them with one helper, `detail::splitDelay`, at
`include/Delay.h:44`:

```
const double d = delay < 1.0 ? 1.0 : delay;
const double wholeFrames = std::floor( d );
whole = static_cast<int>( wholeFrames );
frac = 1.0 - ( d - wholeFrames );
```

Five call sites: `include/Delay.h:127` (CombFeedback), `:200` (CombFeedfwd),
`:275` and `:276` (Dualtap tap 1/2), `:355` (AllpassDelay). The `update()`
bodies are untouched, so the correct `lerp(buf[readPos], buf[readPos + 1],
frac)` now lands on frames `floor(d)` and `floor(d) + 1` instead of one later.

## Tests added

14 new test methods in `tests/src/core/DeviceDspTestParts3.h`, declared at
`tests/src/core/DeviceDspTest.cpp:490-492` and included at
`tests/src/core/DeviceDspTest.cpp:497`:

`combFeedbackFractionalHalfDelay`,
`combFeedbackFractionalQuarterDelay`,
`combFeedbackFractionalThreeQuarterDelay`,
`combFeedbackIntegerDelayUnchanged`,
`combFeedfwdFractionalHalfDelay`, `allpassFractionalHalfDelay`,
`combFeedbackDualtapFractionalDelays`,
`combFeedbackSubSampleClampedToOne`,
`combFeedbackReconfiguredFractionalDelay`,
`combFeedbackFractionalFeedbackPath`, `combFeedfwdFractionalDelays`,
`allpassFractionalDelays`, `combFeedbackRingWrapRamp`,
`combFeedfwdRingWrapRamp`.

## Commands and exit codes

All logs are in `~/.config/kads/zene/state/runs/new-p16-56.r2/logs/` and are
cited by their file in that directory. Every EXIT below is the value written
into the named log by that run.

| # | Step | EXIT | Result |
|---|---|---|---|
| 019 | fix.patch generation (`019-fix.patch`) | not recorded (patch artifact, no EXIT line) | patch for `include/Delay.h` (helper plus five call sites) |
| 020 | reverse check (`020-check-reverse.log`) | 0 | patch reverse-checks clean |
| 021 | apply reversed (`021-apply-reverse.log`) | 0 | reversed applied; one trailing-whitespace warning |
| 022 | build reverted header (`022-build-red.log`) | 0 | build succeeds with the old `ceil` behaviour |
| 023 | ctest `DeviceDspTest` reverted (`023-test-red.log`) | 8 | 40 passed, 13 failed, 7 skipped; 17 `FAIL!` lines across 13 methods |
| 024 | restore fix (`024-apply-restore.log`) | 0 | fix re-applied |
| 025 | build restored (`025-build-green.log`) | 0 | build succeeds |
| 026 | focused ctest (`026-test-green.log`) | 0 | `100% tests passed out of 2` |
| 027 | delay-family ctest (`027-family-green.log`) | 0 | `100% tests passed out of 3` |
| 028 | header backup (`028-Delay.h.bak`) | n/a (file artifact) | pre-mutation `Delay.h` saved for the mutant runs |
| 029 | sibling mutant, CombFeedback (`029-mut-feedback.log`) | 8 | 46 passed, 7 failed; 7 combFeedback tests fail |
| 031 | sibling mutant, CombFeedfwd (`031-mut-feedfwd.log`) | 8 | 50 passed, 3 failed; feedfwd tests fail |
| 033 | sibling mutant, Dualtap (`033-mut-dualtap.log`) | 8 | 52 passed, 1 failed; `combFeedbackDualtapFractionalDelays` |
| 035 | sibling mutant, Allpass (`035-mut-allpass.log`) | 8 | 51 passed, 2 failed; allpass tests fail |
| 037 | restored green (`037-restored-green.log`) | 0 | `100% tests passed out of 1` |
| 038 | detail mutant, clamp (`038-mut-clamp.log`) | 8 | 50 passed, 3 failed |
| 040 | detail mutant, fraction (`040-mut-frac.log`) | 8 | 44 passed, 9 failed |
| 042 | detail mutant, wrap (`042-mut-wrap.log`) | 8 | 43 passed, 10 failed |
| 044 | detail mutant, integer (`044-mut-integer.log`) | 8 | 47 passed, 6 failed |
| 046 | restored green (`046-restored-green.log`) | 0 | `100% tests passed out of 1` |

The 13 methods failing in 023, with the 17 `FAIL!` lines distributed over
them (`combFeedfwdFractionalDelays` and `allpassFractionalDelays` report
three assertion failures each, the rest one):

1. `combFeedbackFractionalHalfDelay`
2. `combFeedbackFractionalQuarterDelay`
3. `combFeedbackFractionalThreeQuarterDelay`
4. `combFeedfwdFractionalHalfDelay`
5. `allpassFractionalHalfDelay`
6. `combFeedbackDualtapFractionalDelays`
7. `combFeedbackSubSampleClampedToOne`
8. `combFeedbackReconfiguredFractionalDelay`
9. `combFeedbackFractionalFeedbackPath`
10. `combFeedfwdFractionalDelays`
11. `allpassFractionalDelays`
12. `combFeedbackRingWrapRamp`
13. `combFeedfwdRingWrapRamp`

`combFeedbackIntegerDelayUnchanged` is the one expected pass under the old
`ceil` code: integer delays were always read on the integer frame, so only
the fractional cases move.

Mutant detail, by named failing test:

- 029 (CombFeedback): `combFeedbackFractionalHalfDelay`,
  `...QuarterDelay`, `...ThreeQuarterDelay`, `...SubSampleClampedToOne`,
  `...ReconfiguredFractionalDelay`, `...FractionalFeedbackPath`,
  `...RingWrapRamp`.
- 031 (CombFeedfwd): `combFeedfwdFractionalDelays`,
  `combFeedfwdFractionalHalfDelay`, `combFeedfwdRingWrapRamp`.
- 033 (Dualtap): `combFeedbackDualtapFractionalDelays`.
- 035 (Allpass): `allpassFractionalDelays`, `allpassFractionalHalfDelay`.
- 038 (clamp removed): `combFeedbackSubSampleClampedToOne`,
  `combFeedfwdFractionalDelays`, `allpassFractionalDelays`.
- 040 (fraction term): `allpassFractionalDelays`,
  `combFeedfwdFractionalDelays`, `combFeedbackDualtapFractionalDelays`,
  `combFeedbackFractionalQuarterDelay`,
  `combFeedbackFractionalThreeQuarterDelay`,
  `combFeedbackIntegerDelayUnchanged`,
  `combFeedbackSubSampleClampedToOne`,
  `rejectedCombFeedbackDelaysPreserveThePreviousDelay`,
  `stereoDelayPrimitivesAdvanceOneCursorPerChannel`.
- 042 (ring wrap): `combFeedbackFractionalHalfDelay`,
  `combFeedbackFractionalQuarterDelay`,
  `combFeedbackFractionalThreeQuarterDelay`,
  `combFeedbackIntegerDelayUnchanged`,
  `combFeedbackSubSampleClampedToOne`,
  `combFeedbackReconfiguredFractionalDelay`,
  `combFeedbackFractionalFeedbackPath`, `combFeedbackRingWrapRamp`,
  `rejectedCombFeedbackDelaysPreserveThePreviousDelay`,
  `stereoDelayPrimitivesAdvanceOneCursorPerChannel`.
- 044 (integer path): `combFeedbackIntegerDelayUnchanged`,
  `combFeedbackSubSampleClampedToOne`, `combFeedfwdFractionalDelays`,
  `allpassFractionalDelays`,
  `rejectedCombFeedbackDelaysPreserveThePreviousDelay`,
  `stereoDelayPrimitivesAdvanceOneCursorPerChannel`.

After 046 the worktree was clean: `git status --porcelain` printed nothing and
`git diff` was empty at HEAD `896c9116f9`. The only change made by this report
is this file.

## Behaviour change

A delay below 1 frame now clamps to exactly 1 frame (`splitDelay` clamps before
`floor`). Previously `d=0.5` with `ceil` gave an effective 1.5 frames. Integer
delays are unchanged: `d=1` gives whole 1, fraction 1.0; `d=2` gives whole 2,
fraction 1.0, both bit-identical to the old paths.

## Other findings

- Only `CombFeedback::setDelay` has an `isfinite`/range guard; the other three
  setters do not.
- `include/Delay.h` uses `memset` but lacks `#include <cstring>`.

## Self-check

- **Reverted red, restored green, all with recorded exits.** 020 reverse
  check EXIT=0 and 021 apply-reverse EXIT=0 put the old header back under the
  new tests; 022 builds it (EXIT=0) and 023 runs it: EXIT=8, 40 passed / 13
  failed / 7 skipped, 17 `FAIL!` lines across the 13 methods listed above.
  024 restores the fix (EXIT=0); 025 builds (EXIT=0), 026 is green with EXIT=0
  on 2/2 and 027 with EXIT=0 on 3/3. The mutant rounds confirm the same
  direction: every mutant is EXIT=8 (029/031/033/035/038/040/042/044) and each
  restore is EXIT=0 (037, 046).
- **Deterministic.** Every test renders a fixed impulse through the
  header-only `StereoCombFeedback`, `StereoCombFeedfwd`, `StereoAllpassDelay`
  and `StereoCombFeedbackDualtap` and compares exact expected sample frames
  for literal delays (1.25, 1.5, 2.75, 0.5, and integer 1.0/2.0). There are no
  sleeps, wall-clock reads, or thread races; nothing is timing-dependent.
- **Cause and completeness.** All five `ceil` sites in the base
  (`include/Delay.h:112/186/262/265/345`) are replaced by the single helper at
  `:44`, called at 127/200/275/276/355. The `update()` bodies were not changed,
  so the existing lerp arithmetic remains the one definition for every delay
  device; no `ceil` remains in `include/Delay.h`.
- **No collateral damage.** Integer delays are unchanged by construction
  (`floor(1)=1`, fraction 1.0; `floor(2)=2`, fraction 1.0), and the integer
  cases pass in 026/027 and in the restored runs. The only altered edge is
  intentional: sub-1-frame delays clamp to 1 frame, covered by
  `combFeedbackSubSampleClampedToOne` and the 0.5 cases. The change is
  header-only DSP arithmetic and touches no saved data or project format.
- **Verdict FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The base commit
  `fa4a5f2e78` contains the five `ceil` lines above, and 023 (old header, new
  tests) fails on exactly the fractional assertions with the impulse shifted
  one frame late, while the restored header passes in 026/027 and 037/046.
