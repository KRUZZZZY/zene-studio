# Cluster new-p10-249: the mixer input accumulation locked on the render path

**Verdict: FIXED**

## The finding

`Mixer::mixToChannel()` is on the audio path: every `AudioBusHandle::audioCallback()`
(`src/core/AudioBusHandle.cpp:265`) ends in it, and `AudioEngine` runs those callbacks on the
audio thread and on its worker threads (`AudioEngine.cpp:315`, `QThread::idealThreadCount()-1`),
so several play handles can feed one channel in one period. The accumulation was shared through
a guard - first a `QMutex`, then a spin flag - which AGENTS.md rule 4 forbids on an audio-thread
path ("no allocation, no mutex, no logging"). The finding asks for a real-time-safe ownership or
preallocated per-worker reduction design; this fix implements the latter.

## The fix

Each render thread now accumulates into its OWN preallocated per-channel partial instead of the
shared channel bus. `AudioEngineWorkerThread` hands every render thread a stable slot in
`[0, AudioEngine::renderThreadCount())` (pool workers `0..n-1`, inline thread `n`), published
through `AudioEngineWorkerThread::currentRenderSlot()`. `MixerChannel::m_inputPartials` holds one
`InputPartial` per slot plus one for non-render callers, sized once in the `MixerChannel`
constructor (control thread, no audio-path allocation). `Mixer::mixToChannel()` adds a render
thread's block into that slot's partial (no lock, no wait, no spin); a caller that is not on a
render job (control side, tests) keeps the original immediate accumulation into `m_bus`, so its
data stays visible at once. `Mixer::reduceInputPartials()` is the single owner of the
worker-fed accumulation: it runs at the period barrier at the top of `Mixer::masterMix()` (after
STAGE 1 and STAGE 2 have joined) and folds the live partials into the channel buses. Partials
are cleared in `reduceInputPartials()` and in `Mixer::clearChannel()`.

## Evidence

- `include/AudioEngineWorkerThread.h`: `currentRenderSlot()`, `setRenderSlot()`, `setInlineRenderSlot()`, `s_renderSlot`, `s_inlineRenderSlot`, `m_renderSlot`.
- `src/core/AudioEngineWorkerThread.cpp`: slot definitions and set/restore in `run()` and `startAndWaitForJobs()`.
- `src/core/AudioEngine.cpp`: constructor assigns slots and publishes the inline slot.
- `include/Mixer.h:157` (before) / `include/Mixer.h:162` (after): `std::atomic_flag m_lock` replaced by `std::vector<InputPartial> m_inputPartials`; declares `reduceInputPartials()`.
- `src/core/Mixer.cpp`: `MixerChannel` constructor sizes the partials; `Mixer::mixToChannel()` indexes the slot; `Mixer::reduceInputPartials()` defined; called at the top of `Mixer::masterMix()`; partials reset in `clearChannel()`.
- `tests/src/core/MixerConcurrencyTest.cpp`: slots `aRenderThreadsBlockStaysOutOfTheSharedChannelBusUntilTheBarrier` and `everyRenderThreadsBlockLandsAtThePeriodBarrier`.

## Regression and verification

- RED build: `cmake --build build --target zene MixerConcurrencyTest > logs/build-revert.log 2>&1` EXIT 0
- RED test: `ctest -R '^MixerConcurrencyTest$' --output-on-failure > logs/test-revert.log 2>&1` EXIT 8
  - assertion: `MIXCONC_249 shared_bus_after_feed l=0.250000 r=-0.250000` and
    `channelBus[0][0] == 0.0f && channelBus[0][1] == 0.0f` returned FALSE.
- GREEN build: `cmake --build build --target zene MixerConcurrencyTest > logs/build-reapply.log 2>&1` EXIT 0
- GREEN test: `ctest -R '^MixerConcurrencyTest$' --output-on-failure > logs/test-reapply.log 2>&1` EXIT 0
- Mixer family (6 tests): `ctest -R '^(MixerConcurrencyTest|MixerListenTest|MixerAbRegressionTest|MixerChannelPanTest|VcaGroupTest|InstrumentRoutingTest)$'` EXIT 0
- Extra (4 tests): `ctest -R '^(MixingRoutingTest|HardwareIoTest|MixerRoutingBackwardCompatTest|PdcMixerTest)$'` EXIT 0
- rt-safety sweep: `python3 tests/rt-safety-sweep.py --check` EXIT 0
- feature_check: `python3 feature_check.py <worktree>` EXIT 0 (RESULT: PASS)

## Files committed

- `include/AudioEngineWorkerThread.h`
- `src/core/AudioEngineWorkerThread.cpp`
- `src/core/AudioEngine.cpp`
- `include/Mixer.h`
- `src/core/Mixer.cpp`
- `tests/src/core/MixerConcurrencyTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-249.md`

## Self-check

- The regression test FAILS with the product change reverted and PASSES with it: reverted run `ctest -R '^MixerConcurrencyTest$'` EXIT 8 (assertion `channelBus[0][0] == 0.0f && channelBus[0][1] == 0.0f` returned FALSE, `MIXCONC_249 shared_bus_after_feed l=0.250000 r=-0.250000`); reapplied run EXIT 0. Recorded in `logs/test-revert.log` and `logs/test-reapply.log`.
- The test is deterministic: no sleeps, wall-clock timing or races. It drives render threads through `AudioEngineWorkerThread::addJob()` + `startAndWaitForJobs()` (a synchronisation hook) and asserts the shared bus is untouched until `renderPeriod()` runs the barrier; injected value is the constant 0.25/-0.25 block and the 4x250 feeder count.
- The fix is at the cause and complete: `mixToChannel`'s only callers (`AudioBusHandle.cpp:265`, `InstrumentOutputBuses.cpp:264`, tests) were checked. Render-thread callers take the partial path; non-render callers (control side, tests such as `InstrumentRoutingTest`, `VcaGroupTest`, `MixingRoutingTest`) keep the original immediate accumulation into `m_bus`, so `m_hasInput` and `m_buffer` stay visible at once and those tests pass (6/6 + 4/4 family runs, EXIT 0). `reduceInputPartials()` is called once at the top of `masterMix()` after the workers join.
- No collateral damage: partials are preallocated in the `MixerChannel` constructor and cleared in `clearChannel()` and `reduceInputPartials()`; no channel-count limit is added (Mixer channels remain an unbounded vector); old projects load unchanged because `clearChannel`/`clear()` reset the new partials alongside the existing buffers. `feature_check.py` EXIT 0 and `rt-safety-sweep.py --check` EXIT 0.
