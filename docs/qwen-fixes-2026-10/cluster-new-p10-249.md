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
