Verdict: FIXED

new-p10-128: a null integrate frame counter causes modulo by zero

## Finding and decision

Luna's finding (`plugins/Xpressive/ExprSynth.cpp:127`, historical) says that `setIntegrate(nullptr, sample_rate)` followed by evaluation can trap on integer division by zero. I confirmed this, and the decision is REAL, reached through the public API only:

- `IntegrateFunction::operator()` skips the counter block when `m_frame` is null, so `m_nCountersCalls` stays 0 forever ([ExprSynth.cpp:104](plugins/Xpressive/ExprSynth.cpp:104), [ExprSynth.cpp:112](plugins/Xpressive/ExprSynth.cpp:112)).
- The unfixed code still ran `m_cc = (m_cc + 1) % m_nCountersCalls;` unconditionally ([ExprSynth.cpp:132](plugins/Xpressive/ExprSynth.cpp:132) before the fix). With the divisor at 0 this is a SIGFPE on x86.
- The header declares `setIntegrate(const f_cnt_t* frameCounter, unsigned int sample_rate)` with no null exclusion ([ExprSynth.h:58](plugins/Xpressive/ExprSynth.h:58)).
- Both production callers pass a real address, so the trap is latent in the shipped product, not reachable from the current UI: [Xpressive.cpp:630](plugins/Xpressive/Xpressive.cpp:630) passes `&frame_counter`, and [ExprSynth.cpp:759](plugins/Xpressive/ExprSynth.cpp:759) passes `&m_note_sample`.

## Fix

[ExprSynth.cpp:132-136](plugins/Xpressive/ExprSynth.cpp:132): the cycle advance runs only when `m_nCountersCalls > 0`.

```cpp
// Without a frame counter no call is ever counted, so there is no cycle to advance.
if (m_nCountersCalls > 0)
{
    m_cc = (m_cc + 1) % m_nCountersCalls;
}
return res / m_sampleRate;
```

Why this and not the alternative:

- Rejecting a null frame counter in `ExprFront::setIntegrate` (the finding's other option) would stop registering `integrate`, so any expression that uses it would fail to compile. That changes behaviour for a caller that exists today in principle, so I did not take it.
- With `m_nCountersCalls > 0` on every path that reached the modulo before (a non-null frame increments it on the first call, [ExprSynth.cpp:112](plugins/Xpressive/ExprSynth.cpp:112), before line 132), the change only affects the null case. In the null case `m_cc` was 0 and remained 0, and `res` was 0, so the returned value `0 / m_sampleRate` is unchanged. The only difference is that the trap no longer happens.
- Sibling checks:
  - [ExprSynth.cpp:170](plugins/Xpressive/ExprSynth.cpp:170) (`LastSampleFunction`) computes `% m_history_size`, but only after `x >= 1 && x <= m_history_size`, which is false for a size of 0. Not reachable with a zero divisor.
  - [ExprSynth.cpp:227](plugins/Xpressive/ExprSynth.cpp:227) (`WaveValueFunctionInterpolate`) computes `% m_size`. Zero-length vectors are rejected before registration by the earlier fix recorded at `tests/upstream-modifications.txt:771`.
- The return type, the `res / m_sampleRate` expression, and the overflow early-return at [ExprSynth.cpp:113-116](plugins/Xpressive/ExprSynth.cpp:113) are unchanged.

## Regression test

`tests/src/plugins/ExprSynthTest.cpp`, new slot `integrateWithoutFrameCounterDoesNotDivideByZero` (line 268). It builds `ExprFront("integrate(1)", 8)`, calls `setIntegrate(nullptr, 48000)`, compiles, and evaluates twice, requiring 0 both times. It is deterministic (no timing, sleeps or threads) and drives the real `ExprFront` path the finding describes.

Test matrix:

| Path | Covered by |
| --- | --- |
| Null frame counter (the bug) | new slot, RED then GREEN |
| Non-null frame counter, accumulating integrate | existing `expressionTimeContinuesPast32BitFrameCount` (`t + integrate(1)`), still PASS |
| Counter overflow early return | unchanged code path; `ExprSynthAllocationTest` still PASS |
| Audio-thread constraints | the branch adds no allocation, lock, log or call; `expressionRenderErrorIsNotLoggedOnTheAudioThread` still PASS |
| Platform | Linux x86_64 build tree only (GCC 16.2.1, Qt 6.12.0); no platform-specific code |

Note on the RED state: the failure is a signal (SIGFPE, FPE_INTDIV), not a QTest assertion. The process dies at the new slot, so the earlier slots report PASS and the binary exits with a signal.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-128.r1/logs/`.

| # | Command (run from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target ExprSynthTest -j4` | 2 | build-1.log (ccache could not write its temp dir: permission denied, a sandbox issue, not a code error) |
| 2 | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-128 cmake --build build --target ExprSynthTest -j4` | 0 | build-2.log (RED build, unfixed code) |
| 3 | `cd build/tests && ctest -R '^ExprSynthTest$' --output-on-failure` | 8 | test-red-1.log: SIGFPE in the new slot; other five slots PASS |
| 4 | `git diff -- plugins/Xpressive/ExprSynth.cpp > logs/fix.patch` | 0 | |
| 5 | `git apply -R logs/fix.patch` | 0 | |
| 6 | `CCACHE_TEMPDIR=... cmake --build build --target ExprSynthTest -j4` | 0 | build-reverted-1.log |
| 7 | `cd build/tests && ./ExprSynthTest` (reverted) | 136 (signal 8) | binary-red-reverted-1.log: SIGFPE in `integrateWithoutFrameCounterDoesNotDivideByZero` |
| 8 | `git apply logs/fix.patch` | 0 | |
| 9 | `CCACHE_TEMPDIR=... cmake --build build --target ExprSynthTest -j4` | 0 | build-reapplied-1.log |
| 10 | `cd build/tests && ./ExprSynthTest` (fixed) | 0 | binary-green-2.log: Totals 7 passed, 0 failed |
| 11 | `cd build/tests && ctest -R '^ExprSynthTest$' --output-on-failure` (fixed) | 0 | test-green-1.log: 100% passed, 1 of 1 |
| 12 | `CCACHE_TEMPDIR=... cmake --build build --target ExprSynthAllocationTest -j4` | 0 | build-alloc-1.log |
| 13 | `CCACHE_TEMPDIR=... cmake --build build --target zene -j4` | 0 | build-zene-1.log |
| 14 | `cd build/tests && ctest -R 'ExprSynth' --output-on-failure` | 0 | test-family-1.log: 100% passed, **2 of 2 executed** (ExprSynthTest, ExprSynthAllocationTest) |
| 15 | `cd build/tests && ctest -N -R '^ExprSynth$'` | 0 | ctest-N-reference-1.log: **0 tests** (see below) |

`CCACHE_TEMPDIR` points at a fresh directory under `$TMPDIR` (on disk) for these commands only. ccache's configured temp dir (`.../dsh/home/ccache-tmp-r6`) was not writable from this lane, and I did not change any configuration.

### The reference name

The brief lists `ctest -R '^ExprSynth$'` for `tests/reference/Xpressive/ExprSynth.cpp`. That file is not a registered ctest target in this build: `ctest -N -R '^ExprSynth$'` matches 0 tests (step 15). The family that is actually registered is `ExprSynthTest` and `ExprSynthAllocationTest`, and step 14 runs both. I did not count the reference file as executed.

### Feature gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` **could not run in this lane**: it exited 1 with a `PermissionError` creating `<worktree>-feature-check/` beside the worktree (log: feature-check-1.log), which is outside the write-jail. I did not edit the gate. Instead I ran each command the script runs, from the worktree, with logs in the LOG FOLDER:

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | gate-fork-sources-1.log |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | gate-all-sources-reproduce-1.log |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | gate-no-upstream-regression-1.log |
| check-namespace | `tests/scripted/check-namespace` | 0 | gate-check-namespace-1.log |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | gate-no-tautology-1.log |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | gate-doc-figures-1.log |
| complexity | `bash tests/complexity-gate.sh --check` | 1 | gate-complexity-1.log: 7 REGRESSION lines, none in a changed file |
| file-length | `bash tests/file-length-gate.sh --check` | 1 | gate-file-length-1.log: 22 REGRESSION lines, none in a changed file |

The complexity and file-length exits are red on the train by design. The feature-check rule is that they pass when no listed item is new in a file the branch changed. Changed files are `plugins/Xpressive/ExprSynth.cpp`, `tests/src/plugins/ExprSynthTest.cpp` and `tests/upstream-modifications.txt`, and none of the 29 listed items names any of them. I checked this by reading the logs, not by running the base comparison, which needs a write to the qwen gate-baselines cache and a worktree in `zene-cachyos`, both outside the jail. The gate's own "no new item" verdict is therefore unverified in this lane.

## Files committed

- `plugins/Xpressive/ExprSynth.cpp` (fix)
- `tests/src/plugins/ExprSynthTest.cpp` (regression slot)
- `tests/upstream-modifications.txt` (registration of the inherited-file change, with reason)
- `docs/qwen-fixes-2026-10/cluster-new-p10-128.md` (this report)

## Self-check

To be completed after the completion check runs (see below).
