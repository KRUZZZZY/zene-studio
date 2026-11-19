ALREADY-FIXED

# cluster new-p9-348-port-port: retained Patman converter modes

The reported defect (a quality change leaving held Patman notes on the old
resampler mode, and over-polyphony SincBest notes losing their converter) is
already fixed on `HEAD`. This cluster adds regression tests and a
`LMMS_TESTING`-only deletion counter beside the existing creation counter. No
product behaviour changes: outside `LMMS_TESTING` every converter release is the
same plain `src_delete` as before.

## The r8 commit is not in this history

    git merge-base --is-ancestor cb06a9fbc3 HEAD   # EXIT=1

`cb06a9fbc3` (r8) is **not** an ancestor of `HEAD`. The fix is present as the
current product code, not as a cherry-pick of that commit. The r8 review's cited
lines therefore belong to a lineage that is absent here: on this `HEAD`,
`plugins/Patman/Patman.cpp:618-677` is `PatmanView::openFile()` (GUI file
dialog), not the slot-pool code the review describes.

## Round 11: audit result and p1 caller table

The read-only audit (`logs/001-audit.md`) found no render-path caller that
creates, frees or switches a converter. Verdict: **no real render-path defect**
in this lineage; the report's cited `HEAD` lines are still accurate.

| Site | What it is | Can it run on the render path? |
| --- | --- | --- |
| `include/Sample.h:67` | `PlaybackState::setConverterMode()` inline -> `m_resampler.setMode()` | Definition only; reachable only from SlicerT. No direct render-path call. |
| `plugins/SlicerT/SlicerT.cpp:126` | `SlicerT::applyConverterMode()` calls `slot.state.setConverterMode(mode)` | No: `applyConverterMode` has zero call sites in `src plugins include`; `playNoteImpl` makes no converter call. |
| `src/core/AudioResampler.cpp:511` | `AudioResampler::setMode()` -> `trySetMode()` | No render-path caller; `Sample::play` never calls it. |
| `src/core/AudioResampler.cpp:530` | `AudioResampler::trySetMode()` definition | Called only by `setMode()`. No render-path caller. |
| `include/AudioResampler.h:151,155` | declarations of `setMode`/`trySetMode` | Declarations; no caller. |

The only `setConverterMode` call site (`SlicerT.cpp:126`) is unreachable dead
virtual code today. The render-path play functions (`Sample::play`,
`PatmanInstrument::playNoteImpl`, `AudioFileProcessor::playNoteImpl`,
`SlicerT::playNoteImpl`) contain no converter create/free/switch.

## The deletion-counter hook and what it covers

`src/core/AudioResampler.cpp` routes every converter release through
`deleteConverterState()`: the pool's `adoptSlot()` replacement, the chunk build
failure path, `discard()`, the pool destructor, and `AudioResampler::
StateDeleter`. The helper increments `s_realtimeConverterDeletions` only under
`LMMS_TESTING` and only while `RealtimeScope` is active; otherwise it is a plain
`src_delete` with no count, allocation, lock or log. The matching seams are
declared under `#ifdef LMMS_TESTING` in `include/AudioResampler.h`.

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

- `qualityChangeKeepsHeldPatmanSlotsAtTheNewMode()` (T1) -- holds three real
  Patman notes through `AudioEngine::applyConverterModeToRetainedStates()`,
  releases and reacquires them, and asserts each reacquired slot reports the
  requested `SrcQuality` mode. Strengthened in r11: the counted render must also
  delete zero converters.
- `lazyRenderPathConversionIsDetected()` (T4) -- holds three real notes across a
  quality switch, reads each note's own `Sample::PlaybackState` through
  `PatmanInstrument::handle_data` (not a layout mirror), asserts every note
  already reports the new mode before rendering, and asserts the following
  `RealtimeScope` render creates and deletes zero converters. It also keeps a
  prepared-but-not-adopted state pending through a render.
- `overPolyphonyKeepsEverySincBestBlockAudible()` (T2) -- renders 200 notes plus
  32 notes beyond the nominal polyphony under `RealtimeScope`, asserts every
  SincBest block is audible and matches the SincBest oracle, and (r11) asserts
  a held-note switch render creates and deletes zero converters.
- `sincBestReferenceDiffersFromLinearReference()` (T3) -- proves the oracle is a
  real SincBest render, not the Linear fallback.

`tests/upstream-modifications.txt` records the test coverage and the
`LMMS_TESTING`-only counter/friend hooks.

## Mutation checks (r11 logs)

Build before each test:

    cmake --build build --target zene PatmanLoadTest

Test, from `build/tests`, `--output-on-failure` so a RED mutation names the
failing assertion:

    ctest -R PatmanLoadTest --output-on-failure

Every build below ended `EXIT=0`; every RED test run ended `EXIT=8` (CTest
failure); the reverted tree ends `EXIT=0`. Logs are under
`~/.config/kads/zene/state/runs/new-p9-348-port-port.r11/logs/`.

| Mutation | Exact edit | Build log / EXIT | RED log / EXIT | Observed failure |
| --- | --- | --- | --- | --- |
| M1 | `RealtimeVoiceConverterPool::adoptSlot` returns early | `024-m1-build.log` / 0 | `025-m1-test.log` / 8 | T1 `reacquired.back().mode()` 1 != 4 (line 453); T4 `state->converterMode()` 1 != 4 (520); T2 `!blocksMatch(sincBestReference, linearReference)` false (604); T3 oracle indistinguishable from Linear (677) |
| M2 | `acquire()` returns nullptr after 4 SincBest acquisitions | `026-m2-build.log` / 0 | `027-m2-test.log` / 8 | T2 aborts with an unhandled `std::runtime_error` (`No prepared realtime voice converter is available`), not a QCOMPARE |
| M3 | `PatmanInstrument::prepareConverterModeForNote` returns true without preparing | `028-m3-build.log` / 0 | `029-m3-test.log` / 8 | T1 `reacquired.back().mode()` 1 != 4 (line 453); T4 `state->converterMode()` 1 != 4 (520) |
| M4 | `referenceBlock` oracle forced to Linear | `030-m4-build.log` / 0 | `031-m4-test.log` / 8 | T2 `!blocksMatch(sincBestReference, linearReference)` false (605); T3 oracle indistinguishable from Linear (678) |
| M5 | the note's prepared-mode adoption is skipped, so the render path lazily converts | `032-m5-build.log` / 0 | `033-m5-test.log` / 8 | T1 realtime allocations 3 != 0 (431); T4 realtime allocations 3 != 0 (514); T2 realtime allocations 3 != 0 (627) |

The stale line numbers in the older table are replaced above by the values in
the r11 logs (the test file grew again when T4 was added). After each mutation
the edit was reverted and the same build/test pair was rerun: restored build
`034-final-build.log` `EXIT=0`, restored test `035-final-test.log` `EXIT=0`
(`100% tests passed out of 1`). The strengthened suite after the counter and T4
were added: `022-strengthened-build.log` `EXIT=0`,
`023-strengthened-test.log` `EXIT=0`.

## Six focused ctest runs

From `build/tests`:

    ctest -R 'AudioResamplerPoolMemoryTest|ControlDeviceCatalogueTest|PatmanLoadTest|AudioPluginTest|RtSafetySweep|RtSafetySelfTest' --output-on-failure

`AudioResamplerPoolMemoryTest`, `ControlDeviceCatalogueTest`, `PatmanLoadTest`,
`AudioPluginTest`, `RtSafetySweep`, and `RtSafetySelfTest` all pass:
`100% tests passed out of 6`, **EXIT=0**. Log:
`.../new-p9-348-port-port.r11/duet/14-worker/tmp/002-r11-six-focused.log`.
The p1 baseline run of the same six is `.../duet/02-worker/tmp/003-p1-test-baseline.log`
(also EXIT=0).

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

## submit_check

    python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <repo> new-p9-348-port-port
    # 021-submit-check.log, rerun after the commit

| Item | Result |
| --- | --- |
| commits on the branch | PASS |
| working tree clean | PASS (after the commit) |
| report | PASS |
| tests added or changed | PASS (claim-only closure: independent QA must verify the cited code/contract) |
| gates (feature_check.py) | PASS (all eight gates) |

`RESULT: PASS`; the five `?` items are answered in `## Self-check`.

## Thread safety

TSan is not available in this environment. The argument is from the design and
the event-ordering tests: preparation happens on the control thread, adoption
is published before playback resumes, and the render thread only takes pool
slots that were reserved for its play handles. The tests use
`AudioEngine::modelChangeWaiters()` and `RealtimeScope` rather than timing
windows.

## Self-check

- [x] `cb06a9fbc3` is not an ancestor of `HEAD` (`git merge-base` EXIT=1).
- [x] The r11 audit found no render-path caller that creates, frees or switches
      a converter; the only `setConverterMode` site is dead SlicerT code.
- [x] The deletion counter is `LMMS_TESTING`-only and wraps every pool/resampler
      release; non-testing releases stay plain `src_delete`.
- [x] T1/T2 assert zero creations and zero deletions; T4 checks each held note's
      real `PlaybackState` mode and the counted render.
- [x] All five mutations are RED and the reverted tree is GREEN.
- [x] `feature_check.py` has no new FAIL (`RESULT: PASS`, EXIT=0).
- [x] No product behaviour changed; only the `LMMS_TESTING`-only counter and
      friend hooks were added.
- [x] New test functions stay within CCN <= 10 and the file is at or below 500
      code lines (both gates: no new item).
- [x] `submit_check.py` has no FAIL and every `?` item is answered.

### Answers to the pre-submit questions

1. **RED with the fix reverted, GREEN with it (exit codes).** M1-M5 each fail
   `PatmanLoadTest` (CTest EXIT=8; `025`, `027`, `029`, `031`, `033`); each
   mutation build is EXIT=0. Reverting the mutations builds cleanly
   (`034`, EXIT=0) and the test passes (`035`, EXIT=0, 100% of 1).
2. **Deterministic.** There are no sleeps or wall-clock deadlines. The load test
   uses the `AudioEngine::modelChangeWaiters()` event seam; playback tests use
   `RealtimeScope`, `framesPerPeriod()`, real `NotePlayHandle` objects, and
   bit-for-bit block comparison against a separately rendered SincBest oracle.
   The deletion/creation counters are atomic and reset immediately before the
   counted render.
3. **Cause and complete.** The only caller of
   `applyConverterModeToRetainedStates()` is `AudioEngine::setSrcQuality()`
   (`AudioEngine.cpp:409`); it drives the prepare/adopt/discard hooks at
   `AudioEngine.cpp:388-393`, and the `Instrument` defaults
   (`include/Instrument.h:143-150`) keep every other instrument consistent.
   Patman is the only override (`Patman.h:66-68`). The p1 table shows no
   render-path converter setter.
4. **No collateral damage.** No product file or persistence format changed. The
   deletion hook has no effect outside `LMMS_TESTING`: it calls plain
   `src_delete` and adds no allocation, free, lock or log. Saved projects load
   through the unchanged readers, and the tests use a temporary GF1 patch and
   the existing test seams only.
5. **ALREADY-FIXED evidence.** `src/core/AudioEngine.cpp:298-320` prepares every
   retained state, `plugins/Patman/Patman.cpp:247-267` forwards prepare/adopt/
   discard to held Patman notes, and `src/core/AudioResampler.cpp:219-264`
   caches the prepared converter before adoption. The reserved capacity at
   `src/core/AudioEngine.cpp:145-149` and the throw at
   `src/core/AudioResampler.cpp:392` keep allocation out of the render thread.

## Other findings

- `PatmanRealtimeTest` does not exist in this design: `ctest -N | rg
  PatmanRealtime` has no match (exit 1). The relevant suite is `PatmanLoadTest`.
- A four-slot SincBest capacity does not exist in this design. The realtime pool
  is the whole global capacity (thousands of slots); T2 bounds the audited case
  at 200 voices plus 32 beyond nominal polyphony because exceeding the global
  pool is not a polyphony scenario.
- The review's cited lines belong to the absent r8 lineage (see the top of this
  report). On this `HEAD` those lines are `PatmanView::openFile()`.
- No product defect was found outside the packet. The over-polyphony path
  intentionally throws when the reserved pool is exhausted rather than
  allocating on the audio thread.
