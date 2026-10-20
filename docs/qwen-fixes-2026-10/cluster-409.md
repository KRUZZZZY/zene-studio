# Cluster 409 — FFTW planning and destruction can race audio processing

**Verdict: FIXED**

## The finding is real

`plugins/ConvolutionReverb/ConvolutionKernel.cpp` created and destroyed FFTW plans on the
control thread with no guard against the audio thread:

- `ConvolutionKernel::ConvolutionKernel` — `fftwf_malloc` x4 and
  `fftwf_plan_dft_r2c_1d` / `fftwf_plan_dft_c2r_1d` (the pre-fix
  `plugins/ConvolutionReverb/ConvolutionKernel.cpp:37-47`).
- `ConvolutionKernel::~ConvolutionKernel` — `= default`, so `FftwPlanFree::operator()`
  (`plugins/ConvolutionReverb/ConvolutionKernel.h:77`) ran `fftwf_destroy_plan` and
  `FftwFree::operator()` (`plugins/ConvolutionReverb/ConvolutionKernel.h:71`) ran
  `fftwf_free`.

The plans are executed on the audio thread: `ConvolutionKernel::process()` ->
`convolveChannel()` -> `fftwf_execute(m_forward.get())` / `fftwf_execute(m_inverse.get())`
(`plugins/ConvolutionReverb/ConvolutionKernel.cpp:87` and `:102` pre-fix), once per
`Partition` samples, from `ConvolutionReverbEffect::processImpl()`
(`plugins/ConvolutionReverb/ConvolutionReverb.cpp:269`).

FFTW's manual ("Thread safety") is explicit: only `fftw_execute` is thread-safe (and only
for *different* plans); "you cannot call any other routine in FFTW while any fftw_execute
calls are in progress". So a plan creation or destruction on the control thread while
another instance's plan is executing on the audio thread is a data race inside FFTW's
planner state.

The control thread reaches the kernel without any guard:

- `ConvolutionReverbEffect::loadImpulse()` built the kernel **outside** the engine's
  model-change guard (`plugins/ConvolutionReverb/ConvolutionReverb.cpp:198-207` pre-fix).
- `ConvolutionReverbEffect::install()` swapped the kernel under
  `requestChangeInModel()`/`doneChangeInModel()`, but the old kernel - and with it its
  plans - was destroyed *after* `doneChangeInModel()`, when `install()`'s by-value
  `std::unique_ptr` parameter went out of scope
  (`plugins/ConvolutionReverb/ConvolutionReverb.cpp:168-176` pre-fix). The earlier
  SKIPPED-SCOPE note claimed this part was already handled; the code shows it was not.
- `~ConvolutionReverbEffect()` (`= default`) destroyed `m_kernel` with no guard at all.

The engine's model-change guard *is* the quiesce mechanism the finding asks for: the
render thread holds `AudioEngine::m_changeMutex` for every period it renders
(`src/core/AudioEngine.cpp:928`, `std::lock_guard{m_changeMutex}` in
`AudioEngine::renderNextPeriod()`), and the effects - including every convolution
instance's `fftwf_execute` - run inside that period
(`src/core/AudioEngine.cpp:943`, `renderStageEffects()`). A thread that holds the guard
therefore has the whole engine to itself: no `fftwf_execute` can be in flight.

## The fix

New `plugins/ConvolutionReverb/ConvolutionFftw.{h,cpp}`: `ConvolutionFftwScope`, a
control-thread RAII scope that takes the engine's model-change guard
(`Engine::audioEngine()->requestChangeInModel()` / `doneChangeInModel()`) for its
lifetime, and counts the FFTW planner calls it covered (`guardedPlannerCalls()` /
`unguardedPlannerCalls()` - the second one must stay at zero).

`plugins/ConvolutionReverb/ConvolutionKernel.{h,cpp}`:

- the constructor creates the FFTW buffers and both plans **inside** the scope
  (`ConvolutionKernel.cpp:36-56`);
- the destructor takes the scope and moves the plans and buffers into locals declared
  after it, so they are destroyed inside it, in one guard rather than one per member
  (`ConvolutionKernel.cpp:66-80`);
- the two deleters (`FftwFree`, `FftwPlanFree` in `ConvolutionKernel.h:77-102`) take the
  scope themselves, so a plan or buffer is never freed outside one - including on the
  constructor's failure path, where the partially built kernel is unwound.

The impulse response's own spectra are still computed with `fftwf_execute`, outside the
scope: `fftwf_execute` is the one thread-safe FFTW routine (and it runs on the *new*
kernel's plan, which no audio thread touches yet), so the guard is not held for the
thousands of transforms a long impulse response needs. That keeps the audio stall of a
load at the plan creation (sub-millisecond) instead of the whole transform.

Everything else is unchanged: the effect still builds the kernel on the control thread,
still swaps it under the guard, still reports the same tail, and the convolution's
numbers are bit-identical (same plans, same `FFTW_ESTIMATE`, same transform order).
The plugin's CMakeLists gains the new source; no engine, command, save format or
project file changed.

## Regression

`tests/src/plugins/ConvolutionFftwPlanScopeTest.cpp` (new target
`ConvolutionFftwPlanScopeTest`, registered in `tests/CMakeLists.txt`; the plugin sources
are compiled into the test binary, the `TripleOscillatorUserWaveTest` pattern). It drives
the real kernel and the real effect and reads the scope's counters - the test seam. There
is no timing, no sleep and no race in it:

- `thePlansLiveInsideTheScope` - a kernel is built and destroyed; every planner call must
  have run inside a scope.
- `loadingAnImpulseKeepsThePlannerCallsInAScope` - the plugin's own load path
  (`ConvolutionReverbEffect::loadImpulse()`, the path behind `plugin.impulse_load`) and the
  effect's removal.
- `theKernelStillConvolves` - a unit impulse still leaves the block unchanged (no
  collateral damage in the numerics).

## Evidence

All `ctest` commands ran from `build/tests`; all builds ran from the worktree root. Every
run's output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/409.r1/logs/`.

| Stage | Exact command | Exit |
| --- | --- | --- |
| Configure the new test target (`cmake -S . -B build` is blocked by the lane's `cmake` shim, exit 97, `cmake-1.log`; the build tree re-generates itself on the next `cmake --build`, `build-2.log`) | `cmake --build build --target zene` | 0 |
| Build the fix and the test (`build-3.log` was an intermediate compile error of my own: a duplicated `operator=` declaration in the new header, fixed before the run below) | `cmake --build build --target ConvolutionFftwPlanScopeTest convolutionreverb` | 0 (`build-4.log`) |
| GREEN, first run | `ctest -R '^ConvolutionFftwPlanScopeTest$' --output-on-failure` | 0, 1/1 (`test-1.log`) |
| RED, full product patch reversed | `git diff -- plugins/ConvolutionReverb/ConvolutionFftw.h plugins/ConvolutionReverb/ConvolutionFftw.cpp plugins/ConvolutionReverb/ConvolutionKernel.h plugins/ConvolutionReverb/ConvolutionKernel.cpp plugins/ConvolutionReverb/ConvolutionReverb.cpp plugins/ConvolutionReverb/CMakeLists.txt > logs/fix.patch && git apply -R logs/fix.patch` (exit 0) | - |
| ... rebuild | `cmake --build build --target ConvolutionFftwPlanScopeTest` | 2 - the test cannot build: *"Cannot find source file: .../plugins/ConvolutionReverb/ConvolutionFftw.cpp"* (`build-5-red-full.log`) |
| ... run (the stale binary was deleted first, so the run is not a false green) | `ctest -R '^ConvolutionFftwPlanScopeTest$' --output-on-failure` | 8 - *"Not Run"* (`test-3-red-full.log`). Without the deletion, `test-2-red-full.log` ran the previous binary and passed - a stale binary, not a result. |
| Restore the fix | `git apply logs/fix.patch` | 0 |
| RED, guard only removed (the new class stays, only the four `ConvolutionFftwScope` uses go) | `git apply logs/noguard.patch` (exit 0) | - |
| ... rebuild | `cmake --build build --target ConvolutionFftwPlanScopeTest` | 0 (`build-6-red-noguard.log`) |
| ... run | `ctest -R '^ConvolutionFftwPlanScopeTest$' --output-on-failure` | 8 - *"the kernel's FFTW plans were created outside the scope that quiesces the audio thread (cluster 409)"* and *"loading an impulse response ran an FFTW planner call outside the scope that quiesces the audio thread (cluster 409)"* (`test-4-red-noguard.log`) |
| Restore the fix | `git apply -R logs/noguard.patch` (exit 0) | - |
| GREEN | `cmake --build build --target ConvolutionFftwPlanScopeTest convolutionreverb zene` -> `ctest -R '^ConvolutionFftwPlanScopeTest$' --output-on-failure` | 0 build (`build-7-green.log`), 0 test, 1/1 (`test-5-green.log`) |
| Affected family build | `cmake --build build --target DeviceDspTest EffectDeviceMenuTest` | 0 (`build-9-affected.log`; `build-8-affected.log` was the link error before the new source was added to the `DeviceDspTest` target, which compiles `ConvolutionKernel.cpp` into its binary) |
| Affected family run | `ctest -R '^(DeviceDspTest\|EffectDeviceMenuTest\|ConvolutionFftwPlanScopeTest)$' --output-on-failure` | 0, 3/3 (`test-8-affected.log`). The first run of this family (`test-7-affected.log`, exit 8) failed two `DeviceDspTest` cases with *"the frequency shifter module is available"* / *"'!console.isEmpty()' returned FALSE"* - the `console` and `frequencyshifter` plugin modules had never been built in this lane's build tree; after `cmake --build build --target console frequencyshifter` (exit 0, `build-10-plugins.log`) the same three tests pass. |
| Wider family | `ctest -R 'Plugin\|Dsp\|Realtime\|Device\|Effect' --output-on-failure` | 8 - every failure is *"Not Run"*: this lane's build tree contains only the test binaries that were built (`test-9-family.log`). `DeviceDspTest`, `EffectDeviceMenuTest` and `ConvolutionFftwPlanScopeTest` are the built ones and they pass. |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 - all eight rows PASS (`gate-2-feature-check.log`); `bash tests/fork-sources-gate.sh` also exit 0 (`gate-1-forksources.log`) |

The guard-only removal is the proof that the test catches the bug: the only difference
between that tree and the fixed tree is the four `ConvolutionFftwScope` uses, and the
assertions fail. The full `git apply -R fix.patch` RED is a build error for the same
reason (the new class does not exist without the patch).

## Files committed

- `plugins/ConvolutionReverb/ConvolutionFftw.h` (new)
- `plugins/ConvolutionReverb/ConvolutionFftw.cpp` (new)
- `plugins/ConvolutionReverb/ConvolutionKernel.h`
- `plugins/ConvolutionReverb/ConvolutionKernel.cpp`
- `plugins/ConvolutionReverb/ConvolutionReverb.cpp` (comment only)
- `plugins/ConvolutionReverb/CMakeLists.txt`
- `tests/src/plugins/ConvolutionFftwPlanScopeTest.cpp` (new)
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-409.md`

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.**
   With the whole product patch reversed (`git apply -R logs/fix.patch`, exit 0) the test
   cannot even build - *"Cannot find source file: .../ConvolutionFftw.cpp"*, build exit 2
   (`build-5-red-full.log`) - and ctest reports *"Not Run"*, exit 8
   (`test-3-red-full.log`). With only the guard removed (`logs/noguard.patch`) the test
   builds and fails on its assertions, exit 8 (`test-4-red-noguard.log`). With the fix
   restored it passes, exit 0 (`test-5-green.log`).
2. **The test is deterministic.** It uses no clock, no sleep and no thread: the seam is
   `ConvolutionFftwScope`'s two counters (`guardedPlannerCalls()` /
   `unguardedPlannerCalls()`), which say whether each FFTW planner call ran inside the
   scope that takes the engine's model-change guard. The kernel and the effect are driven
   synchronously on the test thread.
3. **The fix is at the cause and complete.** Every FFTW planner call in the plugin is now
   inside the scope: the constructor (`ConvolutionKernel.cpp:36-56`), the destructor
   (`ConvolutionKernel.cpp:66-80`), and both deleters (`ConvolutionKernel.h:77-102`), so
   the constructor's failure path (the partially built kernel is unwound, the plans and
   buffers freed) is covered too. The callers of the kernel - `loadImpulse()`
   (`ConvolutionReverb.cpp:179-210`), `install()` (`ConvolutionReverb.cpp:168-177`) and
   `~ConvolutionReverbEffect()` - need no change, because the scope is taken by the kernel
   itself; the effect's own guard around the swap is unchanged. The sibling path into the
   same code - `ConvolutionReverbControls::loadSettings()` -> `loadImpulse()` and the
   `plugin.impulse_load` command (`src/core/ControlCommandsDeviceTools.cpp:327-355`),
   which restores the effect state - goes through the same `loadImpulse()` and is covered
   by the same scope. The other FFTW users in the tree (`plugins/Eq/EqSpectrumView.cpp`,
   `plugins/SpectrumAnalyzer/SaProcessor.cpp`, `plugins/SlicerT/SlicerT.cpp`) are separate
   plugins with their own plan lifetimes; they are outside this finding (which names the
   convolution kernel) and are not touched here.
4. **No collateral damage.** The convolution's numbers are unchanged (the same plans with
   `FFTW_ESTIMATE`, the same transform order); `theKernelStillConvolves` checks the unit
   impulse's identity, and `DeviceDspTest::theConvolutionReverbPlaysALoadedImpulse` (a
   real impulse response through the real plugin module) passes. No project file, save
   format, command, parameter or default changed; old projects load exactly as before.
   The scope is a guard, not a limit: nothing refuses a legitimate caller.
5. **Realtime, undo and commands.** Nothing was added to the audio thread: `process()` and
   `convolveChannel()` are untouched - no allocation, no free, no lock, no wait, no
   logging, no signal. The scope is taken only on the control thread (kernel construction
   and destruction). Undo is untouched: no command was added or changed, and
   `plugin.impulse_load` keeps its one undo step (the state restore calls the same
   `loadImpulse()`).
6. **Old projects and saved data.** Unchanged - the fix touches no serializer, no
   parameter and no project structure.
7. **Portability.** The new code uses only C++20 standard library features
   (`std::atomic`, `thread_local`) and the engine's existing API; no Qt-6-only API, no
   `windows.h` macro names, no `std::filesystem`.
