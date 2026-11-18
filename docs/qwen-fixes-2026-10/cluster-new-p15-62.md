# Cluster new-p15-62: NaN frequency reaches non-peak EQ filters

**Verdict: FIXED**

`clampFrequency()` in `plugins/Eq/EqFilter.h:153-158` passed a NaN frequency through `std::clamp`, which returns NaN unchanged. The high-pass, low-pass and both shelf filters then computed NaN coefficients (`w0` at `EqFilter.h:204`, `247`, `384`, `421`), and the NaN output fed back through their biquad state. The peak filter was already protected by its `isfinite(s)` guard (`EqFilter.h:306`), so it is unchanged. The fix maps a NaN frequency to 0 Hz (`EqFilter.h:156-157`); ±∞ still clamp to the existing bounds.

The reachability of NaN is shown by the model path: `AutomatableModel.cpp:606` and `:616` use `std::clamp`, which also passes NaN through, so a NaN model value reaches `EqEffect::processAudioBuffer` (`plugins/Eq/EqEffect.cpp:147-160`) and then `setParameters`. Whether a saved project can actually carry a NaN value (for example an XML value that `toFloat` parses as `nan`) is **not verified in this run**.

## Behaviour of a NaN frequency after the fix

Derived from the coefficients at 0 Hz (`w0 = 0`, so `c = 1`, `s = 0`); not measured by ear:

- low-pass: silent output (all feed-forward coefficients are 0).
- high-pass: pass-through (`H(z) = 1`).
- low shelf: pass-through (`H(z) = 1`).
- high shelf: flat gain `A²` across all frequencies, where `A = 10^(gain·0.025)`.
- peak: bypass, the same as before the fix (its `sin(0)` guard already bypasses).

The choice of 0 Hz is the one this run made; it is the value the filter already holds before any frequency is set (`EqFilter()` constructor, `EqFilter.h:51`) and the lower bound of the clamp. A reviewer who prefers a bypass for malformed input would need a different branch in each non-peak `calcCoefficents`; that is a design change this run did not make.

## Regression test

`tests/src/plugins/EqFilterTest.cpp` (existing file, already in `tests/fork-sources.txt`) gains three slots:

- `nonFiniteFrequencyFallsBackToZeroHertz` (line 131): the low-pass and peak filters report `m_freq == 0` after a NaN frequency.
- `nonFiniteFrequencyKeepsEveryFilterOutputFinite` (line 144): high-pass, low-pass, low shelf, high shelf and peak all produce finite impulse responses after `setParameters(44100, NaN, …)`.
- `liveNonFiniteFrequencyKeepsOutputFinite` (line 169): high-pass and low-pass stay finite after a valid frequency is followed by `setFrequency(NaN)`.

The test is deterministic: no timing, sleeps or threads.

## Evidence

Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-62.r1/logs/`. All ctest runs were from `build/tests`. Every build ran with `CCACHE_DISABLE=1` (see the note below).

| Step | Exact command | Log | Exit |
|---|---|---|---|
| Build attempt, ccache default | `cmake --build build --target EqFilterTest -j4 > .../build-1-red.log 2>&1; echo EXIT=$?` | build-1-red.log | 2 (ccache could not write temp file under `dsh/`; not a code error) |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target EqFilterTest -j4 > .../build-2-red.log 2>&1; echo EXIT=$?` | build-2-red.log | 0 |
| **RED run (fix absent)** | `ctest -R '^EqFilterTest$' --output-on-failure > .../test-1-red.log 2>&1; echo EXIT=$?` | test-1-red.log | **8** |
| Fix applied, GREEN build | `CCACHE_DISABLE=1 cmake --build build --target EqFilterTest -j4 > .../build-3-green.log 2>&1; echo EXIT=$?` | build-3-green.log | 0 |
| **GREEN run** | `ctest -R '^EqFilterTest$' --output-on-failure > .../test-2-green.log 2>&1; echo EXIT=$?` | test-2-green.log | **0** (1/1 executed) |
| GREEN, QTest totals | `./EqFilterTest > .../test-3-green-direct.log 2>&1; echo EXIT=$?` | test-3-green-direct.log | 0 (`Totals: 11 passed, 0 failed`) |
| Save product patch | `git diff -- plugins/Eq/EqFilter.h > .../fix.patch; echo EXIT=$?` | fix.patch | 0 |
| Reverse product patch | `git apply -R .../fix.patch; echo EXIT=$?` | — | 0 |
| Reverted build | `CCACHE_DISABLE=1 cmake --build build --target EqFilterTest -j4 > .../build-4-revert.log 2>&1; echo EXIT=$?` | build-4-revert.log | 0 |
| **Reverted RED** | `./EqFilterTest > .../test-4-revert-direct.log 2>&1; echo EXIT=$?` | test-4-revert-direct.log | **3** (3 failing slots, see below) |
| Re-apply product patch | `git apply .../fix.patch; echo EXIT=$?` | — | 0 |
| Re-applied build | `CCACHE_DISABLE=1 cmake --build build --target EqFilterTest -j4 > .../build-5-reapply.log 2>&1; echo EXIT=$?` | build-5-reapply.log | 0 |
| **Re-applied GREEN** | `./EqFilterTest > .../test-5-reapply-direct.log 2>&1; echo EXIT=$?` | test-5-reapply-direct.log | **0** (`Totals: 11 passed, 0 failed`) |
| Targets that include `EqFilter.h` | `CCACHE_DISABLE=1 cmake --build build --target eq zene -j4 > .../build-6-eq-zene.log 2>&1; echo EXIT=$?` | build-6-eq-zene.log | 0 |
| Eq sibling tests built | `CCACHE_DISABLE=1 cmake --build build --target EqCurveTest EqPeakHandoffTest -j4 > .../build-7-eq-family.log 2>&1; echo EXIT=$?` | build-7-eq-family.log | 0 |
| Eq family (first run, before sibling build) | `ctest -R 'Eq' --output-on-failure > .../test-6-eq-family.log 2>&1; echo EXIT=$?` | test-6-eq-family.log | 8 (EqCurveTest and EqPeakHandoffTest "Not Run": binaries not yet built) |
| **Eq family (final)** | `ctest -R 'Eq' --output-on-failure > .../test-7-eq-family.log 2>&1; echo EXIT=$?` | test-7-eq-family.log | **0** (3/3 executed and passed: EqFilterTest, EqCurveTest, EqPeakHandoffTest) |
| Gate, as written | `python3 .../qwen/feature_check.py <worktree> > .../gate-1-feature-check.log 2>&1; echo EXIT=$?` | gate-1-feature-check.log | 1 (`PermissionError` creating the sibling directory `zene-qwen-new-p15-62-feature-check` outside the write-jail; no gate ran) |
| **Gate, log directory redirected** | `python3 -I .../logs/fc-src/feature_check_redirected.py <worktree> > .../gate-2-feature-check.log 2>&1; echo EXIT=$?` | gate-2-feature-check.log | **0**, `RESULT: PASS` |

The copy `feature_check_redirected.py` differs from the original only in two lines: `ROOT` points at the original `qwen/` directory (so the cached base listing for `639e1d23e` is read, not recomputed), and `logs` points at the run folder's `feature-check/` directory. The gate logic is unchanged.

Gate rows from `gate-2-feature-check.log`:

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

### RED assertions (`test-1-red.log`)

- `nonFiniteFrequencyFallsBackToZeroHertz`: `Compared floats are not the same … Actual (lowPass.frequency()): nan, Expected (0.0f): 0` at `EqFilterTest.cpp:137`.
- `nonFiniteFrequencyKeepsEveryFilterOutputFinite`: `'std::isfinite(filter.update(input, 0, 1.0f))' returned FALSE` at `EqFilterTest.cpp:54`, reported for four filters.
- `liveNonFiniteFrequencyKeepsOutputFinite`: the same assertion, at `EqFilterTest.cpp:54`, reported for two filters.

Totals in the RED run: `8 passed, 3 failed`. The reverted run (`test-4-revert-direct.log`) shows the same three slots failing.

Note on step 4b: the RED ctest run and the GREEN ctest run used `ctest`; the reverted and re-applied runs used the `EqFilterTest` binary directly from `build/tests`, which is the same executable ctest runs. Both were confirmed by the `Totals:` line.

## Note on build environment

Builds failed at first because ccache could not create a temp file under `~/.local/share/zene-bunny-fix/dsh/` (exit 2, `ccache-tmp-r6/stdout.tmp…: Permission denied`). Setting `CCACHE_DISABLE=1` for the build commands avoided it without touching any file. The compiled code is the same; only the cache was skipped.

## Not fixed: sibling NaN paths (same class, different input)

These are outside the finding's frequency scope and were **not** changed:

- NaN **gain**: `EqPeakFilter::calcCoefficents` guards `s` and `sinhArgument` but not `A = fastPow10f(m_gain * 0.025)` (`EqFilter.h:309` in the pre-fix numbering, `312` now). A NaN gain still gives NaN peak coefficients. The shelves also use `A`.
- NaN **resonance**: `alpha = s / ( 2 * m_res )` (`EqFilter.h:207`, `250`) and `beta = std::sqrt(A) / m_res` (`EqFilter.h:389`, `425`) are NaN for a NaN `m_res`.
- Sample rate **0**: non-peak `w0 = 2π·m_freq / m_sampleRate` divides by zero. `EqEffect` always passes the engine rate, so this is not reached through the plugin; reachability is not verified.
- A NaN frequency is still stored raw in `m_requestedFreq`, so `freq != m_requestedFreq` is always true for it, and `calcCoefficents` runs on every period. This is pre-existing and only costs CPU.

Each of these needs its own finding and test; none is covered by the regression above.

## Registration

- `tests/upstream-modifications.txt`: one line added for `plugins/Eq/EqFilter.h` (inherited file, changed), with the reason and the regression test name.
- No new files were created, so `tests/fork-sources.txt` and `tests/all-sources.txt` are unchanged.

## Realtime, undo and commands

- The change adds one `std::isnan` branch to `clampFrequency`. It does not allocate, lock, log or wait, and `clampFrequency` is already called per period on the audio thread.
- No control command was added or changed; `ControlCommandsSnapshot` is untouched.
- Undo is not involved.

## Self-check

Answers to the five ASK items and the gate row of the pre-submit check. Logs are under the run folder named above.

- **Regression fails without the change and passes with it.** Yes. RED: `test-1-red.log`, ctest EXIT 8, 3 failing slots (fix absent). Reverted product patch on the same test binary: `test-4-revert-direct.log`, EXIT 3, the same three slots fail. Re-applied patch: `test-5-reapply-direct.log`, EXIT 0, `Totals: 11 passed, 0 failed`. The reversal used the saved `fix.patch` as in step 4b.
- **Deterministic.** Yes. No timing, sleeps, threads or randomness. The inputs are literal values (`NaN` from `std::numeric_limits<float>::quiet_NaN()`, 44100 Hz, 1000 Hz). The impulse responses are fixed 32-sample sequences.
- **Fix at the cause; callers and siblings checked.** The cause is `clampFrequency`, which every frequency path calls: `EqFilter.h:67` (`setSampleRate`), `:79` (`setFrequency`), `:120` (`setParameters`), and `:353` (`EqPeakFilter::setParameters`). The peak filter's own path already bypassed NaN through its `isfinite(s)` guard (`:306`) and still bypasses, now via the 0 Hz `sin` check (`:306`, `abs(s) < 1e-6`). `EqEffect.cpp:147-160` is the only production caller and passes every frequency through `setParameters`. Sibling NaN inputs (gain, resonance, sample rate) are **not** covered by this fix; they are listed under "Not fixed" above with their line numbers.
- **No collateral damage.** Finite values and ±∞ take the unchanged code path; the only new branch is `std::isnan(freq)`. A saved project with a finite frequency loads and filters exactly as before. A project that contained NaN previously produced NaN audio; it now produces the 0 Hz behaviour described above. No limit or check was added that could reject a legitimate value.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The pre-fix code is at `EqFilter.h:153-156` (`git show 639e1d23e:plugins/Eq/EqFilter.h`), where `std::clamp` alone returned NaN, and the RED run reproduced the NaN output. So no "already fixed" or "false positive" evidence applies.
- **Gate row (`feature_check.py`, as written).** **Not satisfied in this environment.** The original script exits 1 with `PermissionError` when it creates the sibling directory `zene-qwen-new-p15-62-feature-check` in the projects folder, which lies outside the write-jail (`gate-1-feature-check.log`). The same gate code, with only the log directory redirected into the run folder, exits 0 with all 8 rows PASS (`gate-3-feature-check-final.log`, run after the report was written and before its commit). `submit_check.py` runs the unmodified script, so its gate row will keep failing until the gate can write its log directory. The fix does not depend on that; it is an environment limit for the lane.
