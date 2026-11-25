FIXED

# cluster new-p9-348-port-port: a quality change while converter slots are held

## Decision

The defect is **REAL** and fixed at the pool, in the form it takes on this HEAD.
Two of the review's three blocking items quote the r8 lineage, which is not in
this history; the mechanism they name does not exist here. The verdict per item:

| Blocking item (review r8) | Verdict on this HEAD | Evidence |
| --- | --- | --- |
| `[realtime]` a quality change skips occupied slots; active notes then reach `Sample::play()` with the new quality, which calls `AudioResampler::setMode()`; released slots keep stale mode metadata and acquisition rejects them | **REAL, FIXED at the pool** — but not through the quoted mechanism. Nothing on the render path switches a converter: `Sample::play()` only sets the ratio and processes (`src/core/Sample.cpp:109,157,194`), the only `setMode()` caller is `SamplePlayHandle`-unrelated dead SlicerT code (`include/Sample.h:67` <- `plugins/SlicerT/SlicerT.cpp:126`), and `AudioResampler::trySetMode()` refuses while a `RealtimeScope` is active (`src/core/AudioResampler.cpp:535`). The real half: `RealtimeVoiceConverterPool::prepareAll()` skipped every slot a voice was holding, and `adoptAll()` then read the pool's mode back from slot 0. A holder that no per-handle hook can walk stayed on the old converter, the pool recorded the **old** quality, and `ensureCapacity()` built every chunk appended after the change in that old quality — so a voice that needed capacity after the change (the "overflow" voice of item 2) got a Linear converter while SincBest was selected. | before: skip at `src/core/AudioResampler.cpp:172`, mode read back from slot 0 at `:188-191`; acquisition hands a slot out with its stored mode and never rejects it (`:203-219`); release only clears `inUse` (`:220-223`); after: `:160-181` prepares and records the target, `:183-193` adopts it and sets the pool mode |
| `[regression]` overflow voices use manual linear interpolation and lose the selected SincBest quality (`Patman.cpp:301-322,665-677`) | **FALSE-POSITIVE on this HEAD.** There is no manual/hand interpolation in the play path at all: `Sample::play()` always converts through its state's `AudioResampler` (`src/core/Sample.cpp:157,194`), and every tagged voice claims one converter prepared in the selected mode (`src/core/AudioResampler.cpp:404-417`, `plugins/Patman/Patman.cpp:528`). The quoted lines are the patch-file publication in `setFile()` (`plugins/Patman/Patman.cpp:299-324`) and the filename-ellipsis loop in `PatmanView::updateFilename()` (`:663-679`); the r8 slot pool and its four-slot SincBest cap do not exist here. | capacity is derived, not four slots: `src/core/AudioEngine.cpp:145-149` -> `5 * (playHandleCapacity + 2*PlayHandle::MaxNumber + 1)`; pool control-thread growth at `src/core/AudioEngine.cpp:1545-1565` |
| `[not-fixed]` the required SincBest over-capacity regression is missing | **FIXED by two regressions that fit this design.** The new `AudioResamplerPoolMemoryTest::heldSlotsFollowTheQualityChange` holds pool slots across a quality change (held converter mode, held converter samples, mode of capacity grown afterwards, released slot's mode); `PatmanLoadTest::overPolyphonyKeepsEverySincBestBlockAudible` (T2) already renders 232 overlapping SincBest voices bit-identical to a SincBest oracle with zero render-path converter creations. Pool exhaustion is not a polyphony case here: it needs ~10k states no product path can create (below). | new test `tests/src/core/AudioResamplerPoolMemoryTest.cpp:113-164`; T2 `tests/src/plugins/PatmanLoadTest.cpp:530-629` |

## The defect and the fix

`RealtimeVoiceConverterPool` is the engine's supply of prepared libsamplerate
states. `AudioEngine::applyConverterModeToRetainedStates()` (`src/core/AudioEngine.cpp:279-295`)
runs a prepare/adopt transaction under the engine's change guard, which excludes
the render thread, so a converter can be replaced between periods but never
during one.

`prepareAll()` skipped every slot with `inUse == true`
(`src/core/AudioResampler.cpp:172` before the fix). The per-handle hook
(`src/core/AudioEngine.cpp:306-320` -> `plugins/Patman/Patman.cpp:247-267`)
updates the slots of states the engine can walk, but a plain pooled voice is not
walkable, and the pool itself never revisited a skipped slot: `release()` only
cleared `inUse` (`:220-223`) and `acquire()` handed that slot to the next voice
with its old converter (`:203-219`). `adoptAll()` then set the pool's own mode
from `head->items[0].mode` (`:188-191`); with slot 0 held and skipped, the pool
recorded the old mode, and `ensureCapacity()` -> `appendChunks(capacity, m_mode)`
built every chunk appended afterwards in that old quality.

The fix is at that cause, in `src/core/AudioResampler.cpp`:

- `prepareAll()` prepares **every** slot, held or free (`:160-181`), and records
  the requested mode (`:179`). Preparing a held slot only allocates its
  replacement; the swap happens in the adopt phase, under the same guard the
  held-note hook already uses.
- `adoptAll()` adopts every slot and sets `m_mode` to the mode it adopted
  (`:183-193`) instead of inferring it from one slot.

No allocation, free, lock or log was added to a render path: `prepareSlot()`
allocates and `adoptSlot()` frees on the control thread inside the guarded
transaction (`:237-283`), exactly as before; `acquire()`/`release()` stay atomic
flag operations. No header changed, no persistence changed, no command changed,
and the pool's capacity formula is untouched. Callers and siblings checked:
`initialize()` re-init (`:137-151`), the engine's quality transaction
(`src/core/AudioEngine.cpp:279-341`), the engine's control-thread growth
(`:1545-1565`), a single voice's `trySetMode()` (`:536-550`), the sample-voice
pool and `AudioFileProcessor`'s voice pool (their own prepare/adopt paths,
unchanged), and the discard/rollback path (`src/core/AudioEngine.cpp:344-369`),
which still leaves held states on their old converter when a prepare fails.

## Regression test

`tests/src/core/AudioResamplerPoolMemoryTest.cpp:113-164`,
`heldSlotsFollowTheQualityChange()`:

1. holds the first three slots the pool hands out (slot 0 included) with plain
   `AudioResampler(RealtimeVoiceTag)` voices — no hook can reach them;
2. changes the quality to SincBest through `ExportRenderSettings::setSrcQuality`,
   i.e. the engine transaction, and asserts every held voice reports SincBest;
3. converts the same input on the held voice and on a `SincBest` reference and
   compares the samples bit for bit, so the switch is real output, not metadata;
4. grows the pool by one chunk, as the engine does after a change
   (`ensureRealtimeVoiceConverterPoolCapacity`), and asserts the acquired
   "overflow" slot is SincBest;
5. releases the holders and asserts the released slot comes back in the mode the
   pool now holds.

Without the fix, step 2 fails with `voice.mode()` 1 (Linear) != 4 (SincBest).
The test uses no sleep, deadline or thread; the pool seams are the
`LMMS_TESTING` accessors at `include/AudioResampler.h:174-177`.

The first version of the test was written in `PatmanLoadTest.cpp` (RED at
`905-red-test.log`, line 690). It was moved to the pool's own test file because
the file-length gate flags that file: `916-feature-check.log` `FAIL
file-length NEW: new file over 500 lines: tests/src/plugins/PatmanLoadTest.cpp
(565)`, `GATE_EXIT=1`. `PatmanLoadTest.cpp` is now byte-identical to the base.

## Commands and EXIT codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-348-port-port.r11/logs/`.
Builds ran from the worktree root, `ctest` from `build/tests`.

| What | Command | Exit | Log |
| --- | --- | ---: | --- |
| Baseline build of the untouched tree | `cmake --build build --target PatmanLoadTest patman -j4` | 0 | `900-baseline-build.log` |
| Baseline test | `ctest -R '^PatmanLoadTest$' --output-on-failure` | 0 | `901-baseline-test.log` |
| RED, test first written in `PatmanLoadTest.cpp` | `cmake --build build --target PatmanLoadTest -j4` / `ctest -R '^PatmanLoadTest$' --output-on-failure` | 0 / 8 | `904-red-build.log`, `905-red-test.log`: `voice.mode()` 1 != 4, line 690 |
| GREEN with the fix, before the move | `cmake --build build --target zene patman PatmanLoadTest AudioResamplerPoolMemoryTest AudioResamplerRatioTest -j4` / `ctest -R '^(PatmanLoadTest\|AudioResamplerPoolMemoryTest\|AudioResamplerRatioTest)$'` | 0 / 0 | `906-green-build.log`, `907-green-test.log`, `908-green-verbose.log` (10/10, new test listed) |
| Test moved to the pool test; build and run | `cmake --build build --target AudioResamplerPoolMemoryTest PatmanLoadTest -j4` / `ctest -R '^(AudioResamplerPoolMemoryTest\|PatmanLoadTest)$'` | 0 / 0 | `917-moved-build.log`, `918-moved-test.log` |
| Save the product patch | `git diff -- src/core/AudioResampler.cpp > .../logs/fix.patch` | 0 | `fix.patch` (48 lines) |
| 4b, reversed | `git apply -R .../logs/fix.patch` | 0 | — (`4b-proof.sh`) |
| 4b, reversed build | `cmake --build build --target AudioResamplerPoolMemoryTest -j4` | 0 | `919-reversed-final-build.log` |
| 4b, reversed test: **must fail** | `ctest -R '^AudioResamplerPoolMemoryTest$' --output-on-failure` | 8 | `920-reversed-final-test.log`: `FAIL! heldSlotsFollowTheQualityChange()` `voice.mode()` 1 != 4, `AudioResamplerPoolMemoryTest.cpp(133)`, 20 passed 1 failed |
| 4b, restore | `git apply .../logs/fix.patch` | 0 | — |
| 4b, restored build | `cmake --build build --target AudioResamplerPoolMemoryTest -j4` | 0 | `921-restored-final-build.log` |
| 4b, restored test: **must pass** | `ctest -R '^AudioResamplerPoolMemoryTest$' --output-on-failure` | 0 | `922-restored-final-test.log`: 1/1 passed |
| Final rebuild (product + focused targets) | `cmake --build build --target zene patman PatmanLoadTest AudioResamplerPoolMemoryTest AudioResamplerRatioTest AudioPluginTest ControlDeviceCatalogueTest -j4` | 0 | `923-final-build.log` |
| Focused CTests | `ctest -R '^(PatmanLoadTest\|AudioPluginTest\|ControlDeviceCatalogueTest\|RtSafetySweep\|RtSafetySelfTest\|AudioResamplerPoolMemoryTest\|AudioResamplerRatioTest)$' --output-on-failure` | 0 | `924-final-focused-tests.log`: 7/7 passed |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `925-feature-check.log`: `RESULT: PASS`, all eight checks PASS |
| Feature gate before the move (the file-length finding) | same | 1 | `916-feature-check.log`: `FAIL file-length NEW: new file over 500 lines: tests/src/plugins/PatmanLoadTest.cpp (565)` |

`PatmanRealtimeTest` from the review's focused list does not exist in this tree
(`ctest -N -R PatmanRealtimeTest` has no match); the Patman coverage lives in
`PatmanLoadTest`, which is in the run above.

## Self-check

1. **Fails without the fix, passes with it.** `920-reversed-final-test.log`
   (EXIT=8) after `git apply -R` of the product-only patch, and
   `922-restored-final-test.log` (EXIT=0) after `git apply` restored it; the
   product patch is only `src/core/AudioResampler.cpp`.
2. **Deterministic.** No sleep, deadline, timer or thread. The test uses the
   `LMMS_TESTING` pool seams (`AudioResampler.h:174-177`), one real quality
   transaction through the engine, and a fixed input buffer compared bit for bit.
3. **Cause and complete.** The fix is in the pool's prepare/adopt pass; callers
   and siblings are listed above. The held-note hook, the sample-voice pool and
   `AudioFileProcessor`'s own voice pool already used prepare/adopt and are
   unchanged; the discard path still rolls back cleanly.
4. **No collateral damage.** No header, persistence, command or capacity change.
   The pool's slot count and the mode mapping (`AudioResampler.cpp:664-676`) are
   untouched; a held voice now follows the setting instead of silently keeping
   the old converter. `AudioResamplerRatioTest` and
   `AudioResamplerPoolMemoryTest` (including `everySlotStartsWithSelectedQuality`
   and `failedSwitchPreservesOldQuality`) still pass.
5. **Realtime, undo and commands.** Preparing or adopting a converter still
   allocates/frees only on the control thread, under the engine change guard;
   `acquire()`/`release()` remain single atomic flag operations and the render
   path gained nothing. No command and no undo step changed.
6. **Registration and gates.** `tests/upstream-modifications.txt` has an r12
   entry for each changed file; `925-feature-check.log` is `RESULT: PASS`,
   `GATE_EXIT=0`.

## Other findings

- Pool exhaustion throws out of the render path. When every slot is held,
  `AudioResampler(RealtimeVoiceTag)` throws
  `std::runtime_error{"No prepared realtime voice converter is available"}`
  (`src/core/AudioResampler.cpp:416`) and nothing between
  `PatmanInstrument::playNoteImpl()` and the device callback catches it. No
  product path can reach it: the pool has
  `5 * (playHandles + 2*1024 + 1)` slots while the engine can create at most
  `playHandles * (1 + CompSlipReaders::MaxReaders)` note/comp states plus the
  fixed sample-voice pool. It is a design question for the pool's owner, not
  this cluster, and is recorded rather than changed here.
- `AudioResampler::trySetMode()` returns false (and `setMode()` throws) for a
  pooled state while a `RealtimeScope` is active
  (`src/core/AudioResampler.cpp:535,522`). That is the guard that keeps the r8
  render-path switch from ever happening here; it is unchanged.
