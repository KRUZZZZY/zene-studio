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

## Mutation checks (r11 logs, final test file)

Build before each test:

    cmake --build build --target zene PatmanLoadTest

Test, from `build/tests`, `--output-on-failure` so a RED mutation names the
failing assertion:

    ctest -R 'PatmanLoadTest|AudioResamplerPoolMemoryTest|AudioPluginTest' --output-on-failure

Every build below ended `EXIT=0`; every RED test run is non-zero; the reverted
tree ends `EXIT=0`. Logs are under
`~/.config/kads/zene/state/runs/new-p9-348-port-port.r11/logs/`. The line
numbers are those of the final `tests/src/plugins/PatmanLoadTest.cpp`; M4's log
numbers are three lines higher because that mutation inserted lines above the
assertions.

| Mutation | Exact edit | Build log / EXIT | RED log / EXIT | Observed failure |
| --- | --- | --- | --- | --- |
| M1 | `RealtimeVoiceConverterPool::adoptSlot` returns early | `052-m1-build.log` / 0 | `053-m1-test.log` / 8 | T1 `reacquired.back().mode()` 1 != 4 (line 446); T4 `state->converterMode()` 1 != 4 (507); T2 `!blocksMatch(sincBestReference, linearReference)` false (591); T3 oracle indistinguishable from Linear (654) |
| M2 | `acquire()` returns nullptr after 4 SincBest acquisitions | `054-m2-build.log` / 0 | `055-m2-test.log` / 8 | T2 aborts with an unhandled `std::runtime_error` (`No prepared realtime voice converter is available`), not a QCOMPARE |
| M3 | `PatmanInstrument::prepareConverterModeForNote` returns true without preparing | `056-m3-build.log` / 0 | `057-m3-test.log` / 8 | T1 `reacquired.back().mode()` 1 != 4 (446); T4 `state->converterMode()` 1 != 4 (507) |
| M4 | `referenceBlock` oracle forced to Linear | `058-m4-build.log` / 0 | `059-m4-test.log` / 8 | T2 mismatched 232 != 0 (581; log 584); T3 oracle indistinguishable from Linear (654; log 657) |
| M5 | M3 plus a lazy conversion in `playNoteImpl` (no hooks) | `060-m5-build.log` / 0 | `061-m5-test.log` / 8 | T1 realtime allocations 3 != 0 (424); T4 realtime allocations 3 != 0 (503); T2 realtime allocations 3 != 0 (612) |
| M6 | `adoptPreparedConverterModeForNote` no-op plus `adoptPreparedConverterMode()` before `play` in `playNoteImpl` | `062-m6-build.log` / 0 | `063-m6-test.log` / 8 | T4 realtime deletions 1 != 0 (514, the pending-prepare guard) |
| M7 | M5 but the lazy conversion fires only on the first note that needs it | `064-m7-build.log` / 0 | `065-m7-test.log` / 1 (single test `lazyRenderPathConversionIsDetected`) | T4 realtime allocations 1 != 0 (503) |

M7 is the proof that all three held notes are counted: a conversion on the
first note of the three-note render is caught, so the earlier per-note reset
(which measured only the last note) cannot hide it. T4 now resets both counters
once, renders all three notes in one `RealtimeScope`, and compares creations and
deletions after the scope closes. T2's held render is likewise bounded: its
`RealtimeScope` closes before the QCOMPAREs, cleanup, `switchQuality` and the
`freeSlots` compare.

M6's history is recorded separately. The first M6 run against the
unstrengthened tests did **not** fire: `040-build-m6.log` `EXIT=0`,
`041-test-m6.log` `EXIT=0` (100% passed). T4's pending-prepare guard was added
afterwards; the same M6 edit then failed `042-build-m6-strengthened.log`
`EXIT=0` / `043-test-m6-strengthened.log` `EXIT=8`, and the restored tree passed
`044-build-restored.log` `EXIT=0` / `045-test-restored.log` `EXIT=0`.

The reversal attempt also fails, as it must: `git apply -R --check` of
`fix-hooks.patch` exits 1 (`error: patch failed: src/core/Song.cpp:1345` /
`error: src/core/Song.cpp: patch does not apply`, `068-revert-hooks.log`), and
the same for `fix-pool.patch` (`error: patch failed: src/core/Sample.cpp:103`,
`069-revert-pool.log`). The mutation proof therefore rests on M1-M7, not on
reverting the saved patches. After the last mutation the restored build is
`066-restored-build.log` `EXIT=0` and the restored test is
`067-restored-test.log` `EXIT=0` (100% tests passed out of 3).

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
    # 070-feature-check.log, EXIT=0

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
    # 071-submit-check.log, EXIT=0, rerun after the final commit

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
- [x] All seven mutations (M1-M7) are RED and the reverted tree is GREEN;
      M7 catches a conversion on the first of T4's three notes.
- [x] `feature_check.py` has no new FAIL (`RESULT: PASS`, EXIT=0).
- [x] No product behaviour changed; only the `LMMS_TESTING`-only counter and
      friend hooks were added.
- [x] New test functions stay within CCN <= 10 and the file is at or below 500
      code lines (both gates: no new item).
- [x] `submit_check.py` has no FAIL and every `?` item is answered.

### Answers to the pre-submit questions

1. **RED with the fix reverted, GREEN with it (exit codes).** M1-M7 each fail
   `PatmanLoadTest` or its T4 function (CTest EXIT=8; M7 EXIT=1; logs `053`,
   `055`, `057`, `059`, `061`, `063`, `065`); each mutation build is EXIT=0.
   The restored tree builds cleanly (`066`, EXIT=0) and the test passes (`067`,
   EXIT=0, 100% of 3). The saved-patch reversal is not authoritative: both
   `git apply -R --check` calls exit 1 (`068`, `069`).
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
