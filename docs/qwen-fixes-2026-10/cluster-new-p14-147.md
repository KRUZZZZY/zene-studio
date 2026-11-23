FIXED

# Cluster new-p14-147: an underrun resumes from stale buffered audio

The finding (`src/core/LinkAudioStream.cpp:185` historically) says the RX path marks an
underrun when the ring holds less than one period but leaves those partial frames in the
ring, so playback resumes with audio that was queued before the silence interval.

Verdict: **REAL**. The cause is `LinkAudioStream::receivePeriod()`: on `fill < period` it
counted the underrun, cleared `m_primed` and output silence, but never consumed the partial
frames. They stayed at the head of the ring and were played as soon as the buffer reached
its target again - a discontinuity plus a delay the `latency` report did not show (it
reported the stale frames, not 0).

**Round 0 note.** The branch had no commit of mine to build on (`git log --oneline -3`
shows only the `050/m0-train-50` merges, `git status` was clean), and the log folder was
empty; so there was no earlier round-0 patch to re-examine. Everything below is from this
session.

## The fix

- `src/core/LinkAudioStream.cpp:263-272` - the underrun branch now discards the partial
  frames with the underrun (`m_rx.skip(fill)`, line 270) before clearing `m_primed`, so the
  re-prime starts on fresh audio. The existing recomputed `left = m_rx.fill()` latency
  report two lines below is untouched: it now reports the emptied ring (0).
- `include/LinkAudioStream.h:284-287` - the policy comment on `m_primed` states the rule
  (a period the ring cannot fill is an underrun; its partial frames are discarded with it).
- `include/LinkAudioStream.h:156` - the `underruns` field comment says the partial frames
  are dropped, not only that the period was silent.
- `docs/LINK-AUDIO.md:51-55` - the doc said "An empty buffer is an underrun"; the code's
  rule is `fill < period`, and the doc now says so, plus what happens to the partial frames
  and that the reported latency is 0 after one.

Realtime: `AudioRing::skip()` is the consumer's counter update (atomics only, no
allocation, no lock, no log) - `audioThreadAllocatesNothing` still counts 0 `operator new`
over 200 periods. No control command, undo step or control-command snapshot is touched.
No new product file, so no registration change is needed: `src/core/LinkAudioStream.cpp` and
`include/LinkAudioStream.h` and the three test files are already listed in
`tests/fork-sources.txt` (lines 788, 789, 1201, 2101, 2854, 2855).

## The regression test

`tests/src/core/LinkAudioStreamTestParts2.h:124`, slot
`tests/src/core/LinkAudioStreamTest.cpp:146`, name
`LinkAudioStreamTest::anUnderrunDiscardsTheStalePartialFrames`. It uses only the existing
`LinkAudioStreamTestAccess` seams (`enableReceiver`, `writeRx`): no socket, no thread, no
sleep, no wall clock. From the finding's own procedure:

| step | what the test drives |
|---|---|
| warm-up | one period, to take the epoch bump of `enableReceiver` |
| prime | 1000 frames of `1.0` at 48 kHz, `latencyMs = 5` (period 480, target clamped up to 960) |
| drain | two periods: 1000 -> 520 -> 40 frames, both audible (`1.0`) |
| underrun | the third period: `40 < 480`, `underruns == 1`, the whole period silent |
| re-prime | 960 frames of `2.0`, next period must start at `2.0` and `underruns` stay `1` |
| report | `latencyMs == 10.0` (960 fresh frames less one period): no stale frames are left |

Without the fix the same code path starts that period with `1.0f` and reports `10.833333`
(520 stale frames). Both assertions fail, the first one first.

Also covered by the existing suite in the same binary, all passing: the normal path
(`loopbackExchangeIsSampleExact`), ring exhaustion (`droppedGapSilenceIsCountedAsAnOverrun`),
reconfiguration (`aSampleRateChangeDropsTheOldRateRxFrames`,
`anOldRatePacketIsNotPlayedAtTheNewRate`), the audio-thread allocation count, and the
policy-sensitive stress test `realtimeStressReportsBoundedUnderrunsAndLatency` - the
discard policy did **not** push it: `underruns=0`, `audible_frames=142560` of 144000.

## Commands and exit codes (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-147.r1/logs`)

RED (test first, product code unchanged) - `build-2-red.log`, `test-2-red.log`:

```
cmake --build build --target LinkAudioStreamTest -j4 > .../build-2-red.log 2>&1;                    EXIT=0
cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure -O .../test-2-red.log        EXIT=8
FAIL!  : LinkAudioStreamTest::anUnderrunDiscardsTheStalePartialFrames() Compared floats ...
   Actual   (mix.front().left()): 1 (0x1p+0)
   Expected (2.f)               : 2 (0x1p+1)
   Loc: [../../tests/src/core/LinkAudioStreamTestParts2.h(159)]
Totals: 23 passed, 1 failed, 0 skipped, 0 blacklisted
```

GREEN (fix applied) - `build-4-green.log`, `test-5-green.log`:

```
cmake --build build --target LinkAudioStreamTest -j4 > .../build-4-green.log 2>&1;                 EXIT=0
cd build/tests && ctest -V -R '^LinkAudioStreamTest$' -O .../test-5-green.log                      EXIT=0
PASS   : LinkAudioStreamTest::anUnderrunDiscardsTheStalePartialFrames()
Totals: 24 passed, 0 failed, 0 skipped, 0 blacklisted, 6094ms
```

4b, product patch reversed - `fix.patch`, `build-5-revert.log`, `test-6-revert.log`:

```
git diff -- src/core/LinkAudioStream.cpp include/LinkAudioStream.h > .../fix.patch                 EXIT=0
git apply -R .../fix.patch                                                                         EXIT=0
cmake --build build --target LinkAudioStreamTest -j4 > .../build-5-revert.log 2>&1;                 EXIT=0
cd build/tests && ctest -V -R '^LinkAudioStreamTest$' -O .../test-6-revert.log                     EXIT=8
FAIL!  : ... Actual (mix.front().left()): 1  Expected (2.f): 2
Totals: 23 passed, 1 failed
```

4b, product patch re-applied - `build-6-reapply.log`, `test-7-reapply.log`:

```
git apply .../fix.patch                                                                            EXIT=0
cmake --build build --target LinkAudioStreamTest -j4 > .../build-6-reapply.log 2>&1;                EXIT=0
cd build/tests && ctest -V -R '^LinkAudioStreamTest$' -O .../test-7-reapply.log                    EXIT=0
PASS   : LinkAudioStreamTest::anUnderrunDiscardsTheStalePartialFrames()
Totals: 24 passed, 0 failed
```

Header changed, so every target that includes it was rebuilt (`zene`,
`LinkAudioCodecTest`; `LinkAudioStreamTest` above) - `build-7-zene.log`:

```
cmake --build build --target zene LinkAudioCodecTest -j4 > .../build-7-zene.log 2>&1;               EXIT=0
```

Family - `test-8-family.log`:

```
cd build/tests && ctest -R '^(LinkAudioStreamTest|LinkAudioCodecTest|ControlLinkSync|ControlLinkInterop)$' \
  -O .../test-8-family.log --output-on-failure                                                     EXIT=0
1/4 LinkAudioStreamTest  Passed   6.65 sec
2/4 LinkAudioCodecTest   Passed   0.04 sec
3/4 ControlLinkSync      Skipped 34.04 sec        (pre-existing skip: this build has no Ableton Link)
4/4 ControlLinkInterop   Skipped  0.30 sec        (same)
100% tests passed out of 4
```

Gates - `gates-1.log`:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>                EXIT=0
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
        no-tautology / doc-figures
  PASS  complexity      no new item in your files
  PASS  file-length     no new item in your files
RESULT: PASS
```

## Test matrix

| path | covered by | result |
|---|---|---|
| normal (primed, period plays) | new test (drain rows), `loopbackExchangeIsSampleExact` | pass |
| failure (underrun with partial frames) | new test | red without fix, green with it |
| exhaustion (ring full / gap silence) | `droppedGapSilenceIsCountedAsAnOverrun` | pass |
| reconfiguration (sample-rate change flush) | `aSampleRateChangeDropsTheOldRateRxFrames`, `anOldRatePacketIsNotPlayedAtTheNewRate` | pass |
| audio-thread constraints | `audioThreadAllocatesNothing` (0 allocations / 200 periods) | pass |
| boundedness under drift/loss | `realtimeStressReportsBoundedUnderrunsAndLatency` | pass (underruns 0) |
| platform | POSIX path is what was built and run; the fix touches no socket code, and the Windows path stays unverified exactly as before | n/a |

## Self-check

- **A regression test that fails without the product change**: `fix.patch` reverse step above,
  EXIT=8 with `Actual 1 / Expected 2`, then apply + rebuild, EXIT=0. Both runs are in the log
  folder and in `proof.json` (`red_exit` 8, `green_exit` 0).
- **The cause, and its siblings**: the only consumer of the RX ring is `receivePeriod()`
  (`m_rx.read` in `addFromRing`, `m_rx.skip` in this branch and the two epoch/rate flushes).
  The rate-change paths already dropped everything (`m_rx.skip(m_rx.fill())`), so this branch
  is the one place partial frames survived; nothing else reads or assumes the stale frames.
- **No collateral damage**: one counter and one ring drop, no API, no format, nothing saved
  or loaded changes; every other test in the binary passes unchanged.
- **Realtime / undo / commands**: `skip()` is a consumer counter update on a preallocated
  ring (no allocation, lock, wait or log - `audioThreadAllocatesNothing` = 0). No undo step
  and no control command added, so `ControlCommandsSnapshot` cannot move.
- **Registration and gates**: no new file; the changed files are already in
  `tests/fork-sources.txt`. `feature_check.py` prints `RESULT: PASS`, EXIT=0.

## Other findings

- `docs/LINK-AUDIO.md` still calls the stress test's underrun bound "1% of periods" while
  the assertion is `underruns <= 30` of 3000 (exactly 1%) - consistent, left alone.
- `tests/src/core/LinkAudioStreamTest.cpp` declares two duplicate doc-comment blocks above
  `aSampleRateChangeDropsTheOldRateRxFrames`; cosmetic, out of scope here.
- The sent-back note asks for a diagnostic checkpoint on the lane (batch size 1, a capable
  model, zero-commit runs requeued). That is an orchestration setting this job cannot
  change; recorded here so the next reviewer sees it was read.
