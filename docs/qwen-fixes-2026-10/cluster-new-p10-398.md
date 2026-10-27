Verdict: FIXED

Cluster new-p10-398: a zero-length StereoDelay divides by zero in `tick()`.

## Evidence

- **Reachable on this branch (before the fix).** `tick()` takes the ring length as a modulo divisor:
  `plugins/Delay/StereoDelay.cpp:63` (`m_writeIndex = ( m_writeIndex + 1 ) % ( int )m_maxLength;`).
  Nothing bounded `m_maxLength` from below. The constructor sets it to `maxTime * sampleRate`
  (`:39`) and `setSampleRate()` (the only other writer) sets it to `sampleRate * m_maxTime`
  with no lower bound. So `StereoDelay{0, 48000}`, `StereoDelay{20, 0}` and
  `setSampleRate(0)` all leave a zero-length ring, and the next `tick()` is a modulo by zero.
  A negative argument also made `new SampleFrame[negative]` throw, and a negative `m_length`
  made `tick()` read past the one-frame buffer (`readIndex = writeIndex - (int)m_length`).
- **Production reach.** The only production caller is `DelayEffect`: `plugins/Delay/DelayEffect.cpp:63`
  constructs with a fixed 20 s, and `changeSampleRate()` (`DelayEffect.cpp:156-159`) passes
  `outputSampleRate()`. A zero output rate is not expected in normal use, so the trigger is
  the constructor/setter contract rather than a normal user path. The fix covers the contract.
- **The fix (at the cause).** `setSampleRate()` is the single place the ring size is set after
  construction (the constructor calls it), so the floor goes there:
  - `plugins/Delay/StereoDelay.cpp:88`: `const int bufferSize = std::max(1, static_cast<int>(sampleRate * m_maxTime));`
    The ring is never smaller than one frame, so `tick()` never divides by zero, and a negative size
    never reaches `new`.
  - `plugins/Delay/StereoDelay.cpp:99-102`: the read length is clamped to `>= 0`, so `tick()` cannot index past the ring.
  - `plugins/Delay/StereoDelay.cpp:29`: `#include <algorithm>` for `std::max`.
  - Clamp choice: one frame (not "skip processing"). A one-frame ring with `m_length == 0` is a
    one-sample delay line, which keeps the output stream defined. No tick-path guard was added,
    because `m_maxLength >= 1` is now an invariant established in the only writer and a guard would be dead code.
- **Behaviour outside the bug is unchanged.** For `maxTime > 0` and `sampleRate > 0`,
  `bufferSize` is the same integer as before (`max(1, x) == x` for `x >= 1`), and the new
  `m_length < 0` branch never runs (the length is non-negative for positive arguments and
  `setLength()` rejects negatives). The existing test `sampleRateChangeRebuildsRingState` still passes.
- **Realtime.** `tick()` is untouched. The change is in `setSampleRate()`, on the sample-rate
  change path (`changeSampleRate`), which already allocates (`new SampleFrame[...]`). No new allocation,
  lock, log, or wait was added.
- **Undo, commands, saved data.** No command, serialized field, or project data is touched.
  A zero or negative delay time can only come from a bad constructor/setter argument, so
  saved projects load exactly as before.

## Regression test

`tests/src/core/StereoDelay.cpp` (ctest `StereoDelay`, test #360). Slots:

| Slot | Case | Without fix | With fix |
| --- | --- | --- | --- |
| `zeroMaxTimeClampsToOneFrameRing` | `StereoDelay{0, 48000}` | FAIL (`m_maxLength` 0, line 30) | PASS |
| `zeroSampleRateClampsToOneFrameRing` | `StereoDelay{20, 0}` | FAIL (`m_maxLength` 0, line 30) | PASS |
| `negativeArgumentsClampToOneFrameRing` | `StereoDelay{-1, 48000}` | FAIL (constructor threw `bad_alloc`, line 94) | PASS |
| `zeroSampleRateReconfigurationKeepsRingUsable` | `setSampleRate(0)` after a 1 s ring | FAIL (`m_maxLength` 0, line 30) | PASS |
| `sampleRateChangeRebuildsRingState` (existing) | unchanged | PASS | PASS |

The helper `checkOneFrameDelay()` asserts `m_maxLength == 1` and the one-sample delay sequence
(first tick outputs 0, the second outputs the first input, `m_writeIndex` stays 0). It is deterministic,
with no timing, sleeps, or threads.

The negative-argument case was added after a first GREEN attempt that clamped only the ring
size. That attempt still failed (`test-3-green.log`: second tick read `8.9e-39`, expected `0.5`).
That is the negative `m_length` out-of-bounds read, so the lower bound on `m_length` was added
(`:99-102`). This is why the final fix has two clamps.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-398.r1/logs/`.

ccache could not write its temp file under the default temp path (outside the write-jail,
`build-1-red.log`, EXIT 2). Every later build sets `CCACHE_TEMPDIR` to a directory under `$TMPDIR`.
This changes the build environment only, not the repository.

| Step | Command | EXIT |
| --- | --- | --- |
| Build, first try (ccache temp path denied) | `cmake --build build --target StereoDelay -j4 > logs/build-1-red.log 2>&1` | 2 |
| RED build (test only, unfixed product) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-398 cmake --build build --target StereoDelay -j4 > logs/build-2-red.log 2>&1` | 0 |
| **RED run** (unfixed) | `cd build/tests && ctest -R '^StereoDelay$' --output-on-failure > logs/test-2-red.log 2>&1` | **8** (4 slots FAIL, `Totals: 3 passed, 4 failed`) |
| GREEN attempt 1 (ring-size clamp only) | build-3-green.log (EXIT 0); `ctest -R '^StereoDelay$' ...> logs/test-3-green.log` | build 0, **test 8** (`negativeArguments...` FAIL: OOB read, line 40) |
| GREEN build (final fix) | `CCACHE_TEMPDIR=... cmake --build build --target StereoDelay -j4 > logs/build-4-green.log 2>&1` | 0 |
| **GREEN run** (final fix) | `cd build/tests && ctest -R '^StereoDelay$' --output-on-failure > logs/test-4-green.log 2>&1` | **0** (`100% tests passed out of 1`) |
| GREEN per-slot (verbose binary) | `./StereoDelay -v2 > logs/test-6b-fixed-verbose.log 2>&1` | 0 (7 passed, 0 failed) |
| **Proof 4b, revert** | `git diff -- plugins/Delay/StereoDelay.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 (patch 36 lines) |
| Proof 4b, unfixed build | `CCACHE_TEMPDIR=... cmake --build build --target StereoDelay -j4 > logs/build-5-proof-unfixed.log 2>&1` | 0 |
| **Proof 4b, unfixed run (must FAIL)** | `cd build/tests && ctest -R '^StereoDelay$' --output-on-failure > logs/test-5-proof-unfixed.log 2>&1` | **8** (4 slots FAIL, same assertions as RED) |
| Proof 4b, reapply | `git apply logs/fix.patch` | 0 |
| Proof 4b, fixed build | `CCACHE_TEMPDIR=... cmake --build build --target StereoDelay -j4 > logs/build-6-proof-fixed.log 2>&1` | 0 |
| **Proof 4b, fixed run (must PASS)** | `cd build/tests && ctest -R '^StereoDelay$' --output-on-failure > logs/test-6-proof-fixed.log 2>&1` | **0** |
| Family build (dependent, `StereoDelay.cpp` + `DelayEffect.cpp`) | `CCACHE_TEMPDIR=... cmake --build build --target DelayLfoAutomationTest -j4 > logs/build-7-family.log 2>&1` | 0 |
| Family build (remaining Delay tests) | `CCACHE_TEMPDIR=... cmake --build build --target MonoDelayTest DelayControlsTest -j4 > logs/build-8-family-rest.log 2>&1` | 0 |
| Family run | `cd build/tests && ctest -R 'Delay' --output-on-failure > logs/test-8-family.log 2>&1` | **0** (`100% tests passed out of 4`: MonoDelayTest, StereoDelay, DelayControlsTest, DelayLfoAutomationTest) |

Affected-family test count: 4 executed (MonoDelayTest #13, StereoDelay #360, DelayControlsTest #595,
DelayLfoAutomationTest #598); 0 failed. There is no ctest named `DelayEffect`; the family above
is what the build tree has, and it includes the one test that compiles `DelayEffect.cpp`.

Gates: `plugins/Delay/StereoDelay.cpp` and `tests/src/core/StereoDelay.cpp` are the only changed files
(`git diff --name-only 639e1d23e..HEAD`). `feature_check.py` writes its logs to a sibling directory of
the worktree, which is outside the write-jail, so it fails at the start with a PermissionError (see the
self-check). The gate commands were run from the worktree with the same logic, via
`logs/gates_wrapper.py` (imports the real `feature_check.py` functions; the logs go to the run folder):

| Gate | Command | EXIT |
| --- | --- | --- |
| feature-check (wrapper, same logic) | `python3 -I runs/new-p10-398.r1/gates_wrapper.py <worktree> runs/new-p10-398.r1/logs/gates-feature-check > logs/gates-1.log 2>&1` | **0** (`RESULT: PASS`) |
| feature_check.py, direct | `python3 .../feature_check.py <worktree> > logs/gates-direct.log 2>&1` | 1 (PermissionError creating the sibling log dir; no gate ran) |

Gate rows (wrapper): fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS,
check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in changed files;
the ratchet is red on the train on purpose), file-length PASS (same).

## Registration

- `tests/src/core/StereoDelay.cpp`: already in `tests/fork-sources.txt` (no new file).
- `plugins/Delay/StereoDelay.cpp`: inherited, already listed in `tests/upstream-modifications.txt` for
  `new-p9-443`. A line for `new-p10-398` was added beside it (same commit).

## Self-check

`submit_check.py new-p10-398` (`logs/submit-check-1.log`, EXIT 1): PASS commits on the branch,
working tree clean, report verdict line, tests changed. **FAIL gates (feature_check.py)**: blocked,
not fixed. `submit_check` runs `feature_check.py`, which must create `<worktree>-feature-check` next to
the worktree. That path is outside the write-jail (`PermissionError`, `logs/gates-direct.log`, EXIT 1), so
no gate runs through the checker. The same gate commands, run from the worktree with logs in the run folder
(`gates_wrapper.py`, EXIT 0), all pass. This is the only blocker; the fix itself is committed. A fix to the
checker or the jail path is outside this lane.

1. **RED then GREEN, with the change reverted and restored (exit codes).** Revert: `test-5-proof-unfixed.log`,
   EXIT 8, four slots FAIL (`m_maxLength` 0 at `tests/src/core/StereoDelay.cpp:30`; constructor
   `bad_alloc` at `:94`). Restored: `test-6-proof-fixed.log`, EXIT 0, and `test-6b-fixed-verbose.log`
   shows 7 passed, 0 failed. The RED run before the fix, `test-2-red.log`, gives the same four failures.
2. **Deterministic.** No clock, sleep, thread, or random input. The inputs are fixed constructor and
   setter arguments, and sample values are exact in binary (`0.5f`, `0.25f`). The test reads the
   existing private members through the file's existing `#define private public` seam (already in the
   file before this change). The negative-argument slot catches a `bad_alloc` from a fixed argument.
3. **At the cause, and the siblings are consistent.** Callers of `setSampleRate()`: the constructor
   (`StereoDelay.cpp:44`, which now always goes through the clamp) and `DelayEffect::changeSampleRate()`
   (`DelayEffect.cpp:159`). Siblings checked: `setLength()` (`StereoDelay.h:40-46`) already accepts only
   `0 <= length <= m_maxLength`, which is consistent with a one-frame ring; `tick()` (`:63`) now always divides
   by `>= 1` and reads `readIndex` in `[0, m_maxLength)`; the scale branch `if (m_maxLength > 0)` (`:89`) is
   unchanged; the destructor is unchanged. The reference copies under `tests/reference/Delay/` are upstream
   snapshots and were not edited.
4. **No collateral damage.** For `maxTime > 0` and `sampleRate > 0`, `max(1, x) == x`, so `bufferSize`
   and `m_length` are unchanged, and the new `m_length < 0` branch is not reached (`setLength()`
   rejects negatives). The only production construction is a constant 20 s in `DelayEffect.cpp:63`; no
   saved project data sets the delay ring size, so saved projects load as before. The one-frame floor only
   acts on arguments that were already broken (zero or negative). Existing `sampleRateChangeRebuildsRingState`
   and `DelayLfoAutomationTest`, `MonoDelayTest`, `DelayControlsTest` pass (`test-8-family.log`, EXIT 0).
5. **Verdict.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so no "already fixed" or "not real" proof is
   needed. The bug is reachable on this branch at `StereoDelay.cpp:63` before the fix.
