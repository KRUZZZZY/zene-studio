ALREADY-FIXED

# cluster new-p8-175 - Neural amp models run at the engine sample rate without checking their trained rate

Finding: BUG 6, `plugins/NeuralAmp/NeuralAmpEffect.cpp:125-140, 195-210` (historical): a model's
declared sample rate was only printed in the status text, and processing neither resampled nor
refused a mismatch with the engine rate.

Verdict: the second fix the finding allows ("refuse models whose rate the effect cannot support")
is already in this branch, landed by two earlier commits of this same cluster:

- `3d83d022da` fix(neuralamp): reject models trained at another sample rate (cluster new-p8-175)
- `71a36b4ff1` fix(neuralamp): keep rate policy with its effect (cluster new-p8-175)

Both are ancestors of HEAD (`21c2f3ee2e`), so the product code under test contains the fix. I
changed no product file in this session; the only change is a regression test at the level of the
effect (see below), because the test those commits added only exercised the pure helper.

## Where the behaviour lives now

- `plugins/NeuralAmp/NeuralAmpEffect.h:48` `nam::sampleRateMatchesEngine(modelRate, engineRate)`:
  a missing rate (`-1`) is accepted, a declared rate must be finite, positive and within 0.5 Hz of
  the engine rate.
- `plugins/NeuralAmp/NeuralAmpEffect.cpp:230-238` (`loadOne`): a model whose declared rate does not
  match the engine rate is **refused**. `setStatus("load refused: model trained at ... engine runs
  at ...; rate adaptation is not implemented")` and `return` before `publishModel()`, so the model
  is never published and never reaches the audio thread.
- `plugins/NeuralAmp/NeuralAmpEffect.cpp:335-340` (`processImpl`, audio thread): the effect returns
  dry when `!m_sampleRateCompatible`, so a model published earlier cannot process at a rate the
  engine has since moved to. The flag is an `std::atomic<bool>` read with acquire, no lock.
- `plugins/NeuralAmp/NeuralAmpEffect.cpp:75-76` and `261-289`
  (`refreshSampleRateCompatibility`): the effect is connected to
  `AudioEngine::sampleRateChanged` (emitted by `AudioEngine::doSetAudioDevice`/`setAudioDevice`/
  `restoreAudioDevice`, `src/core/AudioEngine.cpp:808,1423,1450`, on the main thread), so a rate
  change re-decides the flag and rewrites the status to `bypassed: model trained at ...`.

The finding's other option, resampling, is deliberately not implemented; the status text says so,
and the plugin's own offline harness keeps its warning-only behaviour
(`plugins/NeuralAmp/tests/nam_harness.cpp:597-602` warns, it does not refuse) because it is a
developer tool with an explicit `--sample-rate`, not the effect's audio path.

## Regression test added in this session

`tests/src/plugins/NeuralAmpModelLoadTest.cpp` (fork-new file, so no registration entry) gains
`aModelTrainedAtAnotherRateIsNeverProcessed`, driven through the real effect:

1. a fixture `.nam` declaring `sample_rate` = 2x (or 0.5x) the engine rate is refused: status starts
   with `load refused` and `rendersDry()` is true (nothing published, audio untouched);
2. the control: the same fixture declaring the engine's own rate loads (`loaded rate_match`) and
   `rendersDry()` is false;
3. reconfiguration: `AudioDevice::setSampleRateForTesting()` moves the device rate and the device's
   own `sampleRateChanged` signal is delivered, after which the status starts with `bypassed` and
   `rendersDry()` is true again;
4. back at the model's rate the same model runs again (`loaded`, not dry).

The fixture writer `writeModel()` gained an optional rate argument; the fixtures the older tests
load still write no `sample_rate` key, so their behaviour is unchanged.

## Commands and EXIT codes (all run in this session)

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-175`
Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-175.r1/logs`

Baseline (fix present, test not yet written):

    cmake --build build --target zene NeuralAmpModelLoadTest -j4      -> EXIT=0   (build-1.log)
    (cd build/tests && ctest -R NeuralAmp --output-on-failure)        -> EXIT=8   (test-1.log)
        NeuralAmpModelLoadTest Passed; the other 7 NeuralAmp* ctests were "Not Run" because their
        executables were not built yet in this tree (no failure in a test that ran).

New test, fix present (GREEN):

    cmake --build build --target NeuralAmpModelLoadTest -j4           -> EXIT=0   (build-2.log)
    (cd build/tests && ctest -R NeuralAmpModelLoadTest --output-on-failure)
                                                                      -> EXIT=0   (test-2.log)
    (cd build/tests && ./NeuralAmpModelLoadTest \
        aModelTrainedAtAnotherRateIsNeverProcessed -v1)               -> EXIT=0
        PASS   : NeuralAmpModelLoadTest::aModelTrainedAtAnotherRateIsNeverProcessed()

RED - the fix reverted (step 4b). The landed fix is the product change under test and I committed
no product file of my own, so instead of `git diff -- <my product files>` the patch is the removal
of that landed rate policy from the current `plugins/NeuralAmp/NeuralAmpEffect.cpp` (the refusal
block in `loadOne` and the `!m_sampleRateCompatible` gate in `processImpl`), saved as
`logs/defix.patch`:

    git diff -- plugins/NeuralAmp/ > .../logs/defix.patch             -> EXIT=0   (30 lines)
    cmake --build build --target NeuralAmpModelLoadTest -j4           -> EXIT=0   (build-3-red.log)
    (cd build/tests && ctest -R NeuralAmpModelLoadTest --output-on-failure)
                                                                      -> EXIT=8   (test-3-red.log)
        FAIL!  : NeuralAmpModelLoadTest::aModelTrainedAtAnotherRateIsNeverProcessed()
        'waitFor([&] { return effect.modelStatus().startsWith("load refused"); })' returned FALSE.
        (bypassed: model trained at 88200 Hz, engine runs at 44100 Hz; rate adaptation is not
        implemented)
        Loc: [../../tests/src/plugins/NeuralAmpModelLoadTest.cpp(394)]
        (with the refusal and the audio-thread gate gone the 88.2 kHz model was published and the
        effect would have processed it at 44.1 kHz - the defect the finding describes)

GREEN - the fix restored:

    git apply -R .../logs/defix.patch                                 -> EXIT=0
    cmake --build build --target NeuralAmpModelLoadTest -j4           -> EXIT=0   (build-4-green.log)
    (cd build/tests && ctest -R NeuralAmpModelLoadTest --output-on-failure)
                                                                      -> EXIT=0   (test-4-green.log)

Family and siblings (fix present):

    cmake --build build --target AudioPluginTest NamModelLoaderTest NeuralAmpWavInputTest \
        NeuralAmpCompareRateTest -j4                                  -> EXIT=2   (build-5-family.log)
        (NeuralAmpCompareRateTest is not a build target; it is a python ctest over nam_harness)
    cmake --build build --target AudioPluginTest NamModelLoaderTest nam_harness -j4
                                                                      -> EXIT=0   (build-6-family.log)
    (cd build/tests && ctest -R "NeuralAmpModelLoadTest|AudioPluginTest|NamModelLoaderTest|\
        NeuralAmpWavInputTest|NeuralAmpCompareRateTest|NeuralAmpHarnessRateValidation|\
        NeuralAmpWavRunSampleRateWarning" --output-on-failure)        -> EXIT=0   (test-5-family.log)
        7/7 passed
    cmake --build build --target NeuralAmpProfileArgumentsTest NeuralAmpAllocProbeArgumentsTest \
        NeuralAmpHarnessBlockCount nam_profile rt_alloc_probe -j4     -> EXIT=0   (build-7-family.log)
    (cd build/tests && ctest -R NeuralAmp --output-on-failure)        -> EXIT=0   (test-6-family-all.log)
        8/8 passed

Gates:

    python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
        /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-175
                                                                      -> EXIT=0   (gates-1.log)
        PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
        no-tautology / doc-figures / complexity / file-length ; RESULT: PASS

## Self-check

**ASK: the regression test FAILS with your product change reverted and PASSES with it (exit codes).**
RED: `git diff -- plugins/NeuralAmp/ > logs/defix.patch` (removal of the landed rate policy),
`cmake --build ... NeuralAmpModelLoadTest` EXIT=0, `ctest -R NeuralAmpModelLoadTest` **EXIT=8**,
`FAIL! : aModelTrainedAtAnotherRateIsNeverProcessed ... returned FALSE ... Loc:
NeuralAmpModelLoadTest.cpp(394)` (test-3-red.log). Restored with `git apply -R logs/defix.patch`
EXIT=0, rebuilt EXIT=0, `ctest -R NeuralAmpModelLoadTest` **EXIT=0** (test-4-green.log). The same
test also passes standalone: `./NeuralAmpModelLoadTest aModelTrainedAtAnotherRateIsNeverProcessed
-v1` EXIT=0, PASS.

**ASK: the test is deterministic - no wall-clock timing, sleeps or races.**
Nothing in the test asserts on elapsed time. The two inputs are injected rather than timed: the
engine rate is read from `AudioEngine::outputSampleRate()` and written into the fixture
`sample_rate` field, and the reconfiguration is driven by `AudioDevice::setSampleRateForTesting()`
plus the `sampleRateChanged` signal the device itself emits, invoked with
`QMetaObject::invokeMethod(..., Qt::DirectConnection)`. The effect parses on its worker thread, so
the test waits for the published state with the file's existing bounded `waitFor()` predicate poll
(10 s ceiling, asserts on state, not on duration), exactly as the pre-existing tests in this file
do; the audio assertions themselves are synchronous `processImpl()` calls on the calling thread.
No sleeps, no `qWait` used as a duration assertion, no shared mutable state between threads other
than the effect's own atomics.

**ASK: the fix is at the cause and complete - every caller and sibling checked.**
The audio path is unchanged by this session (no product edit). The policy is a single helper used
by all three decision points (`loadOne` refusal, publication flag, `refreshSampleRateCompatibility`);
the audio thread only reads an `std::atomic<bool>` (`NeuralAmpEffect.cpp:336`) so nothing allocates,
locks, waits or logs there. Siblings checked: `nam_harness` (offline developer tool with its own
`--sample-rate`; it warns on a mismatch at `plugins/NeuralAmp/tests/nam_harness.cpp:597-602` and in
`--wav` mode at `618-622` - a tool contract, not the effect's audio path) and the control dialog,
which only displays `modelStatus()`. `NamModel` itself does not resample by design; the loader
validates the declared field (`plugins/NeuralAmp/nam/NamModelLoader.cpp:158-163`).

**ASK: no collateral damage - old projects and saved data load as before; added checks do not hit
other callers.**
No check was added or moved in this session; the only change is a test file. The fixtures the older
tests use still write no `sample_rate` key and are accepted at any engine rate (that path is
`sampleRateMatchesEngine(-1.0, ...) == true`, covered by
`AudioPluginTest::neuralAmpRejectsModelsTrainedAtAnotherSampleRate` which still passes). Saved
projects store only the model path, so nothing about project load/save changed. The whole
`ctest -R NeuralAmp` family (8 tests) and `AudioPluginTest`/`NamModelLoaderTest` pass: EXIT=0.

**ASK: verdict ALREADY-FIXED - the code (file:line) that shows it.**
`plugins/NeuralAmp/NeuralAmpEffect.cpp:230-238` (load refused before publication),
`plugins/NeuralAmp/NeuralAmpEffect.cpp:335-340` (audio thread bypasses an incompatible model),
`plugins/NeuralAmp/NeuralAmpEffect.h:48-53` (the rate-match policy, missing rate accepted),
`plugins/NeuralAmp/NeuralAmpEffect.cpp:75-76,261-289` (re-decided on `AudioEngine::sampleRateChanged`,
emitted at `src/core/AudioEngine.cpp:808,1423,1450`). Landed in `3d83d022da` and `71a36b4ff1`, both
ancestors of HEAD `21c2f3ee2e`.

## Other findings

- The test those two commits added (`AudioPluginTest::neuralAmpRejectsModelsTrainedAtAnotherSampleRate`)
  only exercises the helper, not the effect, and would not have caught a regression in `loadOne` or
  `processImpl`. The effect-level test added here closes that gap; I left the existing test untouched.
- `NeuralAmpCompareRateTest` appears under a `-R NeuralAmp` filter but is a python ctest over
  `nam_harness`, not a build target; `cmake --build --target NeuralAmpCompareRateTest` fails with
  "No rule to make target" (build-5-family.log). Not a defect, just a target-name trap.
- `refreshSampleRateCompatibility` holds `m_rateMutex` while it formats a `std::ostringstream`
  status string and then takes `m_statusMutex` in `setStatus()`. Neither is the audio thread, so
  this is not a realtime defect, but the two-mutex split is worth a later look.
