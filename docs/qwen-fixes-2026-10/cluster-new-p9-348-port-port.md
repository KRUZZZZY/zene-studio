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

| Mutation | What it breaks | RED log | RED exit | Observed failure |
| --- | --- | --- | --- | --- |
| M1 | converter mode mapping degrades SincBest to Linear | `005-test-m1.log` | 8 | mode 1 != expected 4; SincBest oracle indistinguishable from Linear (lines 432, 512, 549) |
| M2 | realtime voice converter pool capacity cut below the reserved play-handle count | `007-test-m2.log` | 8 | `overPolyphonyKeepsEverySincBestBlockAudible()` caught an unhandled exception (`No prepared realtime voice converter is available`) |
| M3 | Patman notes do not adopt the prepared mode for held slots | `009-test-m3.log` | 8 | reacquired slot mode 1 != expected 4 (line 432) |
| M4 | over-polyphony Patman notes silently fall back to Linear when no prepared converter is free | `011-test-m4.log` | 8 | 231 mismatched blocks (expected 0, line 508); SincBest oracle indistinguishable from Linear (line 555) |

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

## Other findings

- The mutation logs predate this report and did not store per-command exit
  codes; the RED values above are CTest's failure status (8), and the GREEN
  exit 0 was re-run and captured.
- No product defect was found outside the packet. The over-polyphony path
  intentionally throws when the reserved pool is exhausted rather than
  allocating on the audio thread.
