# Automation render pre-pass — cluster 2647

## Result

| Cluster | Verdict | Fix commit | Red proof | Green proof | Gates |
| --- | --- | --- | --- | --- | --- |
| 2647 | FIXED | `c28751cef` | Baseline `PhaseDSidechainTest` exited 8: the mixer-fader and plug-in-parameter lookups blocked on `m_valueBufferMutex`; the initial LFO and linked-ramp probes also exposed missing period setup in the test harness. Reversing the same-period cache invalidations made `controlPreparedCacheRefreshesAfterPublishedChanges` fail (`refreshed != nullptr`), exit 8. | Final build exit 0; related automation, mixer, sidechain, modulation and plug-in suites passed 52/52 (CTest exit 0, 101.8 s). Focused `ModulationAudioViewTest` and `PhaseDSidechainTest` passed 2/2. | Complexity, file-length, fork-sources and no-upstream-regression gates all exit 0. RT-safety sweep exits 0: 40 declared paths, 4 hits in 3 existing allowlisted keys, 0 fresh keys. |

## Implementation

Every `AutomatableModel` registers through its base constructor, covering built-in controls, mixer controls and plug-in parameter subclasses. `AudioEngine` owns a geometrically grown array of atomic model slots. Control-side registration and removal take the existing change mutex; publication swaps in a fully built array and retires the old array only after render quiescence. In a render-owner teardown, unregistering tombstones a preallocated slot; the destructor also emits lifetime callbacks, removes Song-cache and link references, and destroys any controller connection before it returns. Teardown after engine retirement skips registry access. Worker destruction during a period is forbidden because its change guard would wait on the render lock while the render owner waits for that worker. Round 2 adds a debug assertion and tests the worker guard predicate; this is an asserted ownership invariant, not deferred worker teardown.

Each model publishes its immediately previous and new fitted float as one packed, always-lock-free 64-bit atomic, with a lock-free revision counter. The serial render pass consumes that pair after `Song::processNextBuffer()` and before worker jobs. It prepares every registered model and each reachable controller, including linked models and shared LFOs. Same-period model, controller-link and automation-ramp changes invalidate an earlier control-side cache so the render pass sees the latest state.

Mixer, effect and plug-in workers now only look up prepared model and controller buffers. They do not acquire `m_valueBufferMutex` or compute a stale cache; an unregistered request and an unprepared worker request return no buffer and increment separate probes. Controller updates run once in the serial preparation path. `currentValue()` workers read the scalar prepared at offset zero without writing shared controller state. The sidechain amount now uses the same prepared lookup as faders and sends.

## Render and realtime evidence

The pre-implementation ramp, controller and manual sidechain outputs are pinned in the regression test and remained bit-identical:

| Source | Before/after FNV-1a hash | First → last sample |
| --- | --- | --- |
| Ramp | `323a07fa3203dfb1` | `0 → 1` |
| Controller | `37f34ff56c876b74` | `0.5 → 0.508344412` |
| Manual change | `7fad6c72da607dde` | `0 → 0.99609375` |

The linked-model test compares every sample bit-for-bit against the interpolation reference and confirms both linked buffers match. The parallel mixer test drives controls on two tracks from one LFO, checks every output frame against the prepared LFO buffer, pins output SHA-256 `e5e6ba72936e7367bc138608fd1ff1768562759a14f6935cb274aafa50e8708e`, and observes exactly one LFO update with no model or controller cache misses. The lock sentinel and allocation probes cover a mixer fader and an effect plug-in parameter; both complete while the old mutex is held, and both report zero getter allocations.

The first full CTest attempt also exposed that the modulation lifetime test's synthetic writer was missing the `RealtimeScope` used by the real render path, and that destruction callbacks must run before the change guard is held. Both were corrected; the final 52-test run includes the modulation lifetime suite and passes.

The initial plug-in scan tried to write its default cache under `Documents/Zene Studio` and received `Permission denied`. Subsequent runs used `LMMS_PLUGIN_SCAN_CACHE=/tmp/zene-automation-stage2-plugin-scan-cache.json` and completed successfully.

`AutomationClip` was not changed in this stage.

## Round 2 proof

| Review row | Proof | Round 2 status |
| --- | --- | --- |
| Setter interpolation start | Rendered a full period at `0.2`, then called `setValue(0.5)` and `setValue(0.9)` before the next period. The legacy buffer is computed with `ValueBuffer::interpolate(0.5, 0.9)`; every full-mix sample must match it bit-for-bit and differ from the `0.2 → 0.9` last-rendered alternative. | PASS. A negative-control build that used the last-rendered scalar failed the bitwise full-mix assertion at frame 0 (actual bits `1041865114`, expected `1052770304`; CTest exit 8). Restoring the published setter pair passed. |
| Full mixer output | `AutomationPrepassProofTest` hashes every interleaved stereo frame from the master mix for an automation ramp, non-LFO controller, LFO, the manual write sequence, and linked faders. Baseline hashes below were captured twice from the `c405233f8` build. | PASS. The final current-tree build matched all five baseline SHA-256 digests; CTest exit 0 (4 QtTest cases passed). |
| Worker teardown | `AutomatableModel::~AutomatableModel()` asserts the exact worker-RealtimeScope predicate that would take a blocking change guard. The test checks that predicate on the control thread and on a worker inside `RealtimeScope`; the deletion-path audit is in `docs/m0-design/automation-lockfree.md`. | PASS. Proof target built and CTest passed; worker predicate was false on the control thread and true on a worker. The assertion enforces the audited ownership invariant in debug builds. |
| Gates and RT sweep | Complexity, file-length, fork-sources, no-upstream-regression, and `rt-safety-sweep.py --check`. | PASS. All five gates exited 0; RT sweep: 40 declared paths, 4 hits in 3 existing allowlisted keys, 0 fresh keys. |

Baseline procedure: a detached scratch worktree at `c405233f8` was configured for Qt 6 RelWithDebInfo with `BUILD_TESTING=ON` and `USE_WERROR=ON`. The new proof source and CTest registration were copied into that scratch tree; only the scratch copy of `PhaseDMixerTestSupport.h` was adjusted to advance `Controller::triggerFrameCounter()` at the same points as `AudioEngine::renderStageMix()`. `cmake --build build --target AutomationPrepassProofTest -- -j4` exited 0, then `ctest -V -R AutomationPrepassProofTest --output-on-failure` from `build/tests` exited 0 twice with identical output hashes. Each digest covers the bytes of all `framesPerPeriod()` stereo `SampleFrame`s returned by `Mixer::masterMix()`.

| Source | SHA-256 from `c405233f8` |
| --- | --- |
| Automation ramp | `a7348883c00f4f81640c7f0f022a02d4ed706e60312431da5fcf5024d18c5a69` |
| Non-LFO controller | `2c66cf0aa6d576d4259b58ea033b76717707f45abfa31e80d0c425c98cf3ec09` |
| LFO controller | `bc60bfe085f932a2d312bcf9ef4a7988aebb26059316d904d005e58b93488a15` |
| Manual `0.2 → 0.5 → 0.9` | `ad70d7f35245e58080ca946254d2d49c17d3834eb2d1494acffe354dde3012d8` |
| Linked faders | `ad70d7f35245e58080ca946254d2d49c17d3834eb2d1494acffe354dde3012d8` |
