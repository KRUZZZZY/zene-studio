FIXED

# cluster new-p14-197: Clearing voices can race with slot claims

## Verdict: REAL
The finding names `src/core/ModulationLayerPublisher.cpp:95`, now line 96:

```cpp
// src/core/ModulationLayerPublisher.cpp:95-97
m_anyPerVoice.store(anyPerVoice, std::memory_order_release);
if (!anyPerVoice) { m_voices.clear(); }
else { removeUnfollowedVoices(m_voices, m_triggerTrack, m_perVoice); }
```

`publishTriggers()` runs on the control thread (from `ModulationLayerPublisher::edit()`, `include/ModulationLayer.h:392`,
inside the reader-visible write window). Audio workers are in the same pool at the same time:
`InstrumentTrackNotes.cpp:416` → `ModulationLayerPublisher::voiceChanged()` → `m_voices.noteOn()`.
`clear()` wrote every slot's state unconditionally (`src/core/ModulationVoices.cpp:93-96` before this fix):

```cpp
void ModulationVoices::clear() noexcept
{
	for (Slot& slot : m_slots) { slot.state.store(Free, std::memory_order_release); }
}
```

`noteOn()` claimed a slot with `compare_exchange(Free → Writing)` and only wrote its fields afterwards, so a `clear()`
that landed in that window freed a slot another worker was filling. Two consequences, both reproduced by the tests
below (they fail on the pre-fix code, see RED):

1. a second worker claimed the same slot (`Free` again) and both wrote `track`/`channel`/`key`/`startedAt`, so one
   voice could carry a mixture of two notes' fields;
2. the first worker then stored `Live`, leaving a voice that was active in a pool that had been cleared, and nothing
   frees it afterwards: while no modulator is per-voice, `voiceChanged()` returns before `noteOff()` (line 102), and
   the note's own note-off is ignored for the same reason. When per-voice modulation came back, that stale voice drove
   `Instrument::modulateNote` for a note that had already ended.

## Fix
`ModulationVoices` now carries a pool **generation** (`include/ModulationVoices.h:143`) and every slot's state word is
`(generation << 2) | State` (`stateWord()`/`kindOf()`/`generationOf()`, lines 115-124, and `currentGeneration()`, lines 126-130):

- `clear()` (`src/core/ModulationVoices.cpp:115-121`) only bumps the generation. It writes no slot, so a slot another
  worker is filling stays `Writing` and no second claim can take it.
- `claim()` (lines 58-79) stamps the claim with the generation it read, and treats a slot as taken when it is `Writing`
  (any generation) or `Live` in the current generation. A voice of a retired generation is claimable again, which is
  what keeps a cleared pool as roomy as before.
- `publish()` (lines 81-96) stores `Live` **in the claim's own generation**. A note-on that finishes after a clear
  therefore publishes a voice of the pool that was retired: invisible to `find()`/`voice()`/`activeCount()`
  (lines 30-44, 123-147), and claimable by the next note-on. Neither side waits, and the audio path still only touches
  atomics.
- `noteOff()` (lines 98-113) frees only the voice `find()` returned, with a compare-exchange on the full state word, so
  a slot re-claimed in between keeps its new claim.

Realtime: `clear()` went from 64 atomic stores to one atomic add; `claim()`/`publish()`/`noteOff()`/`voice()` add one
32-bit atomic load (and one word compare) each. Nothing allocates, locks, waits or logs, and `std::atomic<std::uint32_t>`
is lock-free on every target (no 64-bit atomic was introduced). This is a `clear()` that is cheaper than before, on a
path that was already: `publishTriggers()` used to write 64 slots inside the reader-visible write window.

The seam commit before it (`6b04c5cbae`) splits `noteOn()` into `claim()` + `publish()` without changing any state,
ordering or atomic: same code, same order, only so a test can hold a claim open across a `clear()` deterministically.

## Regression test
`tests/src/core/InstrumentRoutingTestParts1.h` (declarations in `tests/src/core/InstrumentRoutingTest.cpp:259` and `:263`, bodies at `InstrumentRoutingTestParts1.h:431` and `:466`):

- `clearingThePoolDoesNotHandOutAHalfFilledSlot` — claims a slot, clears the pool, claims again: the second claim must
  not take the slot being filled, and after both notes publish, exactly one voice is live, with the second note's
  `track` and `key`.
- `aClaimThatSpansAClearStaysOutOfThePool` — a claim held across `clear()` must not become a voice when it publishes,
  the pool must be ordinary afterwards (`noteOn()` + `voice()`), and a cleared full pool must hand out all 64 slots.

Test matrix this covers: **normal path** (`voicePoolIsFixed`, plus the `noteOn()`/`voice()` leg in the new test),
**failure** (the clear during a claim, both symptoms), **exhaustion** (full pool, `dropped()`, and 64 slots claimable
again after a clear), **reconfiguration** (a note after the clear, and the publisher family below), **audio thread**
(atomics only, no allocation/lock/wait/log — `clear()` is one atomic add), **platform** (32-bit atomics only, no new
include beyond `<cstdint>`, no Qt API, no `std::filesystem`).

## RED / GREEN (all exits seen this session; logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-197.r1/logs`)
Step 4b, with `fix.patch` = the product part of the fix (`git diff 6b04c5cbae 0c03cbf6b8 -- include/ModulationVoices.h
src/core/ModulationVoices.cpp`, 218 lines):

| step | command | exit | log |
|---|---|---|---|
| patch written | `git diff 6b04c5cbae 0c03cbf6b8 -- include/ModulationVoices.h src/core/ModulationVoices.cpp > .../logs/fix.patch` | DIFF_EXIT=0 | `logs/fix.patch` |
| revert | `git apply -R .../logs/fix.patch` | REVERT_EXIT=0 | |
| build | `cmake --build build --target zene InstrumentRoutingTest -j4` | BUILD_EXIT=0 | `12-build-red2.log` |
| RED | `cd build/tests && ctest -R '^InstrumentRoutingTest$' --output-on-failure` | **TEST_EXIT=8** | `13-test-red2.log` |
| restore | `git apply .../logs/fix.patch` | APPLY_EXIT=0 | |
| build | `cmake --build build --target zene InstrumentRoutingTest -j4` | BUILD_EXIT=0 | `14-build-green2.log` |
| GREEN | `cd build/tests && ctest -R '^InstrumentRoutingTest$' --output-on-failure` | **TEST_EXIT=0** | `15-test-green2.log` |

RED output (`13-test-red2.log`):
```
FAIL!  : InstrumentRoutingTest::clearingThePoolDoesNotHandOutAHalfFilledSlot() 'next.slot != filling.slot' returned FALSE. (clear() freed a slot another worker was still filling: two note-ons may claim it at once)
   Loc: [../../tests/src/core/InstrumentRoutingTestParts1.h(441)]
FAIL!  : InstrumentRoutingTest::aClaimThatSpansAClearStaysOutOfThePool() '!voices.voice(interrupted.slot, &voice)' returned FALSE. (a note claimed before the clear became a voice after it)
   Loc: [../../tests/src/core/InstrumentRoutingTestParts1.h(475)]
0% tests passed, 1 tests failed out of 1
```
The same two tests also failed against the seam-only code before the fix was written
(`03-build-red.log` BUILD_EXIT=0, `04-test-red.log` TEST_EXIT=8, same two assertions), so the test is red with or
without the fix.patch machinery.

## GREEN family (`logs/11-test-family.log`, TEST_EXIT=0)
`cd build/tests && ctest -R 'Modulat|MidiInputTransform|InstrumentRouting' --output-on-failure` → 10/10 passed:
`InstrumentRoutingTest`, `ModulationLayerTest`, `ModulationLayerValueTest`, `ModulationLayerProjectRoundTripTest`,
`ModulationAudioViewTest`, `ModulationRateTest`, `ControlModulatorCommandsTest`, `ModulatorToolsTest`,
`MidiInputTransformTest`, `ModulatorPanelTest`. Targets built first with
`cmake --build build --target zene ModulationLayerValueTest ModulationLayerTest ModulationAudioViewTest ControlModulatorCommandsTest ModulationLayerProjectRoundTripTest ModulationRateTest MidiInputTransformTest ModulatorToolsTest -j4`
(BUILD_EXIT=0, `08-build-family.log`) and `... ModulatorPanelTest -j4` (BUILD_EXIT=0, `10-build-family2.log`).
`zene` itself rebuilt after the header change (`05-build-green.log`, `14-build-green2.log`, BUILD_EXIT=0); the header is
included by `include/ModulationLayer.h`, so the library and every test that includes it were rebuilt.

## Gates
`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (unpiped) → **GATE_EXIT=0**
(`logs/16-feature-check.log`):
```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```
Registration: no new files. `include/ModulationVoices.h` and `src/core/ModulationVoices.cpp` are already fork sources
(`tests/fork-sources.txt:1260`, `:2136`) and `tests/src/core/InstrumentRoutingTestParts1.h` is `tests/fork-sources.txt:787`,
so no list changed (and `fork-sources` passes). No command file, no `ControlCommandsSnapshot`, no undo machinery touched.

## Self-check
- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted: `git apply -R
  .../logs/fix.patch` (REVERT_EXIT=0), rebuild (`12-build-red2.log` BUILD_EXIT=0), `ctest -R '^InstrumentRoutingTest$'`
  → **TEST_EXIT=8** (`13-test-red2.log`, the two new assertions). Restored: `git apply .../logs/fix.patch`
  (APPLY_EXIT=0), rebuild (`14-build-green2.log` BUILD_EXIT=0), same ctest → **TEST_EXIT=0** (`15-test-green2.log`).
- **Deterministic, no timing, no races.** The test drives the two halves of a note-on directly and in order:
  `voices.claim()` (a worker that has claimed and not yet published), `voices.clear()` (the control thread's edit),
  `voices.claim()`, `voices.publish(...)`. No threads, no sleeps, no wall clock; the seam is the `claim()`/`publish()`
  split added in `6b04c5cbae`, which is a pure refactor of `noteOn()`.
- **Fix at the cause, every caller and sibling checked.** The only caller of `ModulationVoices::clear()` is
  `ModulationLayerPublisher::publishTriggers()` (`src/core/ModulationLayerPublisher.cpp:96`). The other path into the
  same pool, `removeUnfollowedVoices()` (`src/core/ModulationLayerPublisher.cpp:44-60`), uses `voice()` + `noteOff()`; both were made generation-aware and
  their CAS now checks the full state word. `voiceChanged()`'s early return and `ControlModulationSupport.cpp:262`
  (`voices().activeCount()`) keep their contracts: `activeCount()` counts only the current generation, which is what
  `clear()` used to achieve by writing `Free`. `find()` (retrigger and note-off matching) is generation-aware too, so a
  retired voice neither absorbs a retrigger nor is freed twice.
- **No collateral damage.** The save format is untouched (no attribute added or removed), so old projects load exactly
  as before; the pool has the same 64 slots and the same `dropped()` accounting (only clicks that find every slot
  `Writing` or `Live` in the current generation are dropped, i.e. exactly the pre-fix cases); no limit or check was
  added. `ModulationLayerProjectRoundTripTest` and `ModulationLayerValueTest` pass unchanged.
- **Realtime, undo, commands.** Per period nothing new allocates, frees, locks, waits or logs: `claim()`, `publish()`,
  `noteOff()`, `voice()` and `activeCount()` are atomic loads/stores/compare-exchanges on 32-bit words; `clear()` is
  one atomic add on the control thread and replaced 64 stores. Undo is untouched (no model, command or selection
  changed), and no control command was added, so the `ControlCommandsSnapshot` canary does not move.

## Notes
- The residual case, unchanged from before this fix: `voice()` reads a slot's fields and then re-checks its state word.
  If the same slot were freed, re-claimed and re-published *within the same generation* between those two loads, the
  reader would report the new note under the old read. The window is one short read of four atomics and no deallocation
  is involved (the fields are plain values); it is not this finding, and closing it would need a claim serial wide
  enough to rule out wrap, i.e. a 64-bit atomic on the audio path.
- The generation is 30 bits inside the state word. A claim would have to span 2^30 clears (2^30 layer edits) to be
  mistaken for a live voice.
- One intermediate build of the fix was red: my first version left `activeCount()` and `voice()`'s final re-check
  comparing the raw state word against the old bare `Live`. Both were completed before the fix was committed; the runs
  recorded above are from the committed code.

## Other findings
- `tests/fork-sources.txt:1-20` records that its own recipe does not parse at HEAD, so the list cannot currently be
  re-derived mechanically (`tests/all-sources.txt` does reproduce; the `all-sources-reproduce` gate passes). Not touched
  here.
