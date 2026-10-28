Verdict: FIXED

# Cluster new-p16-307 - tick subtraction overflows before frame conversion

Finding: `src/core/BounceInPlace.cpp:333` (historical line; the return is now `:336`) computed `(to - from) * framesPerTick` with `tick_t` (`std::int32_t`, `include/LmmsTypes.h:35`) operands, so a span wider than 2^31-1 ticks overflowed before the frame conversion.

Round 2 (review ACCEPT-FIXED on the rework request): the review found a second instance of the same overflow on the frozen-take playback path that round 1 missed. Round 1 fixed `framesBetweenTicks` only; `Track::playFrozenTake` had a no-tempo-map branch that subtracted in `tick_t` and bypassed the helper. Round 2 routes every pass through the helper and adds a regression that exercises `playFrozenTake` itself.

## Verdict: REAL

The subtraction is in `tick_t`, and the non-tempo-map branch of `BounceInPlace::framesBetweenTicks` is where the finding points. Reproduced before the fix: `framesBetweenTicks(INT32_MIN, INT32_MAX, 100.0f, inactive, 120, 44100)` returned `-100` (the span wrapped to -1 tick), see round-1 `test-1-red.log`.

### Reachability (read from the code; the path was exercised by the new test, not from a real project file)

- The render path (`prepareRenderTracks`, `src/core/BounceInPlace.cpp:384` and `:386`) only passes `0` and `range.start..range.end`. The control path clamps and validates them to `[0, MaxSongLength]` (`src/core/ControlBounceSupport.cpp:62-63`, `:80-82`; `MaxSongLength` is `9999 * 192` at `include/Song.h:81`), so the render path cannot overflow.
- The frozen-take playback path can. `Track::playFrozenTake` (`src/core/Track.cpp:1458`, on the audio thread) maps the pass through `frozenTakeSourceFrame` (`:1450`). `frozenStart`/`frozenEnd` are read from project XML by `parseTick` (`src/core/Track.cpp:217-228`, `:1389-1391`) over the whole `tick_t` range, with no bound. `playFrozenTake` only requires `position >= startTicks` (`src/core/Track.cpp:1474`), and `transport.seek` accepts ticks up to `0x7fffffff` with no `MaxSongLength` clamp (`src/core/ControlCommandsTransport.cpp:222`).
- So a crafted project with a take starting below zero, with no active tempo map, and a seek to a large tick, reaches the overflow. Ordinary projects do not.

### What the overflow does in that path

- The wrapped span becomes a negative `sourceIn`, which passes the `sourceIn >= sourceOut` window check (`src/core/Track.cpp:1514`). `static_cast<int>(sourceIn)` then gives a large positive start, which `acquirePooledSampleVoice` refuses (the `handle == nullptr` branch, `:1538-1542`). That branch does not clear the pass latch `m_frozenTakePlaying`, while the window check does, so the latch stays set.
- Observed effect in the test: no voice is queued, but the next valid pass inside the take is refused (`play()` returns false).
- Product impact is narrower than that. `Track.cpp:337` connects the pass latch's reset to `Song::playbackPositionJumped`, which `Song.cpp:229` emits only while playing. A seek during playback therefore clears the latch. The stuck latch can persist when the transport is stopped, and the wrapped span is undefined behaviour in every case.

## The fix

1. `src/core/BounceInPlace.cpp:333-337`: the non-tempo-map branch subtracts in `qint64` and converts the difference to `float` before the multiply:

   ```cpp
   return static_cast<float>(static_cast<qint64>(to) - static_cast<qint64>(from)) * framesPerTick;
   ```

2. `src/core/Track.cpp` (round 2): `playFrozenTake` maps every pass through `frozenTakeSourceFrame` and drops the no-tempo-map expression `static_cast<qint64>(std::llround((position - m_frozen.startTicks) * oneRate))`:
   - the pinned read keeps the map unconditionally (`:1499`), in force or not;
   - `adoptTempoMapForFrozenTake` keeps it unconditionally too (`:1431`), so an inactive map adopted later cannot leave a stale in-force copy that the helper would use;
   - the pass uses `frozenTakeSourceFrame(m_frozenLastMap, ...)` (`:1511`).

- **In-range results are unchanged.** For any difference representable in `int32`, `float(int64 diff)` and `float(int32 diff)` are the same correctly rounded value, and the multiply is still `float * float`. The old no-tempo expression and the helper's one-rate branch give the same `llround` input. Reasoned from the C++ conversion rules; the tests check exact in-range values.
- The product stays in `float`. A `double` product would change in-range rounding, which this fix does not do. Precision for spans near 2^31 ticks is a pre-existing float limit, not changed here.
- The map copy is a `std::array` of `MaxEvents = 128` events (`include/TempoMap.h:161`), so the copy does not allocate. It runs once per pass start, after the pass latch, not per period. The in-force case already copied it there.
- `Track.h` is unchanged. `m_frozenLastMapInForce` is still written but no longer branched on. Removing it changes `Track`'s layout, and `Track.h` has 121 includers, so that needs a full dependent rebuild. Deferred; flagged here.
- The tempo-map branch (`BounceInPlace.cpp:338`) subtracts doubles from `secondsAtTick`, so it has no tick subtraction to overflow.

## Regression tests

- `tests/src/core/BounceInPlaceTest.cpp`, slot `framesBetweenTicksSpansTheWholeTickRange` (lines 194-207): calls `framesBetweenTicks` with an inactive `TempoMap`, `framesPerTick = 100.0f`, and the `tick_t` extremes. It checks the forward span is positive and equals `4294967295 * 100` within relative `1e-6`, the reverse span is the negated count, and an in-range span `24..120` equals `96 * 100.0` exactly.
- `tests/src/core/FreezeRegionRepairTest.cpp`, slot `negativeTakeStartAndHugeSeekDoNotWrapTheSourceFrame` (round 2): installs a whole-track take starting at `-1` (`freezeTo(wav, -1, -1, {}, &why, false)`), calls `play()` at `0x7fffffff` (no clip there, so the take's result is the return value), then calls `play()` at tick 10 with no playback jump and no re-freeze. The first call must not queue a voice, and the second must still play.
  - The round-2 first draft checked only the first call. With the bypass it also returned false, so it could not tell the two apart (`test-1-red.log`, EXIT 0). The second call is the discriminator.
  - Cleanup (`removePlayHandlesOfTypes`) runs before each assertion, so a failing run leaves no queued voice.

## Commands and exit codes

Round-2 logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-307.r2/logs`. Round-1 logs are in `…/new-p16-307.r1/logs` (no longer in this run's working directories). All builds used `CCACHE_DISABLE=1` (see environment note below).

| # | Command (run from the worktree, or `build/tests` for ctest) | EXIT | Log / result |
|---|---|---|---|
| R2-1 | `cmake --build build --target FreezeRegionRepairTest -j4` (first draft of the new slot, before the Track fix) | 0 | `build-1-red.log` |
| R2-2 | `ctest -R '^FreezeRegionRepairTest$'` (first draft) | 0 | `test-1-red.log`: passes, so the first draft did not discriminate and was replaced |
| R2-3 | `cmake --build build --target FreezeRegionRepairTest -j4` (final slot, before the Track fix) | 0 | `build-2-red.log` |
| R2-4 | `ctest -R '^FreezeRegionRepairTest$' --output-on-failure` **RED, bypass present** | **8** | `test-2-red.log`: `FAIL! : FreezeRegionRepairTest::negativeTakeStartAndHugeSeekDoNotWrapTheSourceFrame() 'next' returned FALSE.`; Totals 11 passed, 1 failed |
| R2-5 | Track fix applied; `cmake --build build --target zene FreezeRegionRepairTest BounceInPlaceTest ControlBounceRenderTest -j4` | 0 | `build-3-green.log` |
| R2-6 | `ctest -R 'BounceInPlaceTest\|FreezeRegionRepairTest\|ControlBounceRenderTest' --output-on-failure` **GREEN** | 0 | `test-3-green.log`: 100% tests passed, 3/3 |
| R2-7 | Track only reversed: `git diff -- src/core/Track.cpp > track-fix.patch`; `git apply -R track-fix.patch`; rebuild the three targets (`build-4-track-reversed.log`) | 0, 0, 0 | |
| R2-8 | `ctest -R 'BounceInPlaceTest\|FreezeRegionRepairTest\|ControlBounceRenderTest' --output-on-failure` **Track reversed: must FAIL** | **8** | `test-4-track-reversed.log`: `FreezeRegionRepairTest::negativeTakeStartAndHugeSeekDoNotWrapTheSourceFrame() 'next' returned FALSE.`; 2 of 3 passed |
| R2-9 | `git apply track-fix.patch` (restore Track) | 0 | |
| R2-10 | Both product changes reversed: `git diff 37eaf07557 -- src/core/BounceInPlace.cpp src/core/Track.cpp > fix.patch`; `git apply -R fix.patch`; rebuild the three targets (`build-5-both-reversed.log`) | 0, 0, 0 | |
| R2-11 | `ctest -R ... --output-on-failure` **both reversed: must FAIL** | **8** | `test-5-both-reversed.log`: `BounceInPlaceTest::framesBetweenTicksSpansTheWholeTickRange() 'forward > 0.0' returned FALSE. (-100)` and the `FreezeRegionRepairTest` `'next'` failure; 1 of 3 passed |
| R2-12 | `git apply fix.patch` (restore both); rebuild the three targets (`build-6-restored.log`) | 0, 0 | |
| R2-13 | `ctest -V -R ... ` **restored: must PASS** | 0 | `test-6-restored-verbose.log`: 3/3 passed; Totals 8 (BounceInPlaceTest), 12 (ControlBounceRenderTest), 12 (FreezeRegionRepairTest), 0 failed |

Affected family: **3 `ctest` entries executed, 3 passed**. `BounceInPlaceTest` runs 8 test functions, `FreezeRegionRepairTest` 12, `ControlBounceRenderTest` 12.

### Gates

| # | Command | EXIT | Log |
|---|---|---|---|
| R2-14 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | Fails before any gate runs: `PermissionError` creating `<worktree>-feature-check`, outside the write-jail (same as round 1, `gates-1.log` in the round-1 folder) |
| R2-15 | Same gate functions and lists imported from `feature_check.py`, run in the worktree with logs in the round-2 folder; baseline is the cached `gate-baselines/37eaf07557…json` (merge base with `050/m0-train-21`) | 0 | `gates-table.txt`: `RESULT: PASS` |

Replicated table (`gates-table.txt`):

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

- The complexity and file-length scripts exit 1 on the train's existing baseline, which is expected. Their logs and the cached baseline contain no item for `Track.cpp`, `BounceInPlace` or `FreezeRegionRepairTest`.
- **Runner issue (not changed, reported):** `feature_check.py` builds `changed` from `git(...).stdout.strip()`, so the leading space of the first `git status --porcelain` line is stripped and `line[3:]` drops two characters. In this run the first entry came out as `rc/core/Track.cpp`, so the per-file filter cannot match `Track.cpp`. The ratchet logs show no `Track.cpp` items, so this run's result does not depend on it, but the filter should be fixed in the script.

### Registration

- `src/core/BounceInPlace.cpp` and `tests/src/core/BounceInPlaceTest.cpp` are in `tests/fork-sources.txt` (lines 1359 and 2366). `tests/src/core/FreezeRegionRepairTest.cpp` is in it too (line 2445). No new registration was needed for the test files.
- `src/core/BounceInPlace.cpp` (round 1) and `src/core/Track.cpp` (round 2) are edited inherited-registry files, so each has a line in `tests/upstream-modifications.txt` with its reason, in the same commit.

## Siblings checked and not changed

- `src/core/Track.cpp:365` subtracts `m_frozen.endTicks - m_frozen.startTicks` and `loopEnd - loopBegin` in `tick_t` to size the frozen-voice pool. Same class of overflow, but it feeds voice capacity on the control side (`updateFrozenVoiceCapacity`), not frame conversion. Not fixed here; a follow-up.
- `src/core/BounceInPlace.cpp:227` (`tickAfter`) adds `start + frames/framesPerTick` in `tick_t`. Inverse direction (frames to ticks), not part of this finding. Not fixed here.
- After round 2, `rg` for direct tick subtractions in `src/core/Track.cpp` finds no other subtraction on the frozen-take playback path; `:365` is the only one left, and it is the capacity sizing above.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Track-only reverted: `test-4-track-reversed.log` (EXIT 8). Both reverted: `test-5-both-reversed.log` (EXIT 8, 2 failing). Restored: `test-6-restored-verbose.log` (EXIT 0, 3/3).
- **The test is deterministic.** Constants only: an inactive `TempoMap` built in place, `framesPerTick = 100.0f`, `tick_t` extremes from `std::numeric_limits`, and the frozen take's start and seek ticks. No clock, sleep, thread race or randomness. The playback test uses the existing fixture (`trackWithOneClip`, `taketest::writeToneWav`). It calls `play()` directly and does not wait on the audio engine.
- **The fix is at the cause and complete.** Every frozen-take pass now maps through `frozenTakeSourceFrame`, the only place that converts a tick span to frames for playback. Its callers: `playFrozenTake` (the one playback path, `Track.cpp:1511`) and `BounceInPlace::prepareRenderTracks` (`BounceInPlace.cpp:384`, `:386`, bounded values). Both pinned-map sites copy the map unconditionally (`Track.cpp:1431`, `:1499`), so the helper always decides from the current map. Siblings are listed above, not hidden.
- **No collateral damage.** No limit or check was added. In-range results are unchanged (the reasoning above, plus the exact in-range assertions). No saved-data format, header or control command changed. `Track.h` is unchanged, so no layout change reaches the 121 includers. The family runs 3/3. The pinned-map copy is a fixed-size `std::array`, not a heap allocation.
- **Verdict is REAL, not ALREADY-FIXED or FALSE-POSITIVE.** Reachability is latent (crafted project plus a large seek, or a transport seek past a negative take start). The regression reaches `playFrozenTake` through `play()`, which is the path the crafted project would take.
- **Realtime.** `playFrozenTake` and `framesBetweenTicks` run on the audio thread. The change adds an integer widening, a float cast and an unconditional fixed-size map copy once per pass start (after the latch). No allocation, lock, log or wait.
- **Gate blocker.** The official `feature_check.py` cannot write its log directory outside the write-jail (R2-14). The replicated run (R2-15) passes. `submit_check.py` still shows the gates item as FAIL until the official script can run.
- **Environment issue.** The default ccache temp directory is not writable from this sandbox, so the builds used `CCACHE_DISABLE=1`. Compiled output is not affected by the cache setting.
