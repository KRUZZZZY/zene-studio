# Automation render pre-pass — cluster 2647

## Result

| Cluster | Verdict | Fix commit | Red proof | Green proof | Gates |
| --- | --- | --- | --- | --- | --- |
| 2647 | FIXED | `c28751cef` | Baseline `PhaseDSidechainTest` exited 8: the mixer-fader and plug-in-parameter lookups blocked on `m_valueBufferMutex`; the initial LFO and linked-ramp probes also exposed missing period setup in the test harness. Reversing the same-period cache invalidations made `controlPreparedCacheRefreshesAfterPublishedChanges` fail (`refreshed != nullptr`), exit 8. | Final build exit 0; related automation, mixer, sidechain, modulation and plug-in suites passed 52/52 (CTest exit 0, 101.8 s). Focused `ModulationAudioViewTest` and `PhaseDSidechainTest` passed 2/2. | Complexity, file-length, fork-sources and no-upstream-regression gates all exit 0. RT-safety sweep exits 0: 40 declared paths, 4 hits in 3 existing allowlisted keys, 0 fresh keys. |

## Implementation

Every `AutomatableModel` registers through its base constructor, covering built-in controls, mixer controls and plug-in parameter subclasses. `AudioEngine` owns a geometrically grown array of atomic model slots. Control-side registration and removal take the existing change mutex; publication swaps in a fully built array and retires the old array only after render quiescence. Realtime teardown only tombstones a preallocated slot. Model teardown emits lifetime callbacks before taking the render guard, then removes the registry, Song-cache and link references under the guard. Teardown after engine retirement skips registry access.

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
