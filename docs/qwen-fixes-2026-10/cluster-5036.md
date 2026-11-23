FIXED

# Cluster 5036 — Sample-rate changes reuse delay state measured at the old rate

## Verdict

REAL, and fixed at the cause, including the failed-rebuild path the first round missed.
The earlier patrol's "REAL-UNTESTED" note was correct about the defect but wrong about
testability: `AudioDevice::setSampleRateForTesting()` exists for exactly this under
`LMMS_TESTING` (`include/AudioDevice.h:87`), and the signal is a plain Qt signal a test
can emit (`Engine::audioEngine()->sampleRateChanged()`).

Measured in this worktree, with the reverb module rebuilt from source each time:

- 48 kHz → 96 kHz with a 100 ms pre-delay and the old effect buffers: **0.622523** of
  old-rate tone leaked during the first blocks of silence (round 1 RED).
- 48 kHz → 96 kHz with the kernel rebuilt successfully: 0 (round 1 fix).
- 96 kHz → 48 kHz when the impulse file has become unreadable, so the rebuild fails and
  the old kernel is retained: **0.620811** leaked (round 2 RED), from the retained
  kernel's overlap/frequency-domain history *and* the effect buffers.

## Evidence (current line numbers)

- Round 1: `ConvolutionReverb.cpp` (before the fix) connected `sampleRateChanged` to
  `loadImpulse(m_impulsePath)`, which reached only `install()`; the swap happened under
  `requestChangeInModel()`/`doneChangeInModel()` and nothing else was cleared.
- `plugins/ConvolutionReverb/ConvolutionReverb.cpp:248-251` — `predelayFrames(rate)`
  recomputes the read offset in frames of the *new* rate, while the ring still held
  frames written at the old rate; 100 ms at 48 kHz was read as 100 ms at 96 kHz, so the
  read position reached audio written 100–200 ms earlier in wall time.
- `plugins/ConvolutionReverb/ConvolutionReverb.cpp:259-269` — `designFilters()`
  re-designs coefficients at the new rate, but `BiquadSection::set()`
  (`include/BiquadDesign.h:102`) replaces coefficients only and keeps `z1`/`z2` from the
  old-rate history.
- `plugins/ConvolutionReverb/ConvolutionReverb.h:102-112` — `m_predelay`,
  `m_predelayWrite`, `m_in`, `m_out`, `m_dry`, `m_position`, `m_lowCut`, `m_highCut` are
  all sample-count state measured at the old rate.
- Failed rebuild (round 2): `ConvolutionReverb.cpp:222-246` (`buildKernel`) returns
  `false` at `:235` when `SampleBuffer::fromFile` cannot read the impulse, *before*
  `install()`; the old kernel therefore stays. `ConvolutionKernel.cpp:97-125`
  (`convolveChannel`) then keeps convolving the retained impulse with `m_history` and
  `m_previous`, both filled at the old rate.

## Fix

All changes are on the control thread; nothing was added to `processImpl` or any
per-period path.

- `ConvolutionReverb.cpp:177-186` — the `sampleRateChanged` handler calls
  `buildKernel(m_impulsePath, true)`. On success `install()` swaps the kernel and clears
  the effect state. On failure (unreadable impulse, `std::bad_alloc`, resampler failure)
  it takes the model-change guard itself, calls `m_kernel->resetHistory()` on the
  retained kernel, then `resetRateDependentState()`, then releases the guard: the audio
  thread never sees old-rate history through the old kernel, and the old-rate delay
  lines are gone.
- `ConvolutionReverb.cpp:191-201` — `install(kernel, rateChanged)` performs the kernel
  swap and, when `rateChanged`, `resetRateDependentState()` inside the same guard.
- `ConvolutionReverb.cpp:203-215` — `resetRateDependentState()` zeroes both pre-delay
  lines, resets their write cursor, zeroes the queued `m_in`/`m_out`/`m_dry` blocks,
  resets the block position, resets both biquad histories, and sets `m_rate = 0` so
  `designFilters()` re-designs for the new rate.
- `ConvolutionKernel.cpp:97-109` — `ConvolutionKernel::resetHistory()` zeroes both
  channels' frequency-domain delay line (`m_history`) and overlap (`m_previous`) and
  resets `m_head`; the impulse's own spectra (`m_spectra`) stay, because they describe
  the retained response. Declared at `ConvolutionKernel.h:72-75`.
- `ConvolutionReverb.cpp:217-220` — the public `loadImpulse()` still passes
  `rateChanged = false`: ordinary impulse loads, project loads
  (`ConvolutionReverbControls::loadSettings`) and `plugin.impulse_load` behave exactly
  as before, including an unreadable-file refusal.

Callers and siblings checked: `install()` has one caller (`buildKernel`); the other entry
to the same code (`loadImpulse`) keeps the old behaviour; `resetHistory()` has no audio
thread caller; the targets compiling these files are `convolutionreverb`,
`DeviceDspTest` (compiles `ConvolutionKernel.cpp` with interposed FFTW) and
`ConvolutionFftwPlanScopeTest` (compiles the reverb and the kernel), and all were
rebuilt and pass.

## Regression test

`DeviceDspTest::theConvolutionReverbDropsOldRateStateOnASampleRateChange`
(slot in `tests/src/core/DeviceDspTest.cpp`, body in
`tests/src/core/DeviceDspTestParts2.h`):

1. 48 kHz + emitted signal, a 48 kHz one-click impulse, pre-delay 100 ms, 60 blocks of a
   1 kHz tone: fills the pre-delay line. Asserts audible output.
2. 96 kHz + signal: 8 blocks of silence must be below 1e-6 (the successful-rebuild
   path; RED without the round-1 fix: 0.622523, exactly the value measured in round 1).
3. 60 blocks of the tone at 96 kHz: asserts audible output; this fills the *retained
   kernel's* history too and proves the reverb is alive after the change.
4. The impulse file is deleted, then 48 kHz + signal: `buildKernel` fails. 8 blocks of
   silence must be below 1e-6 (the failed-rebuild path; RED without this round's fix:
   0.620811).
5. A scoped restorer puts the device rate back and emits the signal on every exit path.

Deterministic: no sleeps, timers or wall-clock. The rate is injected through the
existing `LMMS_TESTING` seam (`setSampleRateForTesting`), the event is the engine's own
`sampleRateChanged` signal, the input is a pure function of the frame index, and the
assertions are fixed block counts and amplitudes.

Test matrix: normal path (before/after), reconfiguration (48→96→48), failure (unreadable
impulse during a rate change), audio-thread constraints (all new work is control-thread
`std::fill`/`.fill`; the allocation probe in
`theConvolutionReverbPlaysALoadedImpulse` still passes), module missing (QSKIP), and the
supported Linux toolchain.

## Commands and EXIT codes

Round 1 logs: `/home/kruzzzzy/.config/kads/zene/state/runs/5036.r1/logs` (unchanged
evidence for the successful-rebuild half).

RED (round 1, test first, product unchanged):

    cmake --build build --target convolutionreverb DeviceDspTest -j4
      > logs/build-2-red.log 2>&1                         EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-2-red.log 2>&1                          EXIT=1
      FAIL! ... (old-rate audio survived the rate change: 0.622523, 0.622523)

GREEN (round 1 fix):

    cmake --build build --target convolutionreverb DeviceDspTest -j4
      > logs/build-3-green.log 2>&1                       EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-3-green.log 2>&1                        EXIT=0
    cd build/tests && ctest -R '^DeviceDspTest$' --output-on-failure
      > logs/test-4-family.log 2>&1                       EXIT=0 (1/1 passed)
    4b reverse/rebuild/reapply/repass: logs/test-5-red-proof.log EXIT=1,
      logs/test-6-green-proof.log EXIT=0, logs/test-7-family-green.log EXIT=0.

Round 2 logs: `/home/kruzzzzy/.config/kads/zene/state/runs/5036.r2/logs`.

RED (failed-rebuild phase added; the awkward first pass is explained below):

    cmake --build build --target convolutionreverb DeviceDspTest -j4
      > logs/build-5-red.log 2>&1                         EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-5-red.log 2>&1                          EXIT=1
      FAIL! ... (old-rate audio survived a failed rebuild: 0.620811, 0.620811)

GREEN (kernel history reset on a failed rebuild):

    cmake --build build --target convolutionreverb DeviceDspTest \
      ConvolutionFftwPlanScopeTest -j4
      > logs/build-6-green.log 2>&1                       EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-6-green.log 2>&1                        EXIT=0
    cd build/tests && ctest -R '^(DeviceDspTest|ConvolutionFftwPlanScopeTest)$' \
      --output-on-failure > logs/test-7-family.log 2>&1   EXIT=0 (2/2 passed)

4b — this round's product files only, tests kept:

    git diff -- plugins/ConvolutionReverb/ConvolutionReverb.cpp \
      plugins/ConvolutionReverb/ConvolutionKernel.cpp \
      plugins/ConvolutionReverb/ConvolutionKernel.h > logs/fix.patch
                                                          EXIT=0
    git apply -R logs/fix.patch                           EXIT=0
    cmake --build build --target convolutionreverb DeviceDspTest \
      ConvolutionFftwPlanScopeTest -j4
      > logs/build-7-red-proof.log 2>&1                   EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-8-red-proof.log 2>&1                    EXIT=1
      FAIL! ... (old-rate audio survived a failed rebuild: 0.620811, 0.620811)
    git apply logs/fix.patch                              EXIT=0
    cmake --build build --target convolutionreverb DeviceDspTest \
      ConvolutionFftwPlanScopeTest -j4
      > logs/build-8-green-proof.log 2>&1                 EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-9-green-proof.log 2>&1                  EXIT=0
    cd build/tests && ctest -R '^(DeviceDspTest|ConvolutionFftwPlanScopeTest)$' \
      --output-on-failure > logs/test-10-family-final.log 2>&1
                                                          EXIT=0 (2/2 passed)

Final comment-only header touch, rebuilt and rerun:

    cmake --build build --target convolutionreverb DeviceDspTest \
      ConvolutionFftwPlanScopeTest -j4
      > logs/build-9-final.log 2>&1                       EXIT=0
    cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceDspTest \
      theConvolutionReverbDropsOldRateStateOnASampleRateChange \
      > logs/test-11-final.log 2>&1                       EXIT=0

Gates:

    python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
      /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5036 \
      > logs/gates-1.log 2>&1                             EXIT=0
      RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression,
      check-namespace, no-tautology, doc-figures, complexity, file-length)

Honest notes on discarded runs: `5036.r1/logs/test-1-red.log` was the first RED attempt
and failed for a test-authoring reason (it named the parameter `predelay` while
`plugin.param_set` resolves display names, and its temporary directory outlived the rate
restorer); it is not cited. In round 2, `logs/test-1-red.log` through `test-3-debug.log`
initially *passed* because the pre-existing `build/plugins/libconvolutionreverb.so` was a
stale QA build (built by the reviewer's dropped 4-file fix) rather than this worktree's
source. Rebuilding the module from source (`logs/build-5-red.log`) reproduced the leak
0.620811, which matches the reviewer's number; every result cited above is from a module
rebuilt in this session.

## Self-check

- **Regression test fails reverted, passes restored.** Round 1: `test-5-red-proof.log`
  EXIT=1 (0.622523), `test-6-green-proof.log` EXIT=0. Round 2: `test-8-red-proof.log`
  EXIT=1 (0.620811), `test-9-green-proof.log` EXIT=0, via the exact
  `git diff`/`git apply -R`/rebuild/reapply sequence above.
- **Deterministic.** No wall clock, sleeps or timers. Rate changes use
  `AudioDevice::setSampleRateForTesting()` and `emit AudioEngine::sampleRateChanged()`;
  inputs are pure functions of the frame index; assertions use fixed block counts.
- **Cause and completeness.** Both outcomes of a rate change are handled: a successful
  rebuild clears the effect state under the swap guard; a failed rebuild clears the
  retained kernel's history plus the effect state under its own guard. Ordinary
  `loadImpulse` (constructor, project load, `plugin.impulse_load`) keeps
  `rateChanged=false`, so impulse-load failure behaviour is unchanged. `resetHistory()`
  has no audio-thread caller, allocates nothing, and zeroes only vectors that already
  exist. All three targets compiling the changed files were rebuilt and pass.
- **No collateral damage.** Saved XML, the `impulse` attribute and the public
  `loadImpulse` contract are untouched; no limit or check was added. `DeviceDspTest`
  (including `theConvolutionReverbPlaysALoadedImpulse`,
  `anUnresamplableImpulseIsRefusedThroughTheCommand` and the allocation probe) and
  `ConvolutionFftwPlanScopeTest` pass.
- **ALREADY-FIXED / FALSE-POSITIVE item.** Not applicable: the verdict is FIXED, with
  the code evidence above.
- **Sibling commands in changed command files.** No command file is changed by this
  cluster: the product diff is `plugins/ConvolutionReverb/ConvolutionReverb.{h,cpp}` and
  `plugins/ConvolutionReverb/ConvolutionKernel.{h,cpp}`, plus the test and this report.
  No command was added and no canary moved. The `app.*` / `arrangement.*` ids the checker
  lists come from other commits already on the merge train between `FIX-BASE` and this
  branch tip, not from this work.

## Other findings

- None left from this defect. The previously reported unreadable-impulse-at-rate-change
  surface is now the failed-rebuild path this round fixes and tests.
- Environment note, not a product defect: a prebuilt module left in `build/plugins` by
  another session's dropped fix can make a reverted-source run falsely pass. Rebuilding
  the module before judging a result is required; the discarded runs above are recorded.
