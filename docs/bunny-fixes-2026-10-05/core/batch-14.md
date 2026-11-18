# Bunny core lane — batch 14

Six findings, each read against the current tree before any code was touched.
One is real and fixed with a regression test that failed first; two are false
positives, argued from the source line; one duplicates the fixed defect; one is
real but has no deterministic headless reproduction; one is out of this lane's
scope.

### Cluster 5050 — Sleeping effects can silence unrelated bus channels

Verdict: FALSE-POSITIVE

Evidence: for an effect with no audio-ports model the sleep check reads only
`quietChannels()[0]/[1]` while the silence branch calls `silenceAllChannels()`
(`src/core/Effect.cpp:261-274` against `src/core/AudioBus.cpp:339-349`), which
would indeed zero a later pair on a multi-pair bus — but no multi-pair bus
reaches that code. Every `AudioBus` construction in the non-3rdparty tree passes
`channelPairs == 1` (`src/core/Mixer.cpp:215`, `src/core/AudioBusHandle.cpp:51`,
`src/core/RackNodes.cpp:80`, `src/core/InstrumentOutputBuses.cpp:218`,
`src/core/ControlCommandsClipProcess.cpp:91`, `include/AudioPlugin.h:180`,
`include/AudioPlugin.h:336`), and the graph path the awake branch belongs to
requires the same (`canProcessThroughGraph()`, `src/core/EffectChain.cpp:359`).
The over-clear is unreachable, and closing it would need a new
`silenceChannels(ChannelFlags, …)` overload on `AudioBus` (header + source) for
a path nothing takes.

Commit: none
Test: none
Red/green: none

### Cluster 5051 — Clearing a chain can leave the audio thread using deleted effects

Verdict: REAL-UNTESTED

Evidence: `EffectChain::clear()` deletes the effects, releases the guard with
`doneChangeInModel()` (`src/core/EffectChain.cpp:448`) and only then calls
`rebuildRoutingGraph()` (`:454`) — unlike `appendEffect`, `removeEffect`,
`moveDown`/`moveUp`, which all rebuild inside the guard (`:193-211`, `:216-239`,
`:244-283`). The reported use-after-free window is narrower than stated: while
the graph is stale, `graphMirrorsEffectList()` (`:364-375`) is false, so
`canProcessThroughGraph()` (`:348-361`) is false and the audio thread takes the
plain empty-list loop instead of dereferencing stale nodes. What remains real is
that the rebuild mutates audio-thread-read state (`m_graph`, `m_effectNodes`,
`m_graphActive`, `m_patchDropped`, `m_patch`) after the guard is released.

Commit: none
Test: none
Red/green: none — `tests/src/core/MixerConcurrencyTest.cpp` serialises its
guard tests against the render period by hand (e.g.
`reorderingEffectsIsSerialisedWithTheRenderPeriod`) and has no `clear()`
counterpart; no deterministic headless reproduction of the interleaving was
found, so no product code was changed.

### Cluster 2241 — The AudioBuffer path skips the device delta tap

Verdict: FIXED

Evidence: `EffectChain::processAudioBuffer(AudioBuffer&)` called
`effect->processAudioBuffer(buffer)` directly (`src/core/EffectChain.cpp:304`)
while the `AudioBus` overload already ran every effect through
`effect->deltaTap().process(*effect, bus)` (`:336`) and the graph path does the
same (`src/core/RoutingChainNodes.cpp:98`). A device whose delta monitor is on,
processed through this overload, was heard in full instead of hearing only its
change. The loop now routes through `effect->deltaTap().process(*effect, buffer)`
— the overload that already exists at `src/core/DeltaTap.cpp:68-85`, allocation
free, and the plain output when the tap is off.

Commit: 80c518cf0
Test: PatcherCommandsTest::theAudioBufferPathAppliesTheDeltaTap
Red/green: `cmake --build build --target zene PatcherCommandsTest` → EXIT=0, then
`cd build/tests && ctest -R '^PatcherCommandsTest$' --output-on-failure` →
EXIT=8 (`FAIL!` on `theAudioBufferPathAppliesTheDeltaTap()` — Actual 2, Expected
1: the tap's change was not subtracted); after the one-line fix the same build and
ctest → EXIT=0 (12 passed, 0 failed). Covering suites rebuilt and rerun:
`cd build/tests && ctest -R
'^(PatcherCommandsTest|DeviceToolsTest|DeviceDspTest|MixerAbRegressionTest|MixerConcurrencyTest|ArrangingCatchUpTest|DeviceCopyTest)$' --output-on-failure` → EXIT=8, with
`DeviceToolsTest`, `DeviceDspTest`, `MixerAbRegressionTest`,
`MixerConcurrencyTest` and `PatcherCommandsTest` passing; `DeviceCopyTest` and
`ArrangingCatchUpTest` fail on `plugin not found` / `loadInstrument` falling back
to `dummyinstrument` because this tree's plugin shared objects were never built
(`build/plugins/Amplifier/` holds no `libAmplifier.so`) — a pre-existing
environment gap, not a consequence of this change, and neither suite's failing
case touches `EffectChain::processAudioBuffer(AudioBuffer&)`.
Gates: `bash tests/complexity-gate.sh --check` → EXIT=0, `bash
tests/file-length-gate.sh --check` → EXIT=0.

### Cluster 4147 — Delta monitoring is skipped on normal chain paths

Verdict: DUPLICATE

Evidence: both halves are already accounted for. The graph half does apply the
tap (`EffectNode::process`, `src/core/RoutingChainNodes.cpp:98`), and the
`AudioBuffer` half is the same defect as cluster 2241, fixed and committed above
as 80c518cf0.

Commit: none (fixed under 80c518cf0)
Test: PatcherCommandsTest::theAudioBufferPathAppliesTheDeltaTap
Red/green: see cluster 2241

### Cluster 1396 — Authored patch wiring is lost when saving a project

Verdict: FALSE-POSITIVE

Evidence: `EffectChain::saveSettings` / `loadSettings`
(`src/core/EffectChain.cpp:125-188`) indeed never touch `m_patch` — that is the
documented contract, not an omission: "There is no `<routinggraph>` element in
`<fxchain>`: a patch does not survive a save/load"
(`docs/PATCHER-GRAPH.md:99-101`) and "the edit is not lock-free (§2) and it is
not saved into the project" (`:105`). Persisting it would add a new project-file
element, a load-order requirement before `rebuildRoutingGraph()`, and a schema
change — a design change, not a defect fix.

Commit: none
Test: none
Red/green: none

### Cluster 1398 — PDC latency does not match patched signal paths

Verdict: SKIPPED-SCOPE

Evidence: `refreshLatency()` sums `compensatedLatencyFrames()` over the enabled
effects of the whole chain (`src/core/EffectChain.cpp:82-112`) and
`setPatchWiring()` does not call it (`src/core/EffectChainPatcher.cpp:215-246`),
but the missing call is unobservable: latency is a function of the effect list
and the enabled/okay/dontRun flags, and wiring changes neither, so a refresh
would recompute the identical value. Deriving latency from the graph's active
output paths instead is a semantic change to a published value, beyond a
behaviour-preserving fix.

Commit: none
Test: none
Red/green: none

Leftovers: cluster 5051's unguarded `rebuildRoutingGraph()` in
`EffectChain::clear()` is still there (one-line move inside the guard) but ships
without a fix, because no deterministic headless test could be built for it;
`DeviceCopyTest` and `ArrangingCatchUpTest` cannot pass in this tree until the
plugin targets are built.