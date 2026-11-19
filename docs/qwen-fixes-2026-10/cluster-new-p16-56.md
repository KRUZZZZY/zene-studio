# Cluster new-p16-56 - fractional delay read one frame late

## Verdict: FIXED

The four delay setters in `include/Delay.h` used `ceil()` for the whole-frame tap,
so a fractional delay read one frame late. In the base (`fa4a5f2e78`) those lines
were `include/Delay.h:112` (CombFeedback), `:186` (CombFeedfwd), `:262` and
`:265` (CombFeedbackDualtap delays 1/2), and `:345` (AllpassDelay), each
`m_delay = static_cast<int>( ceil( delay ) );`.

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

The p1 trace (`logs/005-delay-trace.out`, analysis in `logs/006-p1-matrix.md`)
showed the bug empirically: gain 0, impulse 1 at frame 0, expected frames 1,2
for d=1.5. The old code produced
`feedback 1.5: 0.000 0.000 0.500 0.500 0.000 0.000` (frames 2,3),
`feedback 2.75: 0.000 0.000 0.000 0.250 0.750 0.000` (frames 3,4), and
`feedback 0.5: 0.000 0.500 0.500 0.000 0.000` (effective 1.5 frames).

## Commands and exit codes

| # | Command (abbreviated) | EXIT | Result |
|---|---|---|---|
| 007 | build DeviceDspTest (regression test added) | 0 | built target DeviceDspTest (`logs/007-build-red.log`) |
| 008 | `ctest -R DeviceDspTest` on the product change reverted | 1 | `Totals: 40 passed, 9 failed`; `381 - DeviceDspTest (Failed)` (`logs/008-test-red.log`) |
| 009 | build after the Delay.h fix | 0 | built target DeviceDspTest (`logs/009-build-green.log`) |
| 010 | `ctest -R DeviceDspTest` restored | 0 | `100% tests passed out of 2` (`logs/010-test-green.log`) |
| 011 | delay-family tests | 0 | `100% tests passed out of 3` (`logs/011-test-delay-family.log`) |
| 020 | build with the non-test hunks reversed | 0 | built target DeviceDspTest (`logs/020-build-reversed.log`) |
| 021 | `ctest -R DeviceDspTest` reversed | 1 | `Totals: 40 passed, 9 failed`; `381 - DeviceDspTest (Failed)` (`logs/021-test-reversed-red.log`) |
| 022 | build restored | 0 | built target DeviceDspTest (`logs/022-build-restored.log`) |
| 023 | `ctest -R DeviceDspTest` restored | 0 | `100% tests passed out of 1` (`logs/023-test-restored-green.log`) |
| 024 | `python3 feature_check.py <worktree>` | 0 | all 8 gates PASS (`logs/024-gates.log`) |

The red logs end with `The following tests FAILED: 381 - DeviceDspTest (Failed)`
and `Errors while running CTest`, so CTest returned nonzero. The green logs show
`100% tests passed out of N`. The fixture in the failure output is the concrete
evidence: `combFeedbackFractionalHalfDelay() ... frame 1 got 0 want 0.5;
frame 3 got 0.5 want 0` (`021`), i.e. the old effective delay 2.5. The reverse
run reverts only the non-test hunks, so the same assertion fails again and passes
when restored.

## Behaviour change

A delay below 1 frame now clamps to exactly 1 frame (`splitDelay` clamps before
`floor`). Previously `d=0.5` with `ceil` gave an effective 1.5 frames. Integer
delays are unchanged: `d=1` gives whole 1, fraction 1.0; `d=2` gives whole 2,
fraction 1.0, both bit-identical to the old paths.

## Other findings

- Only `CombFeedback::setDelay` has an `isfinite`/range guard; the other three
  setters do not.
- `include/Delay.h` uses `memset` but lacks `#include <cstring>`.
- No plugin uses `include/Delay.h`; the audit is `logs/002-delay-include-audit.txt`.

## Self-check

- **Regression test fails reverted and passes restored.** Reverted run EXIT 1
  with 9 failures (`logs/021-test-reversed-red.log`, `Totals: 40 passed, 9
  failed`); restored run EXIT 0 with `100% tests passed out of 1`
  (`logs/023-test-restored-green.log`). The first red run before the fix also
  exited 1 with the same 9 failures (`logs/008-test-red.log`).
- **Deterministic: no wall-clock timing, sleeps or races.** The tests render a
  fixed impulse through the device DSP and compare sample frames; delays come
  from literal values (1.25, 1.5, 2.75, 0.5) and the response arrays shown in
  the failure output are exact, not timed (`logs/005-delay-trace.out`).
- **Fix is at the cause and complete.** All five `ceil` sites are replaced by the
  single helper (`include/Delay.h:44`, call sites 127/200/275/276/355); the
  `update()` bodies were not changed, so the existing lerp arithmetic remains the
  one definition for every delay device.
- **No collateral damage.** Integer delays are unchanged by construction
  (`floor(1)=1`, fraction 1.0; `floor(2)=2`, fraction 1.0) and the integer cases
  in `005-delay-trace.out` are unchanged (`feedback 1.0: 0.000 1.000 ...`,
  `feedback 2.0: 0.000 0.000 1.000 ...`). The only altered edge is intentional:
  sub-1-frame delays clamp to 1 frame, recorded above. No saved-data or project
  format is touched; the change is header-only DSP arithmetic.
- **Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE.** The old `ceil` lines
  at `include/Delay.h:112/186/262/265/345` in base `fa4a5f2e78` and the failing
  trace in `logs/005-delay-trace.out` show the defect in the base code.
