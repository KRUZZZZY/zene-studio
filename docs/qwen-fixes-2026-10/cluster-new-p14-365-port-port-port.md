FIXED

# Cluster new-p14-365-port-port-port: excess mapped MIDI controller events applied too early

## Verdict and integration

REAL on target `050/m0-train-15` (`aac861e62`). The accepted change `74c593d12` fixes the cause in
`plugins/Vst3Effect/Vst3MidiMapping.cpp`: once a parameter queue held five points, `addBoundedPoint` replaced the last
point's value while keeping that point's old sample offset. The RED assertions reproduce the newest value at frame 500
instead of its own frame, 700 (plain mapping) or 600 (two controller aliases).

I inspected the accepted patch with `git show` and applied it by hand. The target branch had added a `maxEvents`
parameter and a bounded drain capacity in `drainMidiIntoEventList`; the accepted branch's conflicting signature did
not have that parameter. The integrated signature makes the map mutable for `flush()` and retains the target's
`maxEvents` argument and capacity calculation. `HostedPlugin::Impl::run()` still passes the event-list room left after
carried events. The accepted fixed-size deferred queue and alias coalescing are retained.

There are two deliberate differences from the accepted commit. `MidiControllerMap::build()` clears deferred state so a
rebuild cannot carry an old controller value into a later request; a deterministic test covers that lifecycle. The
nullable flush check is in a small helper so `drainMidiIntoEventList` stays under the complexity ratchet. Neither
changes event ordering or the event-cap behavior.

## Cause, fix, and affected paths

`addBoundedPoint` now admits up to four points, leaving the SDK's fifth reserved point free. `apply()` stores the newest
overflow point's frame and value in fixed arrays. A second mapped controller for the same parameter updates that same
pending point. `drainMidiIntoEventList()` flushes each pending parameter after processing its sorted events and before
the host slices parameter changes into chunks. Thus the latest value reaches the plug-in at its own frame while no
queue grows beyond `kMaxPointsPerParameter` (five).

The audio-thread path is `HostedPlugin::Impl::run()` (`Vst3Host.cpp:688`) -> `drainMidiIntoEventList()` ->
`MidiControllerMap::apply()` -> `addBoundedPoint()` / `defer()` -> `flush()`. The only product caller of
`addBoundedPoint()` is `apply()`; `drainMidiIntoEventList()` has the host caller and test callers. Other mapped MIDI
types (CC, channel pressure, and pitch bend) use the same `apply()` queue path. `sliceParameterChanges()` remains
downstream of the drain and receives the flushed point.

## Test matrix

| Path | Check and result |
| --- | --- |
| Normal mapped events | Existing `testMappedControllersKeepTheirFrame` injects explicit frame offsets below the queue limit and checks their own frames; the focused VST3 host test passes. |
| Queue exhaustion | `testAFullParameterQueueKeepsTheNewestValue` injects seven timestamped events for one parameter. The old code fails at frame 500; the fix keeps the first four points and the newest value at frame 700, with exactly five points. |
| Aliased controllers | `testAliasedControllersKeepNewestDeferredPoint` fills the queue through CC74, then sends CC74 and CC75 mapped to the same parameter. The old code reports frame 500; the fixed queue reports the newest alias at frame 600. |
| Reconfiguration | `testRebuildingControllerMapClearsDeferredPoints` makes a pending point, rebuilds the map, then drains an empty queue into a fresh parameter-change list; no old point is emitted. `prepare()` calls `release()` before rebuilding the map (`Vst3Host.cpp:491-495, 564`). |
| Null or unavailable queue | `apply()` returns before deferring when `changes` is null. If `addParameterData()` returns null, no pending point is recorded, matching the prior queue-failure path. `flush()` checks for null changes, a missing queue, and a full queue before adding a point. The normal host path creates the queue in `apply()` before deferring. |
| Event-list exhaustion | The target's `maxEvents` cap is preserved. Unconsumed MIDI remains queued for the next request; only points from events drained for this request are flushed. The VST3 family passes. |
| Audio-thread constraints | Deferred values and keys are fixed `std::array` storage. `defer()` and `flush()` do not allocate, free, lock, wait, log, perform I/O, or emit signals. The queue's existing five-point reservation remains the bound. `build()` clears the array during prepare, after `release()`. |
| Supported-platform requirements | The change uses C++20, standard fixed arrays, and existing VST3 `ParamID` / `int32` queue APIs. A key is at most `16 * 130 - 1 = 2079`, which fits `uint16_t`; sample offsets remain `int32`. The Linux build compiled the app and all configured VST3 targets. Windows/MSVC and macOS binaries were not available for cross-build in this worktree. |

The mapped-queue failure case remains the previous behavior: if the SDK cannot provide a queue, the mapped event is
consumed without a parameter point. This port does not broaden that failure policy. No project data, save format, undo
path, or control command changed.

## Registration and gates

No new product source or test file was added. The four VST3 product files were already in `tests/fork-sources.txt`;
the existing test's `tests/upstream-modifications.txt` entry now includes this coverage. The first `feature_check.py`
run found the train-15 `tests/all-sources.txt` snapshot still contained 1,148 vendored sfizz paths despite its own
recipe excluding that tree, and reported CCN 11 in `drainMidiIntoEventList`. I re-derived only the manifest entry list
from its published recipe and extracted the null check into the helper. The final gate run passes all eight rows.

The commit contains these eight paths: `plugins/Vst3Effect/Vst3MidiEvent.cpp`,
`plugins/Vst3Effect/Vst3MidiEvent.h`, `plugins/Vst3Effect/Vst3MidiMapping.cpp`,
`plugins/Vst3Effect/Vst3MidiMapping.h`, `tests/src/plugins/Vst3HostTest.cpp`, `tests/upstream-modifications.txt`,
`tests/all-sources.txt` (recipe-derived gate repair), and this report.

## Evidence

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-365-port-port-port.r1/logs/`.

| Step | Exact command | EXIT | Evidence |
| --- | --- | ---: | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target Vst3HostTest -j4` | 0 | `003-red-build.log` |
| RED focused test, before production edit | `ctest -R '^Vst3HostTest$' --output-on-failure` (from `build/tests`) | 8 | `004-red-test.log`: 19 passed, 2 failed; frame 500 vs 700 and 500 vs 600. |
| 4b save product-only patch | `git diff -- plugins/Vst3Effect/Vst3MidiMapping.cpp plugins/Vst3Effect/Vst3MidiMapping.h plugins/Vst3Effect/Vst3MidiEvent.cpp plugins/Vst3Effect/Vst3MidiEvent.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-365-port-port-port.r1/logs/fix.patch` | 0 | `009-save-product-patch.log` |
| 4b reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-365-port-port-port.r1/logs/fix.patch` | 0 | `009-save-product-patch.log` |
| 4b mutant rebuild | `CCACHE_DISABLE=1 cmake --build build --target Vst3HostTest -j4` | 0 | `012-mutant-final-build.log` |
| 4b mutant focused test | `ctest -R '^Vst3HostTest$' --output-on-failure` (from `build/tests`) | 8 | `013-mutant-final-test.log`: the same two frame assertions fail; 20 passed, 2 failed. |
| 4b restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-365-port-port-port.r1/logs/fix.patch` | 0 | `014-reapply-fix.log` |
| 4b restored rebuild | `CCACHE_DISABLE=1 cmake --build build --target Vst3HostTest -j4` | 0 | `015-restored-build.log` |
| 4b restored focused test | `ctest -R '^Vst3HostTest$' --output-on-failure` (from `build/tests`) | 0 | `016-restored-test.log` |
| Recipe-based source manifest repair | The manifest's published recipe, exact command below | 0 | `021-all-sources-recipe.log`; exactly the 1,148 recipe-excluded vendor entries were removed. |
| Final affected-target build | `CCACHE_DISABLE=1 cmake --build build --target zene Vst3HostTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3InstrumentTest Vst3InstrumentIntegrationTest Vst3InstrumentFixtureProbe Vst3BusMapTest -j4` | 0 | `022-final-affected-build.log` |
| Final focused test | `ctest -R '^Vst3HostTest$' --output-on-failure` (from `build/tests`) | 0 | `023-final-focused-test.log` |
| Final VST3 test family | `ctest -R 'Vst3' --output-on-failure` (from `build/tests`) | 0 | `024-final-vst3-family.log`: 7/7 tests passed. |
| Direct host test count | `./Vst3HostTest` (from `build/tests`) | 0 | `025-final-host-direct.log`: 22 passed, 0 failed. |
| Final feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-new-p14-365-aac861e62b` | 0 | `026-final-feature-check.log`: all eight gates PASS. |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-new-p14-365-aac861e62b new-p14-365-port-port-port` | pending | To be recorded after this report is committed. |

The exact manifest-generation command was:

```sh
{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-365-port-port-port.r1/logs/021-all-sources-generated.txt
```

It exited 0; the generated scratch file was removed after updating the manifest.

The focused VST3 host test is one ctest entry. The VST3 family ran seven ctest entries; `Vst3HostTest` itself reports
22 Qt test functions.

## Self-check

- **Regression proof:** before the fix, `004-red-test.log` exits 8 with the final point at frame 500 instead of 700 and
  600. For step 4b, `git apply -R` exits 0, the mutant rebuild exits 0, and `013-mutant-final-test.log` exits 8 with the
  same two assertions failing. Reapplying the product patch exits 0; rebuild exits 0; `016-restored-test.log` exits 0.
  Final focused test exits 0 and direct run reports 22 passed, 0 failed.
- **Deterministic:** tests inject explicit MIDI frame offsets (100 through 700). No wall-clock timing, sleeps, thread
  races, or asynchronous synchronization is used; `drainMidiIntoEventList` is the test seam.
- **Cause and callers:** `addBoundedPoint` has one product caller, `MidiControllerMap::apply`; all mapped controller
  types share that call. `apply` is reached from the sorted event loop in `drainMidiIntoEventList`; the host caller
  passes the mutable controller map, parameter changes, and its remaining event-list capacity. The drain flushes before
  chunk slicing. Existing tests also call the default-capacity overload. Alias mappings and map rebuild are covered.
- **No collateral damage:** saved projects and parameter serialization are untouched. The five-point SDK limit is not
  increased; a full queue retains four earlier points and the newest deferred point. A host parameter value already
  queued at frame zero consumes one of those same five slots as before. The unchanged `maxEvents` bound is still used
  by the host. No commands or undo actions were added.
- **Realtime:** `defer`, `flush`, and `addBoundedPoint` use fixed storage and the existing SDK queue. The deferral path
  starts only after `addParameterData` returned the queue that already exists for the parameter; at most one deferred
  point per parameter is written. `MidiControllerMap::build` clears state during prepare, after release has stopped MIDI
  producers.
- **Verdict evidence:** `MidiMapping.cpp:133` is the fixed bounded-point cause; the old implementation reset an
  overflow point's `sampleOffset` to the last point's frame. RED and mutant logs prove the finding is REAL. No
  ALREADY-FIXED or FALSE-POSITIVE claim is made.
- **Platform review:** the only new key narrowing is from the fixed range 0..2079 to `uint16_t`; offsets and parameter
  IDs use existing VST3-compatible widths. Linux targets were built. Cross-platform builds were not run here.
- **Gates:** the first feature check exited 1 for the train-stale manifest and CCN 11; after the recipe re-derivation and
  helper extraction, final `feature_check.py` exits 0 with all gates PASS. The completion-check result will be recorded
  here after running it on the committed tree.
