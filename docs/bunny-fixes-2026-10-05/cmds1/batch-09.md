# Batch 09 — Space Bunny lane `cmds1`

Six findings triaged against the current tree: four fixed, two false positives.
No command was added, removed or renamed and no command's params or result
schema changed.

Environment note: the build tree has `LIST_PLUGINS:BOOL=OFF`, so no plugin module
was built and every plugin-based case skipped — and
`DeviceCopyTest::aChainIsBypassedAndCleaned` failed on the clean tree. Building
the `amplifier` target (`cmake --build build --target amplifier`, EXIT=0,
producing `build/plugins/libamplifier.so`) resolved it; all plugin-based cases
then run. Tests needing other modules still skip.

## Cluster 5638 — A/B recall undo becomes a no-op after the effect is recreated

Verdict: FIXED

Evidence: `src/core/ControlCommandsDeviceCopy.cpp` `restoreAb()` began with
`if (effect == nullptr) { return; }` on a captured `QPointer<Effect>`; the undo
step captured no way to find the device again, so once the effect had been
unloaded and undone the pointer was null and the pre-recall settings were never
restored. Now an `AbRecorded` (chain finder + `QPointer` + slot + plugin name)
is recorded and `findRecorded()` resolves the device the way
`ControlStructuralSupport.cpp:416-424` `madeEffect` already does: the live
pointer while it is still on the chain, else the device at the recorded slot
whose `descriptor()->name` matches. `restoreAb` also sets `restored.owner`, so a
rebuilt device keeps its slots.

Commit: `b479c053b`

Test: `DeviceCopyTest::anAbRecallUndoSurvivesTheDeviceBeingRecreated`

Red/green:
- red: `cmake --build build --target zene DeviceCopyTest` EXIT=0; `cd build/tests && ctest -R '^DeviceCopyTest$' --output-on-failure` EXIT=8 (`'std::abs(parameterValue(target, back, name) - stored) < 1e-3' returned FALSE)
- green: same build EXIT=0; `ctest -R '^DeviceCopyTest$'` EXIT=0 (18 passed, 0 failed, 1 skipped); `ctest -R '^DeviceToolsTest$'` EXIT=0
- gates: complexity EXIT=0, file-length EXIT=0

## Cluster 5639 — plugin.ab store changes session state without undo

Verdict: FIXED

Evidence: the `store` branch of `pluginAb` in
`src/core/ControlCommandsDeviceCopy.cpp` rewrote `effect->abSlots()` and the
current-slot flag and returned success, while `plugin.ab` is registered as a
mutating command. Storing a slot and then undoing reached an earlier action and
left the stored slot in place. The branch now captures `AbSlots before` and
calls `addUndoStep(...)` keyed by `effect->id()`.

Commit: `223cd64ed`

Test: `DeviceCopyTest::aStoredSlotIsUndone`

Red/green:
- red: `cmake --build build --target zene DeviceCopyTest` EXIT=0; `cd build/tests && ctest -R '^DeviceCopyTest$' --output-on-failure` EXIT=8 (`'!after.value("has_a").toBool()' returned FALSE)
- green: same build EXIT=0; `ctest -R '^DeviceCopyTest$'` EXIT=0 (16 passed, 0 failed, 1 skipped); `ctest -R '^DevicePanelTest$'` EXIT=0
- gates: complexity EXIT=0, file-length EXIT=0

## Cluster 3868 — Parameter search misses post-fader inserts

Verdict: FIXED

Evidence: `searchTarget()` in `src/core/ControlCommandsDeviceParams.cpp` visited
`target.chain` and the channel's rack chains and then returned; the post-fader
insert chain — a target namespace of its own (`include/MixerPostInserts.h:92`
`targetOf`, `:93` `isTarget`, resolved by `resolvePostInsertsTarget()` at
`src/core/ControlDeviceSupport.cpp:218-234`) — was never searched. Since the
project-wide scan (`paramSearch()` at line 252) walks tracks and channels only,
a parameter living only in a post-fader insert was unreachable by any search.
`searchTarget()` now also searches that chain and labels its hits with the
`ch-<n>/post` target that addresses them.

Commit: `989114333`

Test: `DeviceToolsTest::aPostFaderInsertParameterIsFound`

Red/green:
- red: `cmake --build build --target zene DeviceToolsTest` EXIT=0; `cd build/tests && ctest -R '^DeviceToolsTest$' --output-on-failure` EXIT=8 (`'found.result.value("count").toInt() >= 1' returned FALSE)
- green: same build EXIT=0; `ctest -R '^DeviceToolsTest$'` EXIT=0 (14 passed, 0 failed, 0 skipped); `cmake --build build --target W3EngineTest` EXIT=0; `ctest -R '^(DeviceCopyTest|W3EngineTest)$'` EXIT=0
- gates: complexity EXIT=0, file-length EXIT=0

## Cluster 4761 — plugin.randomise reports unchanged parameters as changed

Verdict: FIXED

Evidence: `writeValues()` in `src/core/ControlCommandsDeviceParams.cpp` wrote
every selected model and set `changed` to the number of models, without asking
whether a value actually differed from the model's own current value. With
`amount: 0` every value stayed put yet the reply claimed all of them changed and
the song was marked modified. It now skips values equal to the current value,
adds an undo step only when the write list is non-empty, and reports
`changed = writes.size()` — the same no-op rule as `writeModelsRecorded()`
(`src/core/ControlCommandsDeviceTools.cpp:84-90`).

Commit: `ab2d66d44`

Test: `DeviceToolsTest::aRandomiseThatChangesNothingIsAnEmptyUndoStep`

Red/green:
- red: `cmake --build build --target zene DeviceToolsTest` EXIT=0; `cd build/tests && ctest -R '^DeviceToolsTest$' --output-on-failure` EXIT=8 (`Compared values are not the same`)
- green: same build EXIT=0; `ctest -R '^DeviceToolsTest$'` EXIT=0 (12 passed, 0 failed, 0 skipped); `ctest -R '^DeviceCopyTest$'` EXIT=0
- gates: complexity EXIT=0, file-length EXIT=0

## Cluster 2815 — Unknown rack split modes are accepted

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlCommandsDeviceTools.cpp:360` declares the split's
`mode` as `enumProperty(RackSplit::modeNames())`, so the schema only admits a
member of `RackSplit::modeNames()`; `checkEnum()` (`src/core/ControlSchema.cpp:102-110`)
rejects anything else, and `src/core/ControlRegistry.cpp:251-255` turns that into
a `ControlErrorKind::InvalidArgs` failure. The `RackSplit::modeFromName` return
value at line 147 is indeed ignored, but the value handed to it has already been
validated against the same name list, so it cannot fail there. A regression case
added to `DeviceToolsTest` (`anUnknownRackSplitModeIsRefused`) passed against
unmodified product code, confirming the rejection happens at schema validation;
the case was then reverted and the tree left clean.

Commit: none

Test: none (the case added to confirm this was reverted)

Red/green: none

## Cluster 2798 — Band splitting can request more bands than the rack has chains

Verdict: FALSE-POSITIVE

Evidence: the band count follows the chains, not the crossovers.
`RackSplitFilter::configureBands()` (`src/core/RackSplit.cpp:107`) computes
`const int bands = std::min(count, RackSplit::MaxBands);` from `count`, which
`src/core/Rack.cpp:142-147` passes as `static_cast<int>(routed.size())` — the
number of routed chains, one per chain node. A two-chain rack therefore renders
two bands no matter how many crossovers were set, and `readCrossovers()`
(`src/core/ControlCommandsDeviceTools.cpp:100-108`) already caps the stored
crossovers at `MaxBands - 1`. Surplus crossovers are simply unused, so nothing
is dropped: the tree at lines 113-119 still sums all-pass, and
`ControlRackSupport.cpp:207-213` reports the same `min(chainCount, MaxBands)`
band count back. The report's premise — that six bands are "accepted" and the
rest omitted — does not hold; the request is clamped at render time.

Commit: none

Test: none

Red/green: none

Leftovers: the build tree still has `LIST_PLUGINS:BOOL=OFF`; only `amplifier`
was built, so cases needing other modules (the gate module, for one) skip rather
than fail. Not a lane change — enabling plugins needs a cmake configure.
