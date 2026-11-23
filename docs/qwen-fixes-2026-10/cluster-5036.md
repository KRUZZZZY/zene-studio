FIXED

# Cluster 5036 — Sample-rate changes reuse delay state measured at the old rate

## Verdict

REAL, and fixed at the cause. The earlier patrol's "REAL-UNTESTED" note was correct
about the defect but wrong about testability: `AudioDevice::setSampleRateForTesting()`
exists for exactly this under `LMMS_TESTING` (`include/AudioDevice.h:87`), and the
signal is a plain Qt signal that a test can emit
(`Engine::audioEngine()->sampleRateChanged()`), as PluginPortsMigrationTest already does.

Before the fix, a 48 kHz → 96 kHz change with a 100 ms pre-delay let **0.62** of the
old-rate tone (full-scale 0.5 input through the impulse) out during the first blocks of
silence after the change; the regression test measured that value in its RED run.

## Evidence (current line numbers)

- `plugins/ConvolutionReverb/ConvolutionReverb.cpp:177` — the `sampleRateChanged`
  handler called `loadImpulse(m_impulsePath)`, i.e. `install()` only.
- `plugins/ConvolutionReverb/ConvolutionReverb.cpp:182-192` — `install()` swapped the
  kernel under `requestChangeInModel()`/`doneChangeInModel()` and did nothing else.
- `plugins/ConvolutionReverb/ConvolutionReverb.cpp:248-251` — `predelayFrames(rate)`
  recomputes the read offset in frames of the *new* rate, while the ring still held
  frames written at the old rate; 100 ms at 48 kHz was read as 100 ms at 96 kHz, so the
  read position reached audio written 100–200 ms earlier in wall time.
- `plugins/ConvolutionReverb/ConvolutionReverb.cpp:259-269` — `designFilters()`
  re-designs the coefficients at the new rate, but `BiquadSection::set()`
  (`include/BiquadDesign.h:102`) replaces coefficients only and keeps `z1`/`z2` from the
  old-rate history.
- `include/ConvolutionReverb.h` members `m_predelay`, `m_predelayWrite`, `m_in`, `m_out`,
  `m_dry`, `m_position`, `m_lowCut`, `m_highCut` (now `ConvolutionReverb.h:102-112`) are
  all sample-count state that was measured at the old rate.

## Fix

- `ConvolutionReverb.cpp:177` — the rate handler now calls `buildKernel(m_impulsePath, true)`.
- `ConvolutionReverb.cpp:182-192` — `install(kernel, rateChanged)` performs the kernel
  swap **and**, when `rateChanged`, `resetRateDependentState()` inside the same
  `requestChangeInModel()`/`doneChangeInModel()` guard, so the audio thread never sees a
  new-rate kernel with old-rate state.
- `ConvolutionReverb.cpp:194-206` — `resetRateDependentState()` zeroes both pre-delay
  lines, resets their write cursor, zeroes the queued `m_in`/`m_out`/`m_dry` blocks,
  resets the block position, resets both biquad histories, and sets `m_rate = 0` so
  `designFilters()` re-designs for the new rate on the next block.
- `ConvolutionReverb.cpp:208-211` — the public `loadImpulse()` keeps the old behaviour
  (`rateChanged = false`): loading a different impulse at the same rate does not clear
  the running delay lines, so project loading and `plugin.impulse_load` are unchanged.
- `ConvolutionReverb.h:91-99` — private declarations updated with the .cpp definitions.

Every caller/sibling was checked: `install()` has one caller (`buildKernel`); the other
entry to the same function (`loadImpulse`, used by the constructor and by
`ConvolutionReverbControls::loadSettings`) passes `false`; the only other target that
includes the header, `ConvolutionFftwPlanScopeTest`, was rebuilt and passes (log
`build-6-header-includer.log` / `test-8-header-includer.log`).

## Regression test

`tests/src/core/DeviceDspTest.cpp` (slot declaration) and
`tests/src/core/DeviceDspTestParts2.h`
(`DeviceDspTest::theConvolutionReverbDropsOldRateStateOnASampleRateChange`):

- sets `AudioDevice::setSampleRateForTesting(48000)` and emits
  `AudioEngine::sampleRateChanged()`, loads a 48 kHz one-click impulse and a 100 ms
  pre-delay, feeds 60 blocks of a 1 kHz tone (fills the pre-delay line);
- sets 96000 and emits the signal again (the real handler path), then runs 8 blocks of
  silence and requires the peak to be below 1e-6 (RED before the fix: 0.62);
- then feeds 60 blocks of the same tone at the new rate and requires audible output
  above 0.1, so the test cannot pass by making the reverb silent; a scoped restorer
  puts the device rate back and emits the signal on every exit path.

Deterministic: no sleeps, timers or wall-clock; the rate is injected through the
existing `LMMS_TESTING` seam and the event is the engine's own signal. Input is a pure
function of the frame index. No real module is skipped: the convolution reverb module is
built and `DeviceDspTest` runs it (QSKIP only if this build lacks it).

Test matrix covered: normal path (tone before and after the change), reconfiguration
path (48→96 kHz plus restore), error path (missing module is QSKIP, rate restored),
audio-thread constraints (reset is control-thread only, `std::fill`/`.fill` and
`BiquadSection::reset()`; the existing allocation probe in
`theConvolutionReverbPlaysALoadedImpulse` still passes), build on the supported Linux
toolchain (this box; no platform-specific API added).

## Commands and EXIT codes (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/5036.r1/logs`)

RED (test first, product unchanged):

    cmake --build build --target convolutionreverb DeviceDspTest -j4
      > logs/build-2-red.log 2>&1                         EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-2-red.log 2>&1                          EXIT=1
      FAIL! ... 'stale[0] < 1.0e-6f && stale[1] < 1.0e-6f' returned FALSE.
      (old-rate audio survived the rate change: 0.622523, 0.622523)

GREEN (fix applied):

    cmake --build build --target convolutionreverb DeviceDspTest -j4
      > logs/build-3-green.log 2>&1                       EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-3-green.log 2>&1                        EXIT=0
      Totals: 3 passed, 0 failed
    cd build/tests && ctest -R '^DeviceDspTest$' --output-on-failure
      > logs/test-4-family.log 2>&1                       EXIT=0 (1/1 passed)

4b — proof the test catches the bug (product files only, tests kept):

    git diff -- plugins/ConvolutionReverb/ConvolutionReverb.cpp \
      plugins/ConvolutionReverb/ConvolutionReverb.h > logs/fix.patch
                                                          EXIT=0
    git apply -R logs/fix.patch                           EXIT=0
    cmake --build build --target convolutionreverb DeviceDspTest -j4
      > logs/build-4-red-proof.log 2>&1                   EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-5-red-proof.log 2>&1                    EXIT=1
      FAIL! ... (old-rate audio survived the rate change: 0.622523, 0.622523)
    git apply logs/fix.patch                              EXIT=0
    cmake --build build --target convolutionreverb DeviceDspTest -j4
      > logs/build-5-green-proof.log 2>&1                 EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-6-green-proof.log 2>&1                  EXIT=0
    cd build/tests && ctest -R '^DeviceDspTest$' --output-on-failure
      > logs/test-7-family-green.log 2>&1                 EXIT=0 (1/1 passed)

Header user rebuilt and rerun:

    cmake --build build --target ConvolutionFftwPlanScopeTest -j4
      > logs/build-6-header-includer.log 2>&1             EXIT=0
    cd build/tests && ctest -R '^ConvolutionFftwPlanScopeTest$' \
      --output-on-failure > logs/test-8-header-includer.log 2>&1
                                                          EXIT=0 (1/1 passed)

Gates:

    python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
      /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5036 \
      > logs/gates-1.log 2>&1                             EXIT=0
      RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression,
      check-namespace, no-tautology, doc-figures, complexity, file-length)

The test-log prefix `test-1-red.log` is the first RED attempt; it failed for a
test-authoring reason (it named the parameter `predelay` while
`plugin.param_set` resolves display names, and its temporary directory outlived the
rate restorer). Both were corrected before the RED recorded above; `test-1-red.log`
is kept for honesty, not cited as evidence.

## Self-check

- **Regression test fails reverted, passes restored.** `test-5-red-proof.log`
  EXIT=1 (0.622523 leaked), `test-6-green-proof.log` EXIT=0, via the exact
  `git diff`/`git apply -R`/rebuild/reapply sequence above.
- **Deterministic.** No wall clock, sleeps or timers. Rate changes use
  `AudioDevice::setSampleRateForTesting()` and `emit AudioEngine::sampleRateChanged()`
  (the engine's real notification); input is a pure function of the frame index; the
  assertions are fixed block counts and amplitudes.
- **Cause and completeness.** The reduced guard in `install()` clears every rate-counted
  field: pre-delay lines + cursor, `m_in`/`m_out`/`m_dry`, block position, both biquad
  histories, and `m_rate` (forcing coefficient redesign). The other caller
  (`loadImpulse`) deliberately keeps the state (`rateChanged=false`) so loading an
  impulse mid-playback behaves as before. The header change forced the one other
  including target to rebuild and pass.
- **No collateral damage.** Project load (`ConvolutionReverbControls::loadSettings`) and
  `plugin.impulse_load` call `loadImpulse`, i.e. the unchanged non-reset path; saved XML
  keys and the `impulse` attribute are untouched. No limit or check was added. The
  DeviceDspTest family (including the impulse-load and allocation-probe cases) passes.
- **ALREADY-FIXED / FALSE-POSITIVE item.** Not applicable: this report's verdict is
  FIXED, with the code evidence above.
- **Sibling commands in changed command files.** This cluster's commit changes no
  command file at all: `git show --stat --oneline HEAD` lists only
  `plugins/ConvolutionReverb/ConvolutionReverb.{h,cpp}`,
  `tests/src/core/DeviceDspTest{,Parts2}.{cpp,h}` and this report, and the fix adds no
  command and moves no canary. The `app.*` / `arrangement.*` ids the checker lists come
  from the other commits already on the merge train between `FIX-BASE` and this branch
  tip, not from this work; no sibling command is affected.

## Other findings

- If `sampleRateChanged` arrives while the impulse path can no longer be read (file
  deleted), `buildKernel` returns false, the old-rate kernel stays installed and the
  old-rate state is not cleared. That is a separate failure surface of the same handler
  (missing impulse file at a rate change), not the state-reuse bug fixed here; it was
  left unchanged to keep this patch to the reported defect.
