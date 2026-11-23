ALREADY-FIXED

# Cluster 5518 — Resampler quality changes allocate on the render path

## Decision

**ALREADY-FIXED on this branch.** The reported defect — `Sample::play` reaching
`AudioResampler::setMode()` (and therefore `src_new()`) on the audio thread when
the export SRC quality changes — no longer exists on `bugs/qwen-5518`. The fix
landed under cluster id `5284-port-port` in commits `5e13795472` ("keep SRC mode
changes off audio thread"), `b9948c0c92` ("prepare realtime SRC converters"),
`fba4e6ad12` (regression tests) and `b1ac6bb713` (pool-memory follow-up), with
its own report in `docs/qwen-fixes-2026-10/cluster-5284-port-port.md`. This
session verified the current code, ran the covering regression tests green, and
re-proved the red/green cycle live (below).

## Code evidence (read this session, current line numbers)

The render path cannot allocate a converter:

- `Sample::play()` (`src/core/Sample.cpp:100-208`) only calls `setRatio`
  (`src/core/Sample.cpp:109`) and `process()` (`src/core/Sample.cpp:157,194`).
  There is no `setMode()` call anywhere in the function.
- The only product caller of `AudioResampler::setMode()` is
  `Sample::PlaybackState::setConverterMode()` (`include/Sample.h:67`), and a
  tree-wide search found **no callers** of it; the header contract states
  `setMode()` "must not be called from the audio play path"
  (`include/AudioResampler.h:142-150`).
- Playback states created on the render path claim prebuilt pool slots instead
  of allocating: the clip `SamplePlayHandle` delegates with
  `AudioResampler::RealtimeVoiceTag{}` (`src/core/SamplePlayHandle.cpp:137-139`),
  comp-slip readers do `state.emplace(AudioResampler::RealtimeVoiceTag{})`
  (`src/core/CompSlip.cpp:84`), and Patman note states do
  `new Sample::PlaybackState(AudioResampler::RealtimeVoiceTag{})`
  (`plugins/Patman/Patman.cpp:528`). The tagged constructor acquires a pooled
  state with atomics only (`src/core/AudioResampler.cpp:384-394`).
- `AudioResampler::trySetMode()` refuses to allocate while a render period is
  active: `if (RealtimeScope::isActive()) { return false; }`
  (`src/core/AudioResampler.cpp:517`).

Quality changes allocate off the audio thread and switch by pointer swap:

- Both entry points route through `ExportRenderSettings::setSrcQuality()`
  (`src/core/ExportRenderSettings.cpp:88-107`): the export dialog via the
  `ProjectRenderer` constructor (`src/core/ProjectRenderer.cpp:161`, clean
  refusal at `162-165`), and the `export.set_src_quality` command
  (`src/core/ControlCommandsExport.cpp:243`, returns a `Busy` failure and keeps
  the previous selection when preparation fails).
- The listener registered at `src/core/AudioEngine.cpp:807`
  (`AudioEngine::onSrcQualityChanged`, `src/core/AudioEngine.cpp:401-410`) runs
  `applyConverterModeToRetainedStates()` (`src/core/AudioEngine.cpp:279-295`):
  it prepares every retained converter state under `requestChangesGuard()`
  (which excludes `renderNextPeriod`), publishes the new quality
  (`ExportRenderSettings::commitProposedSrcQuality`), then adopts the prepared
  states by pointer swap; on any failure it discards and returns false. This is
  exactly the fix the finding asks for: "create converter states off the audio
  thread and switch to a prepared state during rendering".
- The pool is initialized and grown only on the control thread:
  `AudioEngine::initDevices()` → `initializeRealtimeVoiceConverterPool()`
  (`src/core/AudioEngine.cpp:790`), and growth is guarded by
  `!RealtimeScope::isActive() && QThread::currentThread() == thread()`
  (`src/core/AudioEngine.cpp:1454`) and by
  `topUpPlayHandleCapacityOnControlThread()`'s early return on the audio thread
  (`src/core/AudioEngine.cpp:1546-1552`).

The covering regression tests (committed with the fix) assert exactly the
defect shape — zero converter allocations inside a `RealtimeScope` while
`SincBest` is selected:

- `AudioResamplerRatioTest::sampleTrackVoiceUsesPreparedConverterOnRenderPath`
  (`tests/src/core/AudioResamplerRatioTest.cpp:485-503`),
- `PatmanLoadTest::patmanVoiceUsesPreparedConverterOnRenderPath`
  (`tests/src/plugins/PatmanLoadTest.cpp:192-220`).

## Verification run this session (commands and exit codes)

Build and ctest commands ran from the worktree root and `build/tests`
respectively; every log is a new numbered file in
`/home/kruzzzzy/.config/kads/zene/state/runs/5518.r1/logs`.

| Stage | Exact command | Exit | Log |
|---|---|---:|---|
| Build covering tests | `cmake --build build --target AudioResamplerRatioTest PatmanLoadTest AudioResamplerPoolMemoryTest -j4` | 0 | `build-1.log` |
| Baseline green | `ctest -R '^(AudioResamplerRatioTest|PatmanLoadTest|AudioResamplerPoolMemoryTest)$' --output-on-failure` | 0 | `test-1.log` (3/3 passed) |
| Un-fix edit + save | edit `include/Sample.h`: tagged `PlaybackState` ctor builds the ordinary allocating `AudioResampler` (`m_resampler(tag)` → `m_resampler(AudioResampler::modeForCurrentSrcQuality())`), reproducing the historical defect; `git diff -- include/Sample.h > .../logs/unfix.patch` | 0 | `unfix.patch` |
| Rebuild with defect re-introduced | `cmake --build build --target AudioResamplerRatioTest PatmanLoadTest -j4` | 0 | `build-2.log` |
| **RED** | `ctest -R '^(AudioResamplerRatioTest|PatmanLoadTest)$' --output-on-failure` | 8 | `test-2-red.log`: `sampleTrackVoiceUsesPreparedConverterOnRenderPath` Actual `2` vs Expected `0` (`AudioResamplerRatioTest.cpp:502`); `patmanVoiceUsesPreparedConverterOnRenderPath` Actual `1` vs Expected `0` (`PatmanLoadTest.cpp:219`) |
| Restore fix | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/5518.r1/logs/unfix.patch` | 0 | `git status --short` empty afterwards |
| Rebuild app + tests | `cmake --build build --target zene AudioResamplerRatioTest PatmanLoadTest AudioResamplerPoolMemoryTest -j4` | 0 | `build-3.log` |
| **GREEN** | `ctest -R '^(AudioResamplerRatioTest|PatmanLoadTest|AudioResamplerPoolMemoryTest)$' --output-on-failure` | 0 | `test-3-green.log` (3/3 passed) |
| Wider family | build missing targets (`build-4.log` … `build-7.log`: the seven family test binaries, `slicert malletsstk patman audiofileprocessor taptempo`, `amp_1181 dj_eq_1901`, `decimator_1202 declip_1195`), then `ctest -R '^(AudioResamplerRatioTest|PluginPortsMigrationTest|AudioFileProcessorPlayheadTest|AudioFileProcessorWaveViewTest|PatmanLoadTest|SlicerTPlayheadTest|SlicerTWaveformTest|SlicerTZoomTest|TapTempoTest)$'` | 8 | `test-8-family-final.log`: 8/9 pass; the only failure is `PluginPortsMigrationTest`'s three known mallets functions (see Other findings) |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5518` | 0 | `gate-1.log`: all eight checks PASS |

The RED row proves the committed regression tests catch the historical defect,
and the GREEN row proves they pass with the branch's fix in place — both
witnessed live in this session, on this worktree.

## Self-check

1. **Regression test fails with the fix reverted and passes with it:** RED exit
   8 (`test-2-red.log`, allocation counts 2 and 1 vs expected 0) with the
   un-fix applied to `include/Sample.h`; GREEN exit 0 (`test-3-green.log`,
   3/3) after `git apply -R .../logs/unfix.patch` restored it. The working tree
   was clean before and after (`git status --short` empty).
2. **Deterministic:** the tests read the `LMMS_TESTING` counter
   `s_realtimeConverterAllocations`, incremented only inside
   `createConverterState()` while `RealtimeScope::isActive()`
   (`src/core/AudioResampler.cpp:84-93`), around the real clip-handle
   constructor and the real `instrument.playNote()`. No wall-clock timing, no
   sleeps, no races.
3. **Cause and completeness:** every render-path state family is tagged and
   pooled — clip handles (`src/core/SamplePlayHandle.cpp:137-139`), comp-slip
   readers (`src/core/CompSlip.cpp:84`), Patman note states
   (`plugins/Patman/Patman.cpp:528`); SlicerT and AudioFileProcessor retained
   pools are built off-thread and stay on the same prepare/adopt path
   (`docs/qwen-fixes-2026-10/cluster-5284-port-port.md` §Self-check 3). The
   only remaining `setMode()` caller (`include/Sample.h:67`) has no callers
   (verified with `rg` over `src`, `include`, `plugins`, `tests`), and the
   mid-render quality transaction covers pool, sample-voice pool, instruments
   and all live play handles (`src/core/AudioEngine.cpp:298-320`).
4. **No collateral damage:** this session's commit changes documentation only;
   no product or test code changed on the branch by this job, so old projects
   and saved data load exactly as before. The fix itself kept project
   serialization and saved SRC settings unchanged (its report,
   `cluster-5284-port-port.md:13`), and the resampler family passes green here.
5. **ALREADY-FIXED evidence:** `src/core/Sample.cpp:100-208` (play path has no
   mode switch), `src/core/AudioEngine.cpp:279-295` and `401-410` (guarded
   prepare/publish/adopt transaction), `src/core/AudioResampler.cpp:384-394`
   (pooled, allocation-free voice states), `src/core/AudioResampler.cpp:517`
   (no allocation inside a render period), `include/AudioResampler.h:142-150`
   (the header contract the finding quotes is the OLD one; the current header
   forbids the audio-path call).
6. **Sibling commands:** the checker's list (app.*, arrangement.*, …) comes
   from other clusters' commits already on the branch; this cluster's command
   family is `export.*`. `export.set_src_quality` is the only sibling that
   changes the converter, and it routes through the guarded prepare/adopt
   transaction (`src/core/ControlCommandsExport.cpp:243`,
   `src/core/AudioEngine.cpp:279-295`), recording one honest undo step only on
   success; `export.get_settings` is read-only; `export.set_dither`,
   `export.set_loudness_report` and the `export.preset_*` family do not touch
   the converter path.

## Other findings

- `PluginPortsMigrationTest` fails 3 functions at clean HEAD in this worktree:
  `malletsVoiceLimitIsEnforcedPerInstrument`,
  `malletsVoiceLimitSurvivesDeviceChange` and `slice8PluginsAreLiveAndExact`
  ("malletsstk rendered silence"). Branch reports already document this as the
  missing `thirdparty/stk-rawwaves/sinewave.raw` data issue
  (`docs/qwen-fixes-2026-10/cluster-new-p15-71.md:154-157`,
  `cluster-new-p17-91.md:27`). It is unrelated to the resampler path and was
  not touched here. Its fourth failure, `nonRealtimeLadspaEffectsAreInvalid`,
  clears once the `decimator_1202`/`declip_1195` fixture targets are built
  (`test-7-family-recheck3.log`: 93 passed, 3 failed).
- This worktree's build tree started with 7 of the 9 family test binaries and
  several plugin/fixture targets unbuilt (`ctest -N` reported "Could not find
  executable"); they were built during this session (`build-4.log` …
  `build-7.log`). Nothing in git changed while doing so.
- Observation for a future cluster, not this one: the tagged realtime-voice
  constructor throws `std::runtime_error{"No prepared realtime voice converter
  is available"}` if the pool is exhausted (`src/core/AudioResampler.cpp:392`).
  The pool is sized with comp-reader headroom off the audio thread
  (`src/core/AudioEngine.cpp:790,1463,1561`), so exhaustion would be an
  engine-capacity invariant break rather than the allocation defect reported
  here; noting it because the finding family may want it audited.
