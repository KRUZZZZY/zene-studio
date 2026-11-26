FIXED

new-p9-133: FIXED

## Decision: REAL

`DualFilterControls::updateFilters()` deleted both filters and built new ones, from a Qt slot
that `AudioEngine::sampleRateChanged()` invokes on the control thread, while
`DualFilterEffect::processImpl()` dereferences the same two pointers on the render thread:

- before the fix, `plugins/DualFilter/DualFilterControls.cpp:111-114` (HEAD) was
  `delete m_effect->m_filter1; delete m_effect->m_filter2; m_effect->m_filter1 = new BasicFilters<2>( ... ); ...`,
  wired by `connect( Engine::audioEngine(), SIGNAL( sampleRateChanged() ), this, SLOT( updateFilters() ) )`
  at `plugins/DualFilter/DualFilterControls.cpp:102`.
- the render path reads those members every period: `m_filter1->setFilterType(...)` at
  `plugins/DualFilter/DualFilter.cpp:87`, `m_filter1->calcFilterCoeffs(...)` at
  `DualFilter.cpp:154`, and `m_filter1->update(...)` at `DualFilter.cpp:159-160`.
- the plugin had no synchronisation of its own: no atomic, no lock, no "audio stopped" handshake
  between the slot and the render path.

The engine's normal device-change path happens to quiesce the audio thread first
(`AudioEngine::setAudioDevice()` calls `stopProcessing()` before `emit sampleRateChanged()`,
`src/core/AudioEngine.cpp:1422,1427`; `restoreAudioDevice()` likewise at
`AudioEngine.cpp:1450,1454`; `AudioDevice::stopProcessing()` clears the run flag, stops the
backend and then waits for open callbacks, `src/core/audio/AudioDevice.cpp:137-156`), and
`initDevices()` emits without a stop but only at startup (`AudioEngine.cpp:812`, single caller
`src/core/Engine.cpp:86`). That ordering is an engine invariant the plugin neither states nor
enforces: at the plugin's own boundary the slot frees the objects the render path uses, which is
a use-after-free (and a torn-pointer data race on a 32-bit target) the moment the invariant does
not hold. This is the same shape the panel already accepted and fixed twice for the same signal:
`docs/qwen-fixes-2026-10/cluster-new-p18-34.md` (new-p15-70, FreeBoy, atomic publication) and
`docs/qwen-fixes-2026-10/cluster-new-p21-233.md` (Mallets, atomic publication), both of which
state the same engine-invariant argument. Local TSan does not run on this box, so this is a
design argument about object lifetime, not a detector report.

## Fix (at the cause)

`plugins/DualFilter/DualFilterControls.cpp:107-128` - the slot now retunes the two existing
filters instead of replacing them:

- `DualFilterControls.cpp:113` - one read of `Engine::audioEngine()->outputSampleRate()`.
- `DualFilterControls.cpp:116-117` - `m_filter1->setSampleRate( sampleRate )` and the same for
  filter 2. `BasicFilters::setSampleRate()` (`include/BasicFilters.h:365-376`) rejects a zero
  rate and propagates the new rate to a double filter's subfilter.
- `DualFilterControls.cpp:121-122` - `clearHistory()` on both, because the old code produced
  freshly constructed filters and a filter that starts from clean state is the behaviour this
  keeps (the same reset `SlewDistortion::changeSampleRate()` performs,
  `plugins/SlewDistortion/SlewDistortion.cpp:695,700`).
- `DualFilterControls.cpp:126-127` - `m_filter1changed`/`m_filter2changed` still set, so
  `processImpl()` recalculates the coefficients for the new rate exactly as before.

The object identity is unchanged across the signal, so the render path can no longer touch freed
memory, and no new work appears on the audio thread: the render code (`DualFilter.cpp`) is
untouched, and the two `new`/`delete` pairs that used to run on the control thread are gone.
A side effect in the right direction: a double filter's subfilter is now retuned in place instead
of being destroyed and re-`make_unique`d later inside `setFilterType()` on the audio thread
(`include/BasicFilters.h:286-291`).

Every reference to the two members was checked (`plugins/DualFilter/`): construction
(`DualFilter.cpp:60-61`), the destructor (`DualFilter.cpp:73-74`, unchanged - object destruction
is not a sample-rate path), the sample-rate slot (this fix), `onEnabledChanged()`
(`DualFilter.cpp:212-216`, still valid because the pointers are stable), and the render reads
above. `updateFilters()` has exactly one caller, the connection at `DualFilterControls.cpp:102`
(`multi_grep` over the worktree: no other call site). `tests/reference/DualFilter/` still holds
the verbatim base-commit copy used by the Part C comparison harness and is deliberately not
touched.

## Test matrix

| Path | Test | Result |
| --- | --- | --- |
| Reconfiguration (the cause) | `dualFilterSampleRateChangeKeepsFiltersInPlace` in `tests/src/plugins/PluginPortsMigrationTest.cpp:1755` - real `AudioEngine::sampleRateChanged()` emission, filter pointers must be the same objects afterwards | FAIL before (`09-test-red-proof.log`), PASS after (`12-test-green-proof.log`) |
| Retune correctness | same test, second half: the retuned filter's impulse response must equal a `BasicFilters<2>` built at the new rate (guards a no-op "fix") | PASS with the fix |
| Normal render after the change | same test: one render period through `processAudioBuffer(AudioBus&)` after the emission, all samples finite and not all silent | PASS with the fix |
| Failure path (zero/negative rate) | not reachable: `BasicFilters::setSampleRate(0)` keeps the current rate (`include/BasicFilters.h:365-376`), covered by the existing `BasicFiltersSampleRateTest`; the old code would have built a 1 Hz filter | unchanged |
| Exhaustion | not applicable - the effect owns exactly two fixed filters, no resource pool | - |
| Sibling path (filter type change, double filter) | `dualFilterCutoffCachesStartInitialized` (same file) plus the Part C exact-render slots, which compare the migrated plugin against the reference copy | PASS (`04-test-green-focused.log`, `13-test-family-green-proof.log`) |
| Audio-thread constraints | the render path is untouched; the slot is control-thread code and now allocates and frees nothing | PASS (build + family) |
| Supported platforms | only the `.cpp` changed (no header), rebuilt `dualfilter`, `zene` and the test host; the slot runs through Qt's meta-object system, so Qt 5.15/MSVC/macOS are unaffected by the change | PASS (`03-build-green.log`) |

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-133.r1/logs`. Worktree:
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-133`.

RED (test written first, product code untouched):

- `cmake --build build --target PluginPortsMigrationTest -j4` -> EXIT=0, `01-build-red.log`
- `cd build/tests && ./PluginPortsMigrationTest dualFilterSampleRateChangeKeepsFiltersInPlace` -> EXIT=1, `02-test-red-focused.log`
  - `FAIL!  : PluginPortsMigrationTest::dualFilterSampleRateChangeKeepsFiltersInPlace() 'effect->m_filter1 == filter1Before' returned FALSE. (the sample-rate change replaced filter 1 while the audio path uses it)` at `PluginPortsMigrationTest.cpp(1800)`

GREEN (fix in place):

- `cmake --build build --target zene dualfilter PluginPortsMigrationTest -j4` -> EXIT=0, `03-build-green.log`
  (`[ 94%] Building CXX object plugins/DualFilter/CMakeFiles/dualfilter.dir/DualFilterControls.cpp.o`)
- `cd build/tests && ./PluginPortsMigrationTest dualFilterSampleRateChangeKeepsFiltersInPlace dualFilterCutoffCachesStartInitialized` -> EXIT=0, `04-test-green-focused.log`
  (`Totals: 4 passed, 0 failed`)
- `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` -> EXIT=8, `05-test-family-green.log`
  (`96 passed, 4 failed`), then after building the missing LADSPA fixture
  (`cmake --build build --target decimator_1202 -j4` -> EXIT=0, `06-build-ladspa-fixture.log`)
  the same ctest command -> EXIT=8, `07-test-family-green2.log` (`97 passed, 3 failed`).

The three remaining family failures are the checkout's STK raw-waveform gap, not this change:
`PART_C_STK_DIR` in `build/CMakeCache.txt` points at
`.../zene-studio-program/thirdparty/stk-rawwaves`, which does not exist, so
`malletsVoiceLimitIsEnforcedPerInstrument`, `malletsVoiceLimitSurvivesDeviceChange` and
`slice8PluginsAreLiveAndExact` render silence ("malletsstk rendered silence"). The same three
fail with the product change reversed (`10-test-family-red-proof.log`), and the Mallets report
records the same gap (`docs/qwen-fixes-2026-10/cluster-new-p21-233.md`, "Commands and exit
codes").

## 4b. The test catches the bug

- `git diff -- plugins/DualFilter/DualFilterControls.cpp > .../logs/fix.patch` -> EXIT=0 (33 lines)
- `git apply -R .../logs/fix.patch` -> EXIT=0
- `cmake --build build --target zene dualfilter PluginPortsMigrationTest -j4` -> EXIT=0, `08-build-red-proof.log`
- `cd build/tests && ./PluginPortsMigrationTest dualFilterSampleRateChangeKeepsFiltersInPlace` -> EXIT=1, `09-test-red-proof.log`
  - the same assertion fails at `PluginPortsMigrationTest.cpp(1800)`
- `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` -> EXIT=8, `10-test-family-red-proof.log`
  (`96 passed, 4 failed`: my slot plus the three STK slots)
- `git apply .../logs/fix.patch` -> EXIT=0
- `cmake --build build --target zene dualfilter PluginPortsMigrationTest -j4` -> EXIT=0, `11-build-green-proof.log`
- `cd build/tests && ./PluginPortsMigrationTest dualFilterSampleRateChangeKeepsFiltersInPlace` -> EXIT=0, `12-test-green-proof.log`
  (`Totals: 3 passed, 0 failed`)
- `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` -> EXIT=8, `13-test-family-green-proof.log`
  (`97 passed, 3 failed`: only the STK environment slots, i.e. my slot is the single difference
  between the reversed and the restored run)

## Gates

- `FEATURE_CHECK_LOG_DIR=.../logs/gates python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> EXIT=0, `14-feature-check.log`:
  fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS,
  no-tautology PASS, doc-figures PASS, complexity PASS (no new item in my files), file-length PASS
  (no new item in my files), `RESULT: PASS`.
- Registration: `plugins/DualFilter/DualFilterControls.cpp` is an inherited product file and is
  declared in `tests/upstream-modifications.txt` with the `cluster new-p9-133` reason;
  `tests/src/plugins/PluginPortsMigrationTest.cpp` already had an entry there and is also in
  `tests/all-sources.txt`. No new product file, so `tests/fork-sources.txt` is unchanged.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Yes: reversed patch -> `./PluginPortsMigrationTest dualFilterSampleRateChangeKeepsFiltersInPlace`
  EXIT=1 (`09-test-red-proof.log`, failure at `PluginPortsMigrationTest.cpp(1800)`), reapplied ->
  the same command EXIT=0 (`12-test-green-proof.log`). Before any fix the same run was EXIT=1
  (`02-test-red-focused.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races.** Yes: it runs on the test
  thread with the audio device stopped in `initTestCase` (`PluginPortsMigrationTest.cpp`
  `initTestCase` calls `audioDev()->stopProcessing()`), injects the rate through the existing
  `AudioDevice::setSampleRateForTesting()` seam and emits `AudioEngine::sampleRateChanged()`
  directly - no thread, no sleep, no clock. The rate probe compares the retuned filter's impulse
  response with a `BasicFilters<2>` built at the same injected rate, bit for bit.
- **The fix is at the cause and complete: every caller and sibling checked.** The slot was the
  only place that replaced the filters (`multi_grep` for `updateFilters` / `m_effect->m_filter`
  over the worktree finds one call site, the connection at `DualFilterControls.cpp:102`); the
  other users of the members are construction, the destructor, `onEnabledChanged()` and the
  render reads, all listed above and all still correct because the pointers are now stable.
  The filter type path (`setFilterType`) and its subfilter are retuned rather than rebuilt.
- **No collateral damage: old projects and saved data load as before; no new limit rejects a
  legitimate caller.** No save/load field, no model, no control and no parameter changed, so
  `.mmp` files round-trip exactly as before; the effect's controls are untouched. The fix adds no
  check or limit at all - `setSampleRate()` keeps `BasicFilters`' own zero-rate guard, which is
  stricter than the old "build a filter at the rate the engine reports" path only for a
  nonpositive rate, which no audio device reports.
- **Verdict evidence (if ALREADY-FIXED or FALSE-POSITIVE).** Not applicable: the verdict is REAL,
  and the pre-fix code is cited by line above (`git show HEAD:plugins/DualFilter/DualFilterControls.cpp`,
  lines 111-114).
- **Realtime.** The render path (`DualFilterEffect::processImpl`, `DualFilter.cpp:80-209`) is not
  modified: no allocation, free, lock, wait or log is added there. The slot runs on the control
  thread and now allocates and frees nothing; it writes two plain floats through
  `BasicFilters::setSampleRate()` and clears the filter history, the same shape every other
  in-repo sample-rate slot uses (`plugins/Bitcrush/Bitcrush.cpp:87-93`,
  `plugins/CrossoverEQ/CrossoverEQ.cpp:82-87`, `plugins/SlewDistortion/SlewDistortion.cpp:679-706`),
  and it does so with the engine's device stopped, exactly as the engine emits the signal. The one
  object-lifetime hazard the bug was about - freeing an object the render path holds - is gone.
- **Undo, commands and canaries.** No model setter, undo command or `ControlCommands*` file is
  touched (`git status` lists three files: the plugin `.cpp`, the test and the registration list),
  so undo stays as it was and the `ControlCommandsSnapshot` canary does not move.
- **Old projects and sibling effects.** `tests/reference/DualFilter/` keeps the verbatim
  base-commit sources on purpose; the Part C slots that compare the migrated plugin against that
  reference still pass (`13-test-family-green-proof.log`).

## Other findings

- `DualFilterControls` connects to `sampleRateChanged()` in its own constructor
  (`DualFilterControls.cpp:102`), and `DualFilterEffect` allocates `m_filter1`/`m_filter2` only
  afterwards, in the effect's constructor body (`DualFilter.cpp:56-66`). An emission inside that
  window would reach the slot with uninitialised pointers - before this fix it would `delete`
  them, after it dereferences them. The window is a single-threaded constructor body in which no
  signal can be emitted, so it is not reachable today; fixing it would need the filters to be
  created before the controls are constructed. Not changed here (outside the finding).
- Three `PluginPortsMigrationTest` slots cannot pass in this checkout because the configured
  `PART_C_STK_DIR` does not exist (`malletsVoiceLimitIsEnforcedPerInstrument`,
  `malletsVoiceLimitSurvivesDeviceChange`, `slice8PluginsAreLiveAndExact`). Pre-existing
  environment gap, identical with and without this change; reconfiguring the lane build tree is
  the parent's job, not this lane's.
