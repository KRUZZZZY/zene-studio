FIXED

# Cluster new-p16-56 - fractional delay read one frame late

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

## Commits

- `1cdfc2a6f8` fix(delay): fractional comb/allpass delays read from the ring
  write position, not one sample late - the header fix plus the regression test
  (`tests/src/core/DeviceDspTest.cpp`, `DeviceDspTestParts3.h`) and its
  `tests/fork-sources.txt` registration.
- `fb721f299a` docs(delay): correct splitDelay weight comment, test banner.
- `0b66147cb3` chore(tests): re-derive all-sources manifest for
  DeviceDspTestParts3.h - adds `tests/all-sources.txt` entry for the new part
  file after the first gate run failed on it (see below).

## Commands and exit codes

All runs from the worktree root unless the command says otherwise. Log prefixes
are `logs/`, i.e. `~/.config/kads/zene/state/runs/new-p16-56.r1/logs/`.

| # | Command | EXIT | Result |
|---|---|---|---|
| fix.patch | `git diff -- include/Delay.h > logs/fix.patch` | EXIT not recorded; result read from log | patch contains the four `ceil` sites and the helper; applied and reverted in 020-023 |
| 007 | build `DeviceDspTest` (build-tool invocation not echoed; output in `logs/007-build-red.log`) | EXIT not recorded; result read from log | `Built target DeviceDspTest` |
| 008 | `cd build/tests && ctest -R DeviceDspTest --output-on-failure > logs/008-test-red.log 2>&1` | 8 | `Totals: 40 passed, 9 failed`; `381 - DeviceDspTest (Failed)`; `Errors while running CTest` |
| 009 | build `DeviceDspTest` after the `Delay.h` fix (output in `logs/009-build-green.log`) | EXIT not recorded; result read from log | `Built target DeviceDspTest` |
| 010 | `cd build/tests && ctest -R DeviceDspTest --output-on-failure > logs/010-test-green.log 2>&1` | EXIT not recorded; result read from log | `100% tests passed out of 2` |
| 011 | delay-family ctest, regex `StereoDelay` or `MonoDelay` or `DelayControls` (verbatim below) | EXIT not recorded; result read from log | `100% tests passed out of 3` |
| 020 | build with the non-test hunks reversed (output in `logs/020-build-reversed.log`) | EXIT not recorded; result read from log | `Built target DeviceDspTest` |
| p4 reverse | `git apply -R logs/fix.patch` (reverts the `include/Delay.h` hunks, keeps the tests) | EXIT not recorded; result read from log | reverse applied; reversed build in 020 keeps the tests |
| 021 | `cd build/tests && ctest -R DeviceDspTest --output-on-failure > logs/021-test-reversed-red.log 2>&1` | 8 | `Totals: 40 passed, 9 failed`; `381 - DeviceDspTest (Failed)`; `Errors while running CTest` |
| 022 | build restored (output in `logs/022-build-restored.log`) | EXIT not recorded; result read from log | `Built target DeviceDspTest` |
| p4 restore | `git apply logs/fix.patch` | EXIT not recorded; result read from log | fix restored; restored build in 022 |
| 023 | `cd build/tests && ctest -R DeviceDspTest --output-on-failure > logs/023-test-restored-green.log 2>&1` | EXIT not recorded; result read from log | `100% tests passed out of 1` |
| 024 | `python3 feature_check.py <worktree> > logs/024-gates.log 2>&1` | EXIT not recorded; result read from log | 8/8 PASS, `RESULT: PASS` (see disclosure below) |
| 025 | `python3 submit_check.py <worktree> new-p16-56 > logs/025-submit.log 2>&1` | EXIT not recorded; result read from log | `RESULT: PASS - submit` |
| 026 | final-green ctest, regex `DeviceDspTest` or `AudioResamplerRatio` (verbatim below) | 0 | `100% tests passed out of 2`; logged `EXIT=0` - final green |
| 027 | final-family ctest, regex `StereoDelay` or `MonoDelay` or `DelayControls` (verbatim below) | 0 | `100% tests passed out of 3`; logged `EXIT=0` - final green |

Verbatim commands for the rows whose ctest regex contains `|`:

```
cd build/tests && ctest -R 'StereoDelay|MonoDelay|DelayControls' --output-on-failure > logs/011-test-delay-family.log 2>&1
cd build/tests && { ctest -R 'DeviceDspTest|AudioResamplerRatio' --output-on-failure; echo EXIT=$?; } > logs/026-test-final-green.log 2>&1
cd build/tests && { ctest -R 'StereoDelay|MonoDelay|DelayControls' --output-on-failure; echo EXIT=$?; } > logs/027-test-final-family.log 2>&1
```

The two red logs (008, 021) end with `The following tests FAILED: 381 -
DeviceDspTest (Failed)` and `Errors while running CTest`, which is why CTest
returned 8, not 1. The green logs show `100% tests passed out of N`; 026 and 027
additionally append the exit code inside the log, so those two are recorded
rather than inferred. Build steps and the gate scripts were run with output
redirected but without an appended exit line, so their exit codes are not
recoverable from the logs and are not claimed here.

The fixture in the failure output is the concrete evidence:
`combFeedbackFractionalHalfDelay() ... frame 1 got 0 want 0.5; frame 3 got 0.5
want 0` (`021`), i.e. the old effective delay 2.5. The reverse run reverts only
the non-test hunks, so the same assertion fails again and passes when restored.

### Gate-run disclosure

The first `feature_check.py` run exited 1 with `FAIL fork-sources` and
`FAIL all-sources-reproduce`: `DeviceDspTestParts3.h` was missing from
`tests/all-sources.txt`. After re-deriving the manifest (commit `0b66147cb3`)
the rerun gave EXIT=0 with 8/8 PASS. The failing run's log was overwritten by
the rerun, so its raw output is not preserved; `logs/024-gates.log` holds only
the passing rerun.

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

- **Regression test fails reverted and passes restored, with recorded exits.**
  Run 008 was taken against the unfixed header with the new tests in place:
  EXIT=8 with 9 failures (`logs/008-test-red.log`). The reverse run 021 lists
  the same 9 tests and exits 8 (`logs/021-test-reversed-red.log`,
  `Totals: 40 passed, 9 failed`). Restored green: 023 `100% tests passed out of
  1`, and the final recorded runs 026 (EXIT=0, out of 2) and 027 (EXIT=0, out
  of 3).
- **Deterministic: no wall-clock timing, sleeps or races.** The tests drive
  `StereoCombFeedback`, `StereoCombFeedfwd`, `StereoAllpassDelay` and
  `StereoCombFeedbackDualtap` directly - these classes are header-only, so
  instantiating them is the real test surface, not a plugin or device wrapper.
  Each renders a fixed impulse and compares the exact expected sample frames
  for literal delays (1.25, 1.5, 2.75, 0.5), and each also checks that the other
  channel stays silent. Nothing is timed or descheduled.
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
