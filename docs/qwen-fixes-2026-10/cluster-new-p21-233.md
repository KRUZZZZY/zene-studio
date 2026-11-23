FIXED

new-p21-233: FIXED

## Decision: REAL

The finding is real at the plugin's own boundary. `MalletsInstrument::playNoteImpl()` is
render-thread code (`Instrument::playNote` -> `playNoteImpl`, the audio path) and read the
published pool through a **non-atomic** `std::unique_ptr`:

- before the fix, `plugins/Stk/Mallets/Mallets.h:239` declared
  `std::unique_ptr<MalletsVoicePool> m_voicePool;`
- before the fix, the `AudioEngine::sampleRateChanged` lambda on the control thread did
  `m_retiredPools.push_back( std::move( m_voicePool ) ); m_voicePool = std::move( pool );`
  (`plugins/Stk/Mallets/Mallets.cpp:277-281`), and `playNoteImpl()` dereferenced
  `m_voicePool->claim( family )` on the render thread (`Mallets.cpp:514`).

Two defects followed from that shape. First, the control thread writes the `unique_ptr`
while the render thread reads it: a data race, undefined behaviour, and on a 32-bit target a
torn pointer. Second, between `std::move(m_voicePool)` and the assignment the pointer is
null, exactly the window a note enters through `playNoteImpl`; the dereference would be a
null-pointer crash (the pool object itself is not freed there, so this is a null window, not
a use-after-free).

The engine's normal device-change path happens to quiesce the audio thread first
(`AudioEngine::setAudioDevice()` calls `stopProcessing()` before `emit sampleRateChanged()`,
`src/core/AudioEngine.cpp:1406-1415`; `AudioDevice::stopProcessing()` waits for open
callbacks, `src/core/AudioDevice.cpp:135-154`), and `initDevices()` emits without a stop
(`AudioEngine.cpp:808`) but only at startup. That ordering is an undocumented engine
invariant, not a contract the plugin states or can rely on, and the plugin's own code has no
synchronisation at all. The sibling finding for the same pattern in FreeBoy was accepted as
REAL and fixed with a published atomic pointer (docs/qwen-fixes-2026-10/cluster-new-p18-34.md,
new-p15-70), and this fix makes Mallets use the same publication mechanism. The local TSan
does not run on this box, so this is a design argument about the pointer publication, not a
detector report.

## Fix (at the cause)

- `plugins/Stk/Mallets/Mallets.h:243` - `std::atomic<MalletsVoicePool*> m_voicePool{nullptr};`
- `plugins/Stk/Mallets/Mallets.h:249` - `std::vector<std::unique_ptr<MalletsVoicePool>> m_pools;`
  owns the current pool **and** every retired pool, so the pointed-to object cannot be freed
  while a note holds one of its voices (replaces `m_retiredPools`; same lifetime as before,
  one list instead of two).
- `plugins/Stk/Mallets/Mallets.cpp:242-244` - the constructor builds the first pool off the
  audio thread and publishes it with one release store.
- `plugins/Stk/Mallets/Mallets.cpp:285-290` - the `sampleRateChanged` slot builds the new
  pool, takes ownership of it in `m_pools`, then publishes it with one release store. There
  is no longer any intermediate null state, and the old pool stays alive for its sustained
  notes.
- `plugins/Stk/Mallets/Mallets.cpp:517-521` - `playNoteImpl()` loads the pointer with acquire
  (lock-free, no allocation, no lock, no log) and claims from that one snapshot; a null
  pointer skips the note without touching the budget.

Every other reference to `m_voicePool` was checked: construction, the sample-rate slot and
the render read are the only three (`rg -n m_voicePool plugins/Stk/Mallets`); nothing else
(save/load, the view, `deleteNotePluginData`) touches it. `m_voicesInUse` and the pool's own
slot claim/release were not changed.

## Test matrix

| Path | Test | Result |
| --- | --- | --- |
| Publication mechanism (the cause) | `theVoicePoolIsPublishedThroughALockFreeAtomicPointer` in `tests/src/plugins/MalletsInstrumentViewTest.cpp` | FAIL before, PASS after |
| Normal render | `aFreshInstrumentsVoiceBudgetStartsEmpty` (existing) | PASS |
| Exhaustion | `aFreshInstrumentsVoiceBudgetStartsEmpty` phase 2/3, and `malletsVoiceLimitIsEnforcedPerInstrument` (sibling target) | PASS / blocked by missing rawwaves, see below |
| Reconfiguration | `aSampleRateChangeKeepsSustainedNotesAndVoicesNewOnes` (new): a sustained note keeps the same prepared-voice slot and stays audible across a rate change, a note started afterwards is voiced | PASS (also with the fix reversed, as expected: it guards the handoff, it is not the bug trigger) |
| Failure path (STK voice not built) | unchanged code; covered by the existing `ready()` guard | not touched |
| Audio-thread constraints | the render read is one lock-free acquire load; the new test file has a `static_assert(std::atomic<MalletsVoicePool*>::is_always_lock_free)` | PASS (builds and runs) |
| Supported platforms | header + definition changed together; `Mallets` test target rebuilt and `malletsstk` module rebuilt; `zene` rebuilt | PASS |

The regression trigger is the publication pin: it reads `Mallets.h` from
`MALLETS_TEST_PLUGIN_DIR` and requires the initialised `std::atomic<MalletsVoicePool*>`
declaration (the same declaration-pin style as the existing
`theVoiceBudgetIsInitialisedWhereItIsDeclared` in this file). A compiled type probe of a
private member is not possible without a product test seam, and a seam would be removed by
the reverse-patch proof, so this pins the declaration instead; the behavioural test proves
the handoff still works with it.

## Commands and exit codes (all logs in the run log folder)

Product fix made; the test pin fails first (RED):

- `cmake --build build --target MalletsInstrumentViewTest -j4` -> EXIT=0, `01-build-red.log`
- `cd build/tests && ctest -R '^MalletsInstrumentViewTest$' --output-on-failure` -> EXIT=8, `02-test-red.log`
  - `FAIL!  : ...::theVoicePoolIsPublishedThroughALockFreeAtomicPointer() 'atomicDeclaration.match(text).hasMatch()' returned FALSE`
  - `PASS   : ...::aSampleRateChangeKeepsSustainedNotesAndVoicesNewOnes()`

With the fix (GREEN):

- `cmake --build build --target zene malletsstk MalletsInstrumentViewTest -j4` -> EXIT=0, `03-build-green.log`
- `cd build/tests && ctest -R '^MalletsInstrumentViewTest$' --output-on-failure` -> EXIT=0, `04-test-green.log`
- `./MalletsInstrumentViewTest` (slot-level) -> EXIT=0, `14-test-green-slots.log`:
  `Totals: 9 passed, 0 failed, 0 skipped, 0 blacklisted`

Reconfiguration sibling target:

- `cmake --build build --target PluginPortsMigrationTest -j4` -> EXIT=0, `06-build-plugin-migration.log`
- `./PluginPortsMigrationTest malletsVoiceLimitIsEnforcedPerInstrument malletsVoiceLimitSurvivesDeviceChange`
  -> EXIT=2, `07-test-plugin-mallets-slots.log`
  - fails with `Mallets note 0 ... was silenced` because the build's `PART_C_STK_DIR`
    (`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/thirdparty/stk-rawwaves`,
    `build/CMakeCache.txt:557`) does not exist in this checkout; the log shows STK
    `FileRead::open: could not open or find file .../marmstk1.raw` and `.../sinewave.raw`.
    This is an environment prerequisite gap, not this change: the same production sources
    render audible notes through `/usr/share/stk/rawwaves` in `MalletsInstrumentViewTest`
    (green above). Reconfiguring the lane build tree to fix the path is refused by the
    environment: `cmake -S . -B build -UPART_C_STK_DIR` -> EXIT=97,
    `08-cmake-stk-refresh.log` ("bunny-guard: configuring is the parent's job; the lane build
    tree is already configured").

## 4b. The test catches the bug

- `git diff -- plugins/Stk/Mallets/Mallets.cpp plugins/Stk/Mallets/Mallets.h > .../fix.patch` -> EXIT=0
- `git apply -R .../fix.patch` -> EXIT=0
- `cmake --build build --target zene malletsstk MalletsInstrumentViewTest -j4` -> EXIT=0, `09-build-red-proof.log`
- `cd build/tests && ctest -R '^MalletsInstrumentViewTest$' --output-on-failure` -> EXIT=8, `10-test-red-proof.log`
  - `FAIL!  : ...::theVoicePoolIsPublishedThroughALockFreeAtomicPointer() ... returned FALSE` (line 74)
- `git apply .../fix.patch` -> EXIT=0
- `cmake --build build --target zene malletsstk MalletsInstrumentViewTest -j4` -> EXIT=0, `11-build-green-proof.log`
- `cd build/tests && ctest -R '^MalletsInstrumentViewTest$' --output-on-failure` -> EXIT=0, `12-test-green-proof.log`

## Gates

- `FEATURE_CHECK_LOG_DIR=.../logs/gates python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> EXIT=0, `13-feature-check.log`, table: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item), file-length PASS (no new item), RESULT: PASS.
- Registration: `plugins/Stk/Mallets/Mallets.cpp` and `Mallets.h` are inherited files and are declared in `tests/upstream-modifications.txt` with a `cluster new-p21-233` reason; no new product file, so `tests/fork-sources.txt` is unchanged. The changed test file is already in `tests/all-sources.txt`.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Yes: reversed patch rebuilt, `ctest -R '^MalletsInstrumentViewTest$'` EXIT=8 (`10-test-red-proof.log`, `theVoicePoolIsPublishedThroughALockFreeAtomicPointer` FAIL); reapplied and rebuilt, same command EXIT=0 (`12-test-green-proof.log`). Before the fix at all, the same run was EXIT=8 (`02-test-red.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races.** Yes: the trigger reads the declaration from `Mallets.h` (no threads); the reconfiguration test drives the audio path synchronously on the test thread with the device stopped in `initTestCase`, injects the rate through `setSampleRateForTesting` and emits `sampleRateChanged` directly, then renders one period and asserts non-silence and slot identity. No sleeps, no threads, no timing.
- **The fix is at the cause and complete: every caller and sibling checked.** The pool publication is now a lock-free atomic pointer loaded once per note on the render path and stored once on the control path; ownership moved to `m_pools` so the pointed-to object outlives any note (the retired-pool behaviour is unchanged). All three `m_voicePool` references were updated (constructor, lambda, `playNoteImpl`); no other file references it (`rg -n m_voicePool`), and the sibling FreeBoy publication was used as the reference pattern.
- **No collateral damage.** No save/load format or parameter changed; `m_pools` replaces `m_retiredPools` with the same lifetime (freed in the instrument destructor). No limit changed: `MALLETS_VOICE_LIMIT` and `m_voicesInUse` are untouched, so the 128-voice budget and the 129th-note-waits behaviour are exactly as before (`aFreshInstrumentsVoiceBudgetStartsEmpty` green).
- **If ALREADY-FIXED or FALSE-POSITIVE: code or contract.** Not applicable - the verdict is REAL.
- **Realtime.** The only new render-thread operation is `m_voicePool.load(std::memory_order_acquire)` (lock-free, no allocation, no lock, no wait, no log); the atomic store and the pool construction happen on the control thread. No new command, so the ControlCommandsSnapshot canary is untouched. No undo interaction (the bug is in audio-device reconfiguration, not an editable model).

## Other findings

- `PluginPortsMigrationTest`'s Mallets slots cannot run in this checkout: the configured
  `PART_C_STK_DIR` (`.../zene-studio-program/thirdparty/stk-rawwaves`, `build/CMakeCache.txt:557`)
  is absent, so the instrument renders silence and the slots fail with "note 0 was silenced".
  Reconfiguring the lane build tree is refused (EXIT=97, `08-cmake-stk-refresh.log`). The same
  render and reconfiguration behaviour is proven in `MalletsInstrumentViewTest` instead.
- `m_pools` (and `m_retiredPools` before it) is never pruned while the instrument lives; every
  sample-rate change keeps one more pool until the instrument is destroyed. Pre-existing
  lifetime design, unchanged here.
