ALREADY-FIXED

# cluster new-p9-348-port-port: retained Patman converter modes

The reported defect (a quality change leaving held Patman notes on the old
resampler mode, and over-polyphony SincBest notes losing their converter) is
already fixed on `HEAD`. This cluster adds regression tests that pin the
existing behaviour; no product file changes in this branch.

## The r8 commit is not in this history

    git merge-base --is-ancestor cb06a9fbc3 HEAD   # exit 1

`cb06a9fbc3` (r8) is **not** an ancestor of `HEAD`. The fix is present as the
current product code, not as a cherry-pick of that commit.

## Evidence that the fix is already in the product

### (a) A quality change prepares, then adopts, every retained state

- `src/core/AudioEngine.cpp:298-320` --
  `AudioEngine::prepareConverterModeForRetainedStates()` prepares the realtime
  voice converter pool, the sample-voice pool, the song's instrument notes, and
  every live, new, and capacity-deferred play handle. Preparation only reserves
  converter states; nothing is allocated on the audio thread.
- `plugins/Patman/Patman.cpp:247-267` --
  `PatmanInstrument::prepareConverterModeForNote()`,
  `adoptPreparedConverterModeForNote()`, and
  `discardPreparedConverterModeForNote()` forward the engine's prepare/adopt/
  discard phases to each held note's `handle_data::state`. A held note therefore
  receives the prepared mode when the engine publishes it.
- `src/core/AudioResampler.cpp:219-264` -- the realtime voice converter pool's
  `prepareSlot()` creates and caches a converter for the requested mode, and
  `adoptSlot()` swaps the prepared converter into the slot and records
  `preparedMode`. This is the allocation the engineering change moves out of
  the render path.

### (b) The render thread never allocates a converter

- `src/core/AudioEngine.cpp:145-149` -- `realtimeVoiceConverterCapacity()`
  reserves one state per play handle per render playback state
  (`1 + CompSlipReaders::MaxReaders`), sized so every retained and pending
  handle already has a pool slot.
- `src/core/AudioResampler.cpp:392` -- when the pool is exhausted the
  constructor throws `std::runtime_error{"No prepared realtime voice converter
  is available"}` instead of allocating inside the render thread. The design
  therefore fails loudly rather than calling into libsamplerate on the audio
  thread.

## New tests

`tests/src/plugins/PatmanLoadTest.cpp` adds:

- `qualityChangeKeepsHeldPatmanSlotsAtTheNewMode()` -- holds three real Patman
  notes through `AudioEngine::applyConverterModeToRetainedStates()`, releases
  and reacquires them, and asserts each reacquired slot reports the requested
  `SrcQuality` mode.
- `overPolyphonyKeepsEverySincBestBlockAudible()` -- renders 200 notes plus 32
  notes beyond the nominal polyphony under `RealtimeScope`, and asserts every
  SincBest block is audible and matches the SincBest oracle.
- `sincBestReferenceDiffersFromLinearReference()` -- proves the oracle is a
  real SincBest render, not the Linear fallback.

`tests/upstream-modifications.txt` records the new test coverage.

## Mutation checks

Build command (from the repository root), run before each test invocation:

    cmake --build build --target zene PatmanLoadTest

Test command (from `build/tests`), `-V --output-on-failure` so a RED mutation
names the failing assertion:

    ctest -R PatmanLoadTest -V --output-on-failure

The builds all ended `[100%] Built target PatmanLoadTest` (exit 0). RED test
runs return CTest's failure status 8; the GREEN run returns 0. The stored logs
are under `~/.config/kads/zene/state/runs/new-p9-348-port-port.r10/logs/`.
T1 is `qualityChangeKeepsHeldPatmanSlotsAtTheNewMode()`, T2 is
`overPolyphonyKeepsEverySincBestBlockAudible()`, T3 is
`sincBestReferenceDiffersFromLinearReference()`.

| Mutation | Exact edit | RED log | RED exit | Observed failure |
| --- | --- | --- | --- | --- |
| M1 | `RealtimeVoiceConverterPool::adoptSlot` returns early | `005-test-m1.log` | 8 | T1 `reacquired.back().mode()` 1 != 4 (line 432); T2 `!blocksMatch(sincBestReference, linearReference)` false (line 512); T3 SincBest oracle indistinguishable from Linear (line 549) |
| M2 | `acquire()` returns nullptr after 4 SincBest acquisitions | `007-test-m2.log` | 8 | T2 aborts with an unhandled `std::runtime_error` (`No prepared realtime voice converter is available`), not a QCOMPARE |
| M3 | `PatmanInstrument::prepareConverterModeForNote` returns true without preparing | `009-test-m3.log` | 8 | T1 `reacquired.back().mode()` 1 != 4 (line 432) |
| M4 | `referenceBlock` oracle forced to Linear | `011-test-m4.log` | 8 | T2 231 mismatched blocks vs 0 (line 508); T3 SincBest oracle indistinguishable from Linear (line 555) |

M1 and M3 were re-run after T1's play handles were released: `033-test-m1.log`
and `035-test-m3.log` both end `EXIT=8` with T1 failing, and the restored tree
ends `EXIT=0` in `037-test-final.log`.

GREEN after reverting all four mutations:

    cmake --build build --target zene PatmanLoadTest   # 012-build-final.log, exit 0
    ctest -R PatmanLoadTest -V --output-on-failure     # 013-test-final.log, exit 0

Re-run at commit time: `ctest -R PatmanLoadTest --output-on-failure` from
`build/tests` -> exit 0, `100% tests passed out of 1`.

## feature_check

    python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <repo>
    # 020-feature-check.log, EXIT=0

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in these files) |
| file-length | PASS (no new item in these files) |

`RESULT: PASS`.

## Thread safety

TSan is not available in this environment. The argument is from the design and
the event-ordering tests: preparation happens on the control thread, adoption
is published before playback resumes, and the render thread only takes pool
slots that were reserved for its play handles. The tests use
`AudioEngine::modelChangeWaiters()` and `RealtimeScope` rather than timing
windows.

## Self-check

- [x] `cb06a9fbc3` is not an ancestor of `HEAD` (`git merge-base` exit 1).
- [x] Evidence cites the exact product lines for prepare/adopt (a) and the
      reserved render capacity (b).
- [x] All four mutations are RED and the reverted tree is GREEN.
- [x] `feature_check.py` has no new FAIL (`RESULT: PASS`, EXIT=0).
- [x] No product file changed; the tests pin existing behaviour.
- [x] New test functions stay within CCN <= 10 (complexity gate: no new item).
- [x] `submit_check.py` has no FAIL and every `?` item is answered.

### Answers to the pre-submit questions

1. **RED with the fix reverted, GREEN with it (exit codes).** M1-M4 each fail
   `PatmanLoadTest` (CTest exit 8; `005`, `007`, `009`, `011`). Reverting the
   mutations builds cleanly (exit 0) and the test passes (exit 0; `012`, `013`).
2. **Deterministic.** There are no sleeps or wall-clock deadlines. The load test
   uses the `AudioEngine::modelChangeWaiters()` event seam; playback tests use
   `RealtimeScope`, `framesPerPeriod()`, real `NotePlayHandle` objects, and
   bit-for-bit block comparison against a separately rendered SincBest oracle.
3. **Cause and complete.** The only caller of
   `applyConverterModeToRetainedStates()` is
   `AudioEngine::setSrcQuality()` (`AudioEngine.cpp:409`); it drives the prepare/
   adopt/discard hooks at `AudioEngine.cpp:388-393`, and the `Instrument`
   defaults (`include/Instrument.h:143-150`) keep every other instrument
   consistent. Patman is the only override (`Patman.h:66-68`).
4. **No collateral damage.** No product file or persistence format changed.
   Saved projects load through the unchanged file readers, and the tests add no
   new product limit: they use a temporary GF1 patch and the existing test seams
   only.
5. **ALREADY-FIXED evidence.** `src/core/AudioEngine.cpp:298-320` prepares every
   retained state, `plugins/Patman/Patman.cpp:247-267` forwards prepare/adopt/
   discard to held Patman notes, and `src/core/AudioResampler.cpp:219-264`
   caches the prepared converter before adoption. The reserved capacity at
   `src/core/AudioEngine.cpp:145-149` and the throw at
   `src/core/AudioResampler.cpp:392` keep allocation out of the render thread.

## Other findings

- The `005`/`007`/`009`/`011` logs predate this report and did not store
  per-command exit codes; their RED values are CTest's failure status (8).
  After T1 gained handle cleanup, M1 and M3 were re-run with explicit `EXIT`
  capture (`033`, `035`: `EXIT=8`) and the restored tree was re-run
  (`037`: `EXIT=0`).
- No product defect was found outside the packet. The over-polyphony path
  intentionally throws when the reserved pool is exhausted rather than
  allocating on the audio thread.
