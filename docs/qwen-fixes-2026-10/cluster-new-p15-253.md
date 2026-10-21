FIXED

new-p15-253: FIXED
new-p12-313: FIXED

# Cluster new-p15-253 / new-p12-313: an audio bus registered on the audio path

Both reports describe ONE defect: the `AudioBusHandle` constructor registers the bus with the
engine and with the audio device inline, on whatever thread the bus was created - and the tree
explicitly allows a bus to be created on the audio path. The registration takes the engine's
model lock (a render worker would block on a mutex the render owner holds for the whole period,
while the render owner waits for that worker - a deadlock) and grows the engine's bus list (an
allocation on the audio path). The constructor also enabled the device port, which is a device
call the audio path may not make.

- BUG 1 (new-p15-253, `src/core/AudioBusHandle.cpp:101-102`, `src/core/AudioEngine.cpp:1166`):
  constructing an audio bus on a realtime thread can deadlock or allocate.
- BUG 2 (new-p12-313, `src/core/AudioEngine.cpp:1166` `addAudioBusHandle`): the audio bus
  registration can allocate or lock on the realtime path.

The fix is at the shared cause: the registration is no longer performed inline on the audio
path. A bus that is created on the audio path is published to a fixed, lock-free queue and
registered by the render owner at the start of the next period - under the model lock it holds
there, and without growing the bus list. The device port update is applied by the control
thread. The sibling of the registration - the removal, and the destruction of a bus whose owner
is a play handle - was fixed the same way, with the lifetime handling the realtime path needs.

## Evidence (file:line, current tree)

### The defect

- `src/core/AudioBusHandle.cpp:101-102` (before the fix): the constructor ended with
  `Engine::audioEngine()->addAudioBusHandle(this); setExtOutputEnabled(true);` - the engine
  registration and the device port setup, inline, on the constructing thread.
- `src/core/AudioBusHandle.cpp:93-95` (unchanged): the comment that states the contract this
  fix has to keep - "Standalone sample buses created on the audio path use the inline handle
  slot below until the main thread tops up their list."
- `src/core/AudioEngine.cpp:1166` `addAudioBusHandle` (after the fix): the audio path publishes
  the bus (`m_pendingAudioBusHandles.push(...)`) and returns; no lock, no container growth.
- `src/core/AudioEngine.cpp:1611-1614` `requestChangeInModel`: `if (s_renderingThread) { return; }
  m_changeMutex.lock();` - the lock is taken on every thread that is not the render owner.
- `src/core/AudioEngine.cpp:934-936` `renderNextPeriod`: `const auto lock =
  std::lock_guard{m_changeMutex};` - the render owner holds the model lock for the WHOLE period.
- `src/core/AudioEngineWorkerThread.cpp` `startAndWaitForJobs`: the render owner waits for the
  pool workers inside that period. So a worker that took the model lock in `addAudioBusHandle`
  blocked on a mutex the render owner held, while the render owner waited for that worker:
  the deadlock the finding describes.
- `src/core/AudioEngine.cpp:1156` (before the fix): `m_audioBusHandles.push_back(busHandle);` -
  the growth that allocates on the audio path when the vector has no spare capacity.
- `src/core/AudioBusHandle.cpp:130-144` (before the fix): `setExtOutputEnabled` called
  `Engine::audioEngine()->audioDev()->registerPort(this)` directly - the device port setup on
  the audio path.

### The fix

- `src/core/AudioEngine.cpp:1166-1203` `addAudioBusHandle`: on the audio path
  (`RealtimeScope::isActive()`) the bus is published to `m_pendingAudioBusHandles` (a
  `MpscQueue<AudioBusHandle*, 64>`, fixed storage, lock-free, no allocation) and the function
  returns. The control thread keeps the path it always had (the model lock, the list growth,
  the play-handle capacity top-up).
- `src/core/AudioEngine.cpp:1260-1283` `promotePendingAudioBusHandles`: the render owner
  registers the published buses at the start of the period (`src/core/AudioEngine.cpp:952`,
  inside `renderNextPeriod`, under the model lock the render owner holds). The bus list is only
  appended to, never grown: if it has no spare capacity the bus waits and the control thread
  is asked to make room (`m_audioBusHandleCapacityRefreshRequested`).
- `src/core/AudioEngine.cpp:1285-1309` `ensureAudioBusHandleCapacityOnControlThread` /
  `reserveAudioBusHandleCapacity`: the bus list grows only on the control thread, under the
  model lock (`requestChangesGuard()`), so the render owner never reallocates it.
- `src/core/AudioEngine.cpp:1205-1248` `removeAudioBusHandle` / `destroyAudioBusHandle`: the
  removal is realtime-safe too. The render owner erases under the lock it holds; a render
  worker hands the bus to the control thread (taking the model lock there would deadlock
  against the render owner that waits for the worker); `destroyAudioBusHandle` keeps such a bus
  ALIVE until the control thread removed it from the list, so no worker can read a bus that was
  freed under it.
- `src/core/AudioBusHandle.cpp:130-146` `setExtOutputEnabled`: on the audio path the device
  call is replaced by `Engine::audioEngine()->requestDevicePortUpdate(this, enabled)`; the
  control thread applies it (`src/core/AudioEngine.cpp:1334-1368`), which checks that the bus
  is still registered before it hands it to the device.
- `src/core/SamplePlayHandle.cpp:437-448`: the standalone sample bus (the only heap-allocated
  bus in the tree) is destroyed through `AudioEngine::destroyAudioBusHandle()` instead of
  `delete`, and the play handle is taken out of the bus's play-handle list first, so a bus that
  outlives its handle never mixes a freed handle.
- `include/AudioEngine.h:199-260, 705-745`: the declarations and the three fixed-size queues.

### Every caller and sibling

- `addAudioBusHandle` has exactly one caller: the `AudioBusHandle` constructor
  (`src/core/AudioBusHandle.cpp:101`). The engine's own pool bus
  (`src/core/AudioEngine.cpp:148`) and the track buses (`include/InstrumentTrack.h:472`,
  `include/SampleTrack.h:124`) are constructed on the control thread and keep the old path.
- `removeAudioBusHandle` is called from the `AudioBusHandle` destructor
  (`src/core/AudioBusHandle.cpp:124`) and from `AudioEngine::destroyAudioBusHandle` (through the
  destructor, on the control thread). The engine's `m_audioBusHandles` readers
  (`src/core/AudioEngine.cpp:859` render stage, `:1261` capacity top-up,
  `src/core/Mixer.cpp:2119`/`:2184` latency graph, `src/core/ControlCommandsPdc.cpp:106`) all
  read the list under the model lock, as before.
- `setExtOutputEnabled` has two callers besides the constructor/destructor:
  `src/core/AudioEngine.cpp:159` (the pool bus switches its hardware port off - control thread)
  and the constructor/destructor.
- The device port functions are `AudioDevice::registerPort/unregisterPort`
  (`src/core/audio/AudioDevice.cpp:137-155`); the JACK overrides are behind
  `AUDIO_BUS_HANDLE_SUPPORT`, which this tree never defines, so the port call is a no-op in
  every build here - the fix defers it anyway, so a JACK build cannot put an allocation on the
  audio path.

## Regression tests (added)

`tests/src/core/AudioBusHandleTest.cpp` (the file the brief names for this area; +73 lines):

- `busCreatedOnTheRealtimePathIsRegisteredByTheRenderOwner()` (line 278) - BUG 1
  (new-p15-253). Inside a `RealtimeScope` (the marker the tree uses for the audio path) a bus is
  constructed. The test asserts that the engine's bus list does NOT contain it right after the
  constructor returned - the registration was deferred, not done inline - and that the render
  owner registers it at the start of the next period (`renderNextPeriod()`), and that the
  destructor removes it again.
- `engineBusRegistrationOnTheRealtimePathIsDeferred()` (line 304) - BUG 2
  (new-p12-313). The engine's registration path itself: a bus that the control thread
  registered is removed again, then `AudioEngine::addAudioBusHandle` is called inside a
  `RealtimeScope`. The test asserts that the bus list did not grow on the audio path and that
  the render owner grew it by exactly one in the next period.

Both tests use only the existing public API (`Engine::audioEngine()->audioBusHandles()`,
`AudioEngine::addAudioBusHandle/removeAudioBusHandle/renderNextPeriod`,
`AudioEngine::RealtimeScope`), so they compile against the unfixed code and fail on the
behaviour, not on a missing symbol. Deterministic: no wall-clock, no sleep, no race - the
realtime path is entered through the `RealtimeScope` the production code itself uses, and the
registration is observed through the engine's bus list.

## Commands run (all logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-253.r2/logs`)

| step | command (from the worktree / `build/tests`) | EXIT | log |
| --- | --- | --- | --- |
| build (first try) | `cmake --build build --target zene -j8` | 2 | build-1.log: `updates.size()` on a C array (my draft) |
| build | `cmake --build build --target zene AudioBusHandleTest -j8` | 0 | build-2.log |
| build (capacity fix) | `cmake --build build --target zene AudioBusHandleTest -j8` | 0 | build-3.log |
| **GREEN** (first green run) | `ctest -R '^AudioBusHandleTest$' --output-on-failure` | **0** | test-1.log: 1/1 passed |
| **GREEN** (direct run, count) | `./AudioBusHandleTest` | **0** | test-2-direct.log: `Totals: 9 passed, 0 failed` |
| family build | `cmake --build build --target <20 family targets> -j8` | 0 | build-6-family.log |
| family test | `ctest -R '^(AudioBusHandleTest\|...\|W3EngineTest)$'` | 0 | test-7-family.log: 20/20 passed |
| final build | `cmake --build build --target zene AudioBusHandleTest ... -j8` | 0 | build-7-final.log (counter cleanup) |
| patch | `git diff -- include/AudioEngine.h src/core/AudioEngine.cpp src/core/AudioBusHandle.cpp src/core/SamplePlayHandle.cpp > logs/fix.patch` | 0 | fix.patch (435 lines) |
| revert the fix | `git apply -R logs/fix.patch` | 0 | - |
| build (reverted) | `cmake --build build --target zene AudioBusHandleTest -j8` | 0 | build-8-red-final.log |
| **RED** (reverted) | `ctest -R '^AudioBusHandleTest$' --output-on-failure` | **8** | test-8-red-final.log: `FAIL! ... busCreatedOnTheRealtimePathIsRegisteredByTheRenderOwner() 'std::find(handles.begin(), handles.end(), bus) == handles.end()' returned FALSE` and `FAIL! ... engineBusRegistrationOnTheRealtimePathIsDeferred() Compared values are not the same`; `Totals: 7 passed, 2 failed` |
| **RED** (direct run) | `./AudioBusHandleTest` | **2** | test-9-red-final-direct.log: `Totals: 7 passed, 2 failed` |
| restore the fix | `git apply logs/fix.patch` | 0 | - |
| build (restored) | `cmake --build build --target zene AudioBusHandleTest -j8` | 0 | build-9-green-final.log |
| **GREEN** (restored) | `ctest -R '^AudioBusHandleTest$' --output-on-failure` | **0** | test-10-green-final.log: 1/1 passed |
| **GREEN** (restored, direct) | `./AudioBusHandleTest` | **0** | test-11-green-final-direct.log: `Totals: 9 passed, 0 failed` |
| family build (final) | `cmake --build build --target <20 family targets> -j8` | 0 | build-10-family-final.log |
| family test (final) | `ctest -R '^(AudioBusHandleTest\|...\|W3EngineTest)$'` | 0 | test-12-family-final.log: 20/20 passed |

The RED run is the final product code with only the product hunks reverted (`git apply -R
logs/fix.patch`); the test file was not touched. Both new tests failed, the other seven
functions of the same binary still passed.

### Affected family (final binaries)

| command | EXIT | result |
| --- | --- | --- |
| `ctest -R '^(AudioBusHandleTest\|AudioBusPlayHandleQueueTest\|MissingDevicesPreserveTest\|AudioClipLoopTest\|AudioEditingTest\|AudioResamplerRatioTest\|AudioStretcherTest\|ClipFadesRenderTest\|ClipOffsetFadePairTest\|ClipOffsetFadeTest\|FreezeRegionRepairTest\|FrozenTakeHandoffTest\|FrozenTakeRoundTwoTest\|InstrumentLatencyPdcTest\|MixerConcurrencyTest\|MixerToolsTest\|PdcMixerTest\|RackZoneFeedRunTest\|RackZoneRoutingTest\|W3EngineTest)$'` | 0 | 20/20 passed (test-12-family-final.log) |

The three tests the brief names for this area - `AudioBusHandleTest`,
`AudioBusPlayHandleQueueTest`, `MissingDevicesPreserveTest` - are part of that set and pass.

## Files committed

- `include/AudioEngine.h` - the realtime-safe registration API and the three fixed-size queues.
- `src/core/AudioEngine.cpp` - the deferred registration, the render owner's promotion, the
  realtime-safe removal and the control-thread device port update.
- `src/core/AudioBusHandle.cpp` - the device port update is no longer done on the audio path.
- `src/core/SamplePlayHandle.cpp` - the standalone bus is destroyed through the engine.
- `tests/src/core/AudioBusHandleTest.cpp` - the two regression tests.
- `tests/upstream-modifications.txt` - the divergence ledger entries for the four inherited
  product files and the test file.
- `docs/qwen-fixes-2026-10/cluster-new-p15-253.md` - this report.

No new product file, so `tests/fork-sources.txt` needs no entry. The four inherited files
changed are declared in `tests/upstream-modifications.txt` with a reason, as the ledger
requires.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → **EXIT 0** (log:
gate-1-feature-check.log):

```
feature-check zene-qwen-new-p15-253 (base b61e5f965, 7 changed files; logs in .../zene-qwen-new-p15-253-feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

The complexity and file-length ratchets report "no new item in your files": the four inherited
files I changed are not fork-NEW sources, and no new function of mine is over the CCN target or
in a file over the line limit. `git status --short` after the gates lists only the files this
report commits; no gate rewrote a file.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit
  codes).** RED: `ctest -R '^AudioBusHandleTest$' --output-on-failure` EXIT **8** (2 of the 9
  functions failed, both new ones; test-8-red-final.log). GREEN after `git apply logs/fix.patch`:
  EXIT **0**, `Totals: 9 passed, 0 failed` (test-10-green-final.log, test-11-green-final-direct.log).
- **The test is deterministic: no wall-clock timing, sleeps or races.** The realtime path is
  entered through `RealtimeScope` (the same marker the production code uses), the registration
  is observed through the engine's bus list, and the period is driven by the existing public
  `AudioEngine::renderNextPeriod()`. No timer, no sleep, no thread.
- **The fix is at the cause and complete: every caller and sibling was checked.** The shared
  cause - the inline registration in the `AudioBusHandle` constructor - is fixed once, in the
  engine's registration path (`AudioEngine::addAudioBusHandle`), and the two callers of the
  constructor path (the pool bus, the track buses) keep the control-thread behaviour. The
  sibling of the registration - the removal and the destruction of a bus - was fixed with the
  same mechanism (`removeAudioBusHandle`, `destroyAudioBusHandle`) and the only heap-allocated
  bus in the tree (`SamplePlayHandle`) now uses it. The device port setup (the other half of the
  constructor) was deferred to the control thread as well.
- **No collateral damage.** The control-thread behaviour is unchanged: a bus created on the
  control thread is still registered inline, under the model lock, and the device port is still
  set up there. The bus list is only appended to, never reordered, so the mix order and the
  latency graph see the same order as before. The only observable difference on the audio path
  is that a bus created there joins the render one period later - the period it was created in
  was already running. No project file, save format or undo step is touched; no command was
  added. The 20 tests of the affected family pass (test-12-family-final.log).
- **Realtime.** Nothing new on the audio path allocates, frees, locks, waits or logs: the
  audio path only does an atomic push into a fixed-size `MpscQueue` and an atomic counter
  increment (`AudioEngine::addAudioBusHandle`); the render owner only appends to a vector that
  the control thread keeps room in (`promotePendingAudioBusHandles`); the device call moved to
  the control thread (`setExtOutputEnabled` -> `requestDevicePortUpdate`). The removal on a
  render worker hands the bus over instead of taking the model lock the render owner holds.
- **Undo / commands.** No control command was added, no command id changed, so the
  `ControlCommandsSnapshot` canary is untouched. The fix changes no undo step: a bus is not an
  undoable model object, and its registration was never an undo step.
- **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
  contract that shows it."** Not applicable: both verdicts are FIXED (the defect was in the
  tree, the code is cited above, and the tests fail without the change). The contract that the
  fix keeps is the comment at `src/core/AudioBusHandle.cpp:93-95`: a standalone sample bus may
  be created on the audio path.

## Submit check

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p15-253` → **EXIT 0**
(log: submit-check-1.log):

```
PRE-SUBMIT CHECK for new-p15-253
  PASS  commits on the branch
  PASS  working tree clean
  PASS  report docs/qwen-fixes-2026-10/cluster-new-p15-253.md
  PASS  tests added or changed - tests/src/core/AudioBusHandleTest.cpp
  PASS  gates (feature_check.py)
RESULT: PASS - submit
```

The five ASK items are answered in the Self-check section above, each with its evidence
(the RED/GREEN exit codes, the deterministic seam, the caller and sibling check, the
collateral-damage argument, and the not-applicable note for the verdict-specific item).
