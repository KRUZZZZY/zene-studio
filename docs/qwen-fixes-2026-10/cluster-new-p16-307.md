Verdict: FIXED

# Cluster new-p16-307 - tick subtraction overflows before frame conversion

Finding: `src/core/BounceInPlace.cpp:333` (historical line; now `:336`) computed `(to - from) * framesPerTick` with `tick_t` (`std::int32_t`, `include/LmmsTypes.h:35`) operands, so a span wider than 2^31-1 ticks overflowed before the frame conversion.

## Verdict: REAL

The subtraction is in `tick_t` and the non-tempo-map branch of `BounceInPlace::framesBetweenTicks` is the only place this happens in the function. Reproduced before the fix: `framesBetweenTicks(INT32_MIN, INT32_MAX, 100.0f, inactive, 120, 44100)` returned `-100` (the span wrapped to -1 tick), see `test-1-red.log`.

### Reachability (read from the code, not run end to end)

- The render path (`prepareRenderTracks`, `src/core/BounceInPlace.cpp:384` and `:386`) only passes `0` and `range.start..range.end`. The control path clamps and validates them to `[0, MaxSongLength]` (`src/core/ControlBounceSupport.cpp:62-63`, `:80-82`; `MaxSongLength` is `9999 * 192` at `include/Song.h:81`), so the render path cannot overflow.
- The frozen-take playback path can. `Track::frozenTakeSourceFrame` (`src/core/Track.cpp:1449-1452`) is called from `Track::playFrozenTake` (`src/core/Track.cpp:1509`) on the audio thread. `frozenStart`/`frozenEnd` are read from project XML by `parseTick` (`src/core/Track.cpp:217-228`, `:1389-1391`) over the whole `tick_t` range, with no bound. `playFrozenTake` only requires `position >= startTicks` (`src/core/Track.cpp:1466-1487`), and `transport.seek` accepts ticks up to `0x7fffffff` with no `MaxSongLength` clamp (`src/core/ControlCommandsTransport.cpp:222`).
- So a crafted project with a take starting below zero, with no active tempo map, and a seek to a large tick, reaches the overflow. Ordinary projects do not. This is latent and needs crafted input; the runtime path was not exercised end to end.

## The fix

`src/core/BounceInPlace.cpp:333-337`: the non-tempo-map branch subtracts in `qint64` and converts the difference to `float` before the multiply:

```cpp
return static_cast<float>(static_cast<qint64>(to) - static_cast<qint64>(from)) * framesPerTick;
```

- **In-range results are unchanged bit for bit.** For any difference representable in `int32`, `float(int64 diff)` and `float(int32 diff)` are the same correctly rounded value, and the multiply is still a `float * float` as before. (Reasoned from the C++ conversion rules; the test below also checks an exact in-range value.)
- The product stays in `float`, as before. A `double` product would change in-range rounding, which this fix does not. The float's 24-bit mantissa still gives a relative error up to about 2^-24 for spans near 2^31 ticks (pre-existing, not changed here).
- The tempo-map branch (`:338`) subtracts doubles from `secondsAtTick`, so it has no tick subtraction to overflow.
- No header changed, so no dependent header rebuild was needed.

## Regression test

`tests/src/core/BounceInPlaceTest.cpp`: new slot `framesBetweenTicksSpansTheWholeTickRange` (lines 194-207) with `#include <cmath>`, `<limits>` and `TempoMap.h`. It uses constants only (no clock, sleep, engine or race): an inactive `TempoMap`, `framesPerTick = 100.0f`, and the extreme ticks. It checks the forward span is positive and equals `4294967295 * 100` within relative `1e-6`, the reverse span is the negated count, and an in-range span `24..120` equals `96 * 100.0` exactly.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-307.r1/logs`.

| # | Command (run from the worktree / `build/tests` as noted) | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target BounceInPlaceTest -j4` (RED build attempt) | 2 | `build-1-red.log` (ccache could not create a temp file under the shared cache dir; no compile happened) |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target BounceInPlaceTest -j4` (RED build) | 0 | `build-2-red.log` |
| 3 | `ctest -R '^BounceInPlaceTest$' --output-on-failure` (in `build/tests`) **RED, before fix** | **8** | `test-1-red.log`: `FAIL! : BounceInPlaceTest::framesBetweenTicksSpansTheWholeTickRange() 'forward > 0.0' returned FALSE. (-100)`; Totals 7 passed, 1 failed |
| 4 | fix applied; `CCACHE_DISABLE=1 cmake --build build --target BounceInPlaceTest -j4` | 0 | `build-3-green.log` |
| 5 | `ctest -R '^BounceInPlaceTest$' --output-on-failure` **GREEN** | 0 | `test-2-green.log`: 100% tests passed, 1 of 1 |
| 6 | Step 4b: `git diff -- src/core/BounceInPlace.cpp > logs/fix.patch` | 0 | (patch saved) |
| 7 | Step 4b: `git apply -R logs/fix.patch` | 0 | |
| 8 | `CCACHE_DISABLE=1 cmake --build build --target BounceInPlaceTest -j4` (fix reverted) | 0 | `build-4-unfixed.log` |
| 9 | `ctest -R '^BounceInPlaceTest$' --output-on-failure` **reverted: must FAIL** | **8** | `test-3-unfixed.log`: `forward > 0.0` returned FALSE (-100) |
| 10 | `git apply logs/fix.patch` | 0 | |
| 11 | `CCACHE_DISABLE=1 cmake --build build --target BounceInPlaceTest -j4` (fix restored) | 0 | `build-5-fixed.log` |
| 12 | `ctest -V -R '^BounceInPlaceTest$'` **restored: must PASS** | 0 | `test-4-fixed-verbose.log`: `PASS : BounceInPlaceTest::framesBetweenTicksSpansTheWholeTickRange()`; Totals 8 passed, 0 failed |
| 13 | `CCACHE_DISABLE=1 cmake --build build --target zene FreezeRegionRepairTest ControlBounceRenderTest -j4` | 0 | `build-6-family.log` |
| 14 | `ctest -R 'BounceInPlaceTest\|FreezeRegionRepairTest\|ControlBounceRenderTest' --output-on-failure` (in `build/tests`) | 0 | `test-5-family.log`: 3/3 tests run, 100% passed |

Affected family: **3 `ctest` entries executed, 3 passed** (`BounceInPlaceTest`, `FreezeRegionRepairTest`, `ControlBounceRenderTest`). `BounceInPlaceTest` itself runs 8 test functions.

### Gates

| # | Command | EXIT | Log |
|---|---|---|---|
| 15 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `gates-1.log`: fails before any gate runs, with `PermissionError` creating `<worktree>-feature-check`, which is outside the write-jail |
| 16 | Same gate functions and gate lists imported from `feature_check.py`, run in the worktree, logs redirected to the log folder; baseline taken from the cached `gate-baselines/37eaf07557…json` (the merge base with `050/m0-train-21`; no new worktree created) | 0 | `gates-2-table.txt` (`RESULT: PASS`) |

Replicated table (`gates-2-table.txt`):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              script exit 1; no new item in your files
  PASS  file-length             script exit 1; no new item in your files
RESULT: PASS
```

The complexity and file-length scripts exit 1 on the train's existing baseline, which is expected; their logs contain no item for `BounceInPlace` or `framesBetween`. This replication is my stand-in for the official run, which could not write its log directory.

### Registration

- `src/core/BounceInPlace.cpp` and `tests/src/core/BounceInPlaceTest.cpp` are already in `tests/fork-sources.txt` (lines 1359 and 2366), so no new registration was needed.
- `src/core/BounceInPlace.cpp` is an edited inherited-registry file, so a line was added to `tests/upstream-modifications.txt` in the same commit with its reason.

## Siblings checked and not changed

- `src/core/Track.cpp:365` subtracts `m_frozen.endTicks - m_frozen.startTicks` and `loopEnd - loopBegin` in `tick_t` to size the frozen-voice pool. It is the same class of overflow, but it feeds voice capacity, not frame conversion, and it is outside this finding's file. Not fixed here; a follow-up.
- `src/core/BounceInPlace.cpp:227` (`tickAfter`) adds `start + frames/framesPerTick` in `tick_t`. It is the inverse direction (frames to ticks), not part of this finding. Not fixed here.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** RED: `test-1-red.log` (EXIT 8) and `test-3-unfixed.log` (EXIT 8, step 4b). GREEN: `test-2-green.log` (EXIT 0) and `test-4-fixed-verbose.log` (EXIT 0, 8 passed).
- **The test is deterministic.** It uses constants only: an inactive `TempoMap` built in place, `framesPerTick = 100.0f`, and the `tick_t` extremes from `std::numeric_limits`. No clock, sleep, engine state, threads or randomness.
- **The fix is at the cause and complete.** The only caller that can reach the overflow is the frozen-take path (`Track.cpp:1509`); the render callers (`BounceInPlace.cpp:384`, `:386`) use values already bounded to `[0, MaxSongLength]`. Both callers' values are unchanged for in-range inputs. The tempo-map branch has no tick subtraction. The siblings above are listed, not hidden.
- **No collateral damage.** No limit or check was added; the function only widens the subtraction. In-range results are bit-identical (reasoned from the conversion rules, with an exact in-range assertion in the test). No saved-data format, header or command changed. The family runs 3/3 (`test-5-family.log`).
- **Verdict is REAL, not ALREADY-FIXED or FALSE-POSITIVE**, so no "not real" proof is needed. Reachability is latent (crafted project plus a large seek) and the end-to-end path was not run.
- **Realtime.** `framesBetweenTicks` is reached from the audio thread via `playFrozenTake`. The fix adds an integer widening and a float cast only: no allocation, lock, log or wait.
- **Gate blocker.** The official `feature_check.py` cannot write its log directory outside the write-jail (`gates-1.log`); the replicated run (row 16) passes. `submit_check.py` will show the gates item as FAIL until the official script can run.
- **Environment issue.** The default ccache temp directory is not writable from this sandbox, so the builds used `CCACHE_DISABLE=1`. Compiled output is not affected by the cache setting.
