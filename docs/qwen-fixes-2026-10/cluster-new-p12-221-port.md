FIXED

# Cluster new-p12-221-port: starting a sample clip allocated on the audio thread

## Verdict and cause

**Verdict: FIXED.** The finding is real: `SampleTrack::play()` runs on the render path and
built each started clip's `SamplePlayHandle` with `new`, and the handle's own per-period
buffer was a `std::vector<SampleFrame>` allocating in the same constructor; a comped clip's
comp-slip bundle was a third heap allocation on that path. Those allocations can block on
the allocator's lock, so a clip start was not realtime-safe.

## Fix (the commits)

- **180a4adc29** - `fix(sample-track): pool frame buffers and comp-slip readers, grown on
  the control thread`: `FixedBlockPool` in `include/BufferManager.h` / `src/core/BufferManager.cpp`
  plus `CompSlip` reservations, new `include/RealtimeFreeList.h`, handle/`PlayHandle` changes.
- **180e12a251** - `feat(sample-track): pooled SamplePlayHandle storage and tryCreate`:
  adds `src/core/SamplePlayHandlePool.cpp` and the `tryCreate` / `reserveBlocks` API.
- **d767ca125c** - `fix(sample-track): claim sample play handles from a pool, not new, on
  the audio thread` (7 files): `src/tracks/SampleTrack.cpp` claims from the pool; a refused
  start clears the latch and counts the deferral.
- **140c978e3b** - `fix(sample-track): size the frame and slip pools with the clip count`
  (`include/SampleClip.h`, `src/core/SamplePlayHandlePool.cpp`): pool size follows the clip
  count instead of a fixed 1024/4096 ceiling.
- **719c712198** - `test(new-p12-221-port): RED tests and test seam`: the two test files, the
  `AudioEngine` test seam in `include/AudioEngine.h`, and the test registrations.
- **604ea002a7** - `chore(new-p12-221-port): regenerate all-sources.txt ...` (see Disclosures).

Verified code points in this tree:

- `src/core/SamplePlayHandlePool.cpp:90-102` - `tryCreate` pops a pooled block and
  placement-constructs; `:92-93` returns nullptr on an empty pool; `:105-115` -
  `reserveBlocks(n)` allocates blocks off the render path and pushes them.
- `src/tracks/SampleTrack.cpp:304` - `tryCreate`; `:305` null check; `:309`
  `setIsPlaying(false)`; `:310` `s_deferredClipStarts.fetch_add(1)`; `:311`
  `requestPlayHandleCapacityRefresh()`; `:312` `continue` (return false at `:325`).
  Success path sets the latch at `:256` (`sClip->setIsPlaying(true)`).
- `src/core/AudioEngine.cpp:508` - `m_newPlayHandles(PlayHandle::MaxNumber)` (staging queue
  bound); `:1497-1509` - `addPlayHandle` pushes to `m_newPlayHandles` or disposes;
  `:1546-1571` - `topUpPlayHandleCapacityOnControlThread()`, the control-thread top-up called
  by the 100 ms timer at `:563-571`; `:1630` - `removePlayHandlesOfTypes` (the cleanup the
  tests use).

## Test matrix

| Area | Slot / evidence | Result |
| --- | --- | --- |
| normal path | `SampleClipStartAllocationTest::playStartsPlainClipWithoutAllocating` (plus comped and preserve-pitch slots) | GREEN, 0 allocations (`test-1-green.log`, `test-green2.log`) |
| refusal | `SampleClipPoolExhaustionTest::aRefusedStartIsDeferredThenLandsAfterTheControlThreadGrowsThePool` D/P1/G/P2 (`:339-421`) | GREEN 3 passed slot (`test-green3-slot.log`) |
| exhaustion | `startsBeyondTheListCapacityAllSoundWithoutAllocating` (:330): 1200 starts across 20 tracks, 1199 live, above the 1024-slot list | GREEN; RED at `:330` when the two fixes are reversed (`test-red-pool.log`) |
| audio-thread allocation probe | thread-local allocation probe armed around exact `play()`/`renderNextPeriod()` calls, plus interposed `__cxa_allocate_exception`; `QCOMPARE(refused.allocations, 0)` `:419`, `QCOMPARE(retried.allocations, 0)` `:420` | GREEN; RED shows `allocations: 1` when the fix is reversed |
| platform | Only C++20 standard facilities; no Qt API, no `windows.h`, no `std::filesystem` on the changed render path. No TSan run (see Unverified) | argued from the design, not measured |

## RED/GREEN evidence

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-221-port.r10/logs`.

**The literal full-range reverse is not used as the RED run.** `git apply -R` of
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-221-port.r10/logs/fix.patch`
(`git diff 719c712198^..HEAD -- src include`) would remove `tryCreate` and `reserveBlocks`
(added by 180e12a251) that the tests call, and it removes 719c712198's `AudioEngine` test
seam. That would be a compile failure, not a RED, so it is not used as the RED run below.
It was not re-run in this round.

**RED therefore reverses only d767ca125c + 140c978e3b** - the behavioural fix, as
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-221-port.r10/logs/fix-behaviour.patch`
(**138 lines**; `git diff 180e12a251..140c978e3b -- src include`, `cmp` against it
EXIT=0), leaving the API and the test seam in place.

- RED (`git apply -R fix-behaviour.patch` EXIT=0, build label `build-red` EXIT=0):
  `ctest -R 'SampleClip(StartAllocation|PoolExhaustion)Test' --output-on-failure` in
  `build/tests`, label `test-red` -> **EXIT=8** (`RESULTS.txt`), exactly **five FAIL!
  lines** in `test-red.log`:
  - `SampleClipStartAllocationTest` 3 FAIL!: `allocations` Actual 1, Expected 0 at `:131`,
    `:159`, `:184`;
  - `SampleClipPoolExhaustionTest` 2 FAIL!: `allocP1` Actual 1000 at `:330`, and
    `'!drained.empty()' returned FALSE` at `:377`.
  - Alone: label `test-red-start` -> **EXIT=3** (`RESULTS.txt`; `test-red-start.log`,
    Totals 2 passed, 3 failed); label `test-red-pool` -> **EXIT=2** (`RESULTS.txt`;
    `test-red-pool.log`, Totals 2 passed, 2 failed at `:330` and `:377`).
- Restore (`git apply fix-behaviour.patch` EXIT=0; `git status --short` empty): build label
  `build-green2` EXIT=0; `ctest` label `test-green2` -> **EXIT=0**, 2/2
  (`test-green2.log`). p1 `test-1-green` -> **EXIT=0**, 2/2 (`test-1-green.log`); family
  `test-2-family` -> **EXIT=0**, **11/11** (`test-2-family.log`).

**The refusal slot's RED under that reversal stops at the D precondition (:377), not at the
P1/P2 assertions.** With the pool fix reversed the pool is never populated, so the drain
holds no blocks and the test fails at `'!drained.empty()' returned FALSE` (`:377`).
`state-sequence.md` records this explicitly, and `test-red.log:40` / `test-red-pool.log:15`
show it.

**P1/P2 order assertions were shown RED separately by temporary mutations** in
`p3-notes.txt` (restored clean after each):

- **M1** (drop `s_deferredClipStarts.fetch_add` at `SampleTrack.cpp:310`): build
  `build-mut-M1` EXIT=0, run **EXIT=1**, `FAIL! : ...SampleClipPoolExhaustionTest.cpp(404)`
  - deferred delta 0, expected 1 (P1); `test-mut-M1-slot.log`.
- **M2b** (drop `sClip->setIsPlaying(true)` on the success path at `:256`): build
  `build-mut-M2b` EXIT=0, run **EXIT=1**, `FAIL! : ...SampleClipPoolExhaustionTest.cpp(411)`
  - `'clip->isPlaying()' returned FALSE` (P2); `test-mut-M2b-slot.log`.
- After each, `git apply -R` of `mut-M1.patch` / `mut-M2b.patch` EXIT=0,
  `git status --short` empty, and `cmp` of `git diff 180e12a251..140c978e3b` against
  `fix-behaviour.patch` EXIT=0; clean-tree slot re-run `test-green3-slot.log` 3 passed,
  EXIT=0.

## State sequence (from /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-221-port.r10/logs/state-sequence.md)

Slot `SampleClipPoolExhaustionTest::aRefusedStartIsDeferredThenLandsAfterTheControlThreadGrowsThePool` (`tests/src/tracks/SampleClipPoolExhaustionTest.cpp:339-421`):

| # | State | Test assertion / call (file:line) | Production cause (file:line) |
|---|-------|-----------------------------------|------------------------------|
| D | Drain: pool runs empty | `tryCreate` loop `:370-375`; `QVERIFY2(poolEmptied, ...)` `:376`; `QVERIFY2(!drained.empty(), ...)` `:377` | `SamplePlayHandle::tryCreate` `src/core/SamplePlayHandlePool.cpp:90-93`: `pool().pop()` null -> `return nullptr`. Primed by `reserveForClips` (`:117-128`), called from `src/core/SampleClip.cpp:63` (ctor) and `:118` (copy ctor), and `src/core/AudioEngine.cpp:1570`. |
| P1 | Refused `play()`: returns false, latch clear, deferred +1, 0 live handles | `armedTrackPlay()` `:385-392` (`RealtimeScope` `:386`, `track.play(...)` `:389`), called `:396`; assertions `!refused.started` `:402`, `!clip->isPlaying()` `:403`, deferred delta == 1 `:404`, `handlesFromTrack(track) == 0` `:405` | `src/tracks/SampleTrack.cpp:304` `tryCreate` fails; `:305` null check; `:309` `setIsPlaying(false)`; `:310` `s_deferredClipStarts.fetch_add(1)` (counter `:112`, accessor `:133-135`); `:311` `requestPlayHandleCapacityRefresh()`; `:312` `continue`, so `played_a_note` stays false (`:162`) and `:325` returns false. |
| G | Control thread grows the pool by one block, outside `RealtimeScope` | `SamplePlayHandle::reserveBlocks(1)` `:408`; its effect asserted by P2 `:410`/`:416`. The enclosing `armedTrackPlay` scope ended at `:392` and the render scope at `:401`, so `:408` is outside any `RealtimeScope`. | `src/core/SamplePlayHandlePool.cpp:105-115`: `while (pool().count() < blocks)` allocates, sets `header->pooled = true`, appends to `owners()`, pushes to `pool()`. `reserveForClips` skips under `RealtimeScope` (`:119`). |
| P2 | Retry at the same position lands: returns true, latch set, 1 live handle | `armedTrackPlay()` `:409`; assertions `retried.started` `:410`, `clip->isPlaying()` `:411`, `handlesFromTrack(track) == 1` `:416` | `src/tracks/SampleTrack.cpp:304` `tryCreate` now succeeds (`SamplePlayHandlePool.cpp:95-102`); latch set at `SampleTrack.cpp:256`; handle added `:320`; `played_a_note = true` `:321`; returned true `:325`. |

Ordering facts: D -> P1 -> G -> P2 is forced by straight-line statements `:370-416` on one
thread; the allocation probe runs on both `play()` calls (`:396`, `:409`); the allocation
checks run last (`:419`, `:420`). `rg -n 'sleep|QThread|qWait'` on the test file returns no
matches (exit 1).

## Gates

`gate-1.log`: `feature_check.py` on this tree -> **EXIT=0** (`RESULTS.txt`, label
`p4-a-gate1`), `RESULT: PASS`, **8 PASS / 0 FAIL** (fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity "no new item
in your files", file-length "no new item in your files").

## Disclosures

- **No `schemas/proof.json` exists on disk** and `submit_check.py` does not validate a
  manifest (see
  `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-221-port.r10/logs/schema-notes.txt`).
  `proof.json` therefore uses the brief's field names (the regression-test keys are from the
  brief's example; the state-sequence keys `kind`, `test_file`, `test`, `states` are a
  reading of the brief) and has not been checked against a schema.
- **604ea002a7 regenerated `tests/all-sources.txt`** by the file's own header recipe **after
  the "no more edits" checkpoint**. `git show --stat` confirms it touches **only
  `tests/all-sources.txt`** (4 insertions, 0 deletions; the four added paths), **no `src/`,
  `include/` or test file**. It is a mechanical list regeneration, not a product or test change.

## Self-check

**? The regression test FAILS with your product change reverted and PASSES with it (exit
codes).** RED: build `build-red` EXIT=0, `test-red` **EXIT=8** (5 FAIL!:
`SampleClipStartAllocationTest` `:131/:159/:184` allocations=1;
`SampleClipPoolExhaustionTest` `:330` allocP1=1000, `:377` empty pool). GREEN: build
`build-green2` EXIT=0, `test-green2` **EXIT=0**, 2/2; `test-1-green` **EXIT=0** 2/2,
`test-2-family` **EXIT=0** 11/11. The P1/P2 assertions were additionally RED under
M1 (EXIT=1, `:404`) and M2b (EXIT=1, `:411`).

**? The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
injected values it uses).** Seam: a thread-local allocation probe plus an interposed
`__cxa_allocate_exception`, armed around exact `play()` / `renderNextPeriod()` calls on this
thread; the dummy audio device is stopped in `initTestCase`. Sources are fixed frames (a
`std::sin` tone, a two-level step take), transport set explicitly with `Song::setPlayPos`.
Cleanup is `AudioEngine::removePlayHandlesOfTypes` (`AudioEngine.cpp:1630`), which sweeps the
active list and staged queue, so no wait is needed. No `qWait`, `sleep` or timeout in the
test file (grep exit 1).

**? The fix is at the cause and complete: every caller and sibling was checked and stays
consistent.** `SamplePlayHandle::tryCreate` has one product caller
(`src/tracks/SampleTrack.cpp:304`); other constructions (`src/gui/FileBrowser.cpp`,
`src/core/ControlCommandsBrowserMedia.cpp`, a preview handle) are off the render path.
`BufferManager::acquire` has one caller (`src/core/PlayHandle.cpp:42`);
`src/core/AudioBusHandle.cpp:78` uses `acquireBlocking()`, not `acquire`;
`CompSlipReaders::create` has one (`SamplePlayHandle::snapshotComp`), and its predicate is
shared with `slipsComp()`. `FixedBlockPool`'s accessors were rewritten with the segment list.
`AudioEngine.cpp:1497-1509` (add path) and `:1546-1571` (control-thread top-up) stay on the
control thread; `:508` remains the staging bound.

**? No collateral damage: old projects and saved data still load as before, and any limit or
check added does not hit other callers that legitimately exceed it.** No file format, project
data, undo stack or command was touched. The pools grow (`reserveBlocks`,
`reserveForClips`) instead of capping, so a project with more live clips
than the starting pool is delayed one pass and then starts;
`startsBeyondTheListCapacityAllSoundWithoutAllocating` starts 1200 clips across 20 tracks
(1199 live, one one-tick clip ended), above the 1024-slot list; no slot covers a period
re-init, so none is claimed.

**? If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code or intended contract.** Not
applicable; the verdict is FIXED. The defect was real (`SampleTrack::play` constructed with
`new`), and the fix is in the commits above.

## Unverified

- **TSan does not run here.** Races are argued from the design (atomic bitmap claims, relaxed
  flags, growth only on the engine's control thread outside `RealtimeScope`), not measured.
- **The audio thread is covered by the allocation probe only**; there is no instrumented
  measure of lock/wait time on the render path.

## Other findings

- The base report's citation `AudioEngine.cpp:1385-1420` for `removePlayHandlesOfTypes` is
  stale; in this tree the function is at `src/core/AudioEngine.cpp:1630`. Corrected here.
- `src/core/SamplePlayHandle.cpp` still exists (changed by 180a4adc29) alongside
  `src/core/SamplePlayHandlePool.cpp`; the pool implementation cited by the packet is the
  latter (`:90-115`), and the state-sequence table uses those line numbers.
