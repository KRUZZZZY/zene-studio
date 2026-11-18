# Bunny fixes — plugins batch-11

Lane: `plugins` (plugin hosts and plugins). Branch `bugs/bunny-plugins-2026-10-05`.
Six Luna clusters were investigated by hand against the current tree. No product code was
changed: every defect that is real turned out to have no deterministic headless test in this
build tree (reasons per finding), and one reported memory-corruption claim is provably not a
corruption.

## Summary

| Cluster | Title | Verdict |
|---|---|---|
| 3434 | VST MIDI sorting can allocate during processing | REAL-UNTESTED |
| 3433 | VST MIDI bursts can overrun fixed event arrays | REAL-UNTESTED |
| 3652 | Oversized blocks corrupt the delayed dry fallback | FALSE-POSITIVE |
| 5444 | Unloading a WASM module reports failure after unloading it | REAL-UNTESTED |
| 5439 | Zero-latency WASM modules still add an unreported block of latency | REAL-UNTESTED |
| 4571 | Loading settings with no module leaves a previously loaded module running | ALREADY-FIXED |

0 FIXED, 4 REAL-UNTESTED, 1 FALSE-POSITIVE, 1 ALREADY-FIXED, 0 DUPLICATE, 0 SKIPPED-SCOPE.

Environment note that decided three findings: `WANT_WASM` is `ON` in `build/CMakeCache.txt` but
`Wasmtime_INCLUDE_DIR-NOTFOUND`, so `CMakeLists.txt:1020` (`SET(WANT_WASM OFF)`) degrades it at
configure time. The `if(WANT_WASM)` guard at `tests/CMakeLists.txt:1852` is therefore false, and
the only target that compiles `plugins/WasmEffect/WasmEffect.cpp` (`tests/CMakeLists.txt:1860`)
does not exist:

    cmake --build build --target zene WasmSandboxTest > build/bunny-b.log 2>&1
    -> gmake: *** No rule to make target 'WasmSandboxTest'.  Stop.

Per the lane rules the build tree is never reconfigured, so no test can reach `WasmEffect.cpp`
here.

### Cluster 3434 — VST MIDI sorting can allocate during processing
Verdict: REAL-UNTESTED

Evidence: `plugins/VstBase/RemoteVstPlugin.cpp:1061-1065` — `RemoteVstPlugin::process()` calls
`std::stable_sort(m_midiEvents.begin(), m_midiEvents.end(), ...)` on the per-period processing
path. `m_midiEvents` is a `std::vector<VstMidiEvent>` (`:478-479`), and `std::stable_sort` obtains
a temporary buffer through `std::get_temporary_buffer` once the range exceeds its small-range
insertion-sort threshold, so a large MIDI burst does allocate on that path. The claim is real.

`RemoteVstPlugin.cpp` is compiled only into the remote client binary, never into a test target
(`rg -n 'WasmEffect.cpp|RemoteVstPlugin' tests/CMakeLists.txt` matches no VST source; the VST
entries at `:491-492` are the host-side E2E tests). `tests/src/core/RemotePluginAudioPortsTest.cpp:107-108`
states the client half of this protocol has no host-buildable implementation because
`RemoteVstPlugin`'s `process()` needs a real VST library. No deterministic failing test can be
written headlessly, so per the lane rules no product code was changed.

Commit: none
Test: none
Red/green: none

### Cluster 3433 — VST MIDI bursts can overrun fixed event arrays
Verdict: REAL-UNTESTED

Evidence: `plugins/VstBase/RemoteVstPlugin.cpp:1067-1076` — `events->numEvents` is assigned
`m_midiEvents.size()` with no bound (`:1069`), and the loop then writes `vme[idx]` and
`events->events[idx]` for every queued event (`:1072-1076`) into `static VstMidiEvent
vme[MIDI_EVENT_BUFFER_COUNT]` and `static char eventsBuffer[...]` sized for
`MIDI_EVENT_BUFFER_COUNT = 1024` (`:1054-1056`). Events are appended without any cap in
`RemoteVstPlugin::enqueueMidiEvent()` (`:1176`), and a bounded search confirms no clamp exists
anywhere in the directory (`rg -n 'MIDI_EVENT_BUFFER_COUNT|numEvents' plugins/VstBase` returns only
the definition and the unchecked assignment). More than 1024 events in one period therefore writes
past both static buffers. The overflow is real.

Same testability wall as cluster 3434: the code is reachable only inside the remote client binary,
which no headless test target builds, so per the lane rules no product code was changed.

Commit: none
Test: none
Red/green: none

### Cluster 3652 — Oversized blocks corrupt the delayed dry fallback
Verdict: FALSE-POSITIVE

Evidence: the check order is as reported — the `!m_worker.isReady()` branch
(`plugins/WasmEffect/WasmEffect.cpp:202-215`) runs before the `frameCount > maxBlockFrames` check
(`:217-221`) — but neither write can leave the delay line. `pushDry()` (`:265-272`) indexes
`m_delayLine[(m_delayWrite + i) % delayLineCapacity]` and `pullDelayed()` (`:274-280`) indexes
`m_delayLine[(m_delayWrite + i - delay) % delayLineCapacity]`. Both are modulo-bounded by
`delayLineCapacity` (`WasmEffect.h:121-122`, 24576 = 8192 + 2 * 8192), so a block larger than
`maxBlockFrames` overwrites history instead of writing out of bounds — the ordinary ring-buffer
behaviour — and the unsigned underflow in the read offset wraps into the same bounded domain.
The reported corruption cannot occur.

The residual observation, which is not the reported defect: for an oversized block the not-ready
path with nonzero latency returns stale dry history rather than the untouched pass-through the
ready path (`:217-221`) returns, so the two paths are inconsistent for `frames > 8192`. The engine
hands effects `framesPerPeriod()` frames, so this is not reachable in normal processing, and
changing it would be an untestable audio-behaviour change. Left alone.

Commit: none
Test: none
Red/green: none

### Cluster 5444 — Unloading a WASM module reports failure after unloading it
Verdict: REAL-UNTESTED

Evidence: `plugins/WasmEffect/WasmEffect.cpp:91-98` — an empty path calls `stopWorker()`, sets
`m_loaded = false`, clears the saved path via `m_controls->setModulePath(QString())`, refreshes the
chain latency and then `return false`. The caller
`plugins/WasmEffect/WasmEffectControls.cpp:73-80` treats that `false` as a load failure and sets
`m_error` to `tr("Failed to load %1").arg("")`, so a completed unload is reported as a failure with
a bogus message. The UI path is live: `plugins/WasmEffect/WasmEffectControlDialog.cpp:92` calls
`m_wasmControls->loadModule(path)` on the chooser result. The defect is real and the header comment
at `plugins/WasmEffect/WasmEffect.h:67` only documents `false` for a failed load, not for a
successful unload.

Not testable here: the sole target that compiles `WasmEffect.cpp` is `WasmSandboxTest`, which is
not generated in this build tree (`CMakeLists.txt:1020` degrades `WANT_WASM` to OFF; see the
environment note above, target missing from `cmake --build`). Per the lane rules no product code was
changed.

Commit: none
Test: none
Red/green: none

### Cluster 5439 — Zero-latency WASM modules still add an unreported block of latency
Verdict: REAL-UNTESTED

Evidence: `plugins/WasmEffect/WasmEffect.cpp:169-187` — `latencyFrames()` returns `0` early when
`compensatedModuleLatency()` is 0 (`:172-175`), which discards the host pipeline period. That
early return contradicts the file's own documented invariant at `:176-182` ("The audio thread
submits block N and collects block N-1, so the host adds exactly one block of pipeline delay on top
of whatever the module reports"), and `processImpl()` (`:241-252`) does always collect the previous
block before submitting the current one when the worker is ready, whether or not the module
declared latency. So a ready module declaring `0` is heard one period late while PDC is told the
effect adds nothing — the same misreporting class as the already-fixed nonzero-latency case that
`p6_moduleLoadRefreshesTheChainLatencyCache` covers. The claim is real.

Not testable here: `WasmEffect.cpp` is compiled only into the missing `WasmSandboxTest` target (see
the environment note). The fix would also change what the chain reports to PDC for every
zero-latency module, which is not a change to make without a red/green test. No product code was
changed.

Commit: none
Test: none
Red/green: none

### Cluster 4571 — Loading settings with no module leaves a previously loaded module running
Verdict: ALREADY-FIXED

Evidence: `plugins/WasmEffect/WasmEffectControls.cpp:162-171` — the empty-path branch of
`loadSettings()` already calls `m_effect->loadModule(QString())` before clearing `m_error`, and the
comment above it records the intent ("0.5.0 E22: an element without a module unloads the one
running ... instead of leaving it playing behind an empty path"). `WasmEffect::loadModule("")` stops
the worker (`plugins/WasmEffect/WasmEffect.cpp:91-98`), so the module does stop. `p2` in
`tests/src/wasm/WasmSandboxTest.cpp:704-711` already asserts the restored state has no module and no
ready worker. Fixed on the branch before this batch.

Commit: none (pre-existing on branch)
Test: `WasmSandboxTest::p2_effectSavesAndReloadsModuleAndParameters` (not runnable in this tree)
Red/green: none

Leftovers: three real defects (5439, 5444, 3433) and one real allocation hazard (3434) remain
unfixed because no buildable test target reaches `plugins/WasmEffect/WasmEffect.cpp` or
`plugins/VstBase/RemoteVstPlugin.cpp` in this worktree's build tree; they need either a
wasmtime-enabled configure (which the lane forbids) or a VST2 fixture. Cluster 3652 leaves the
not-ready oversized-block inconsistency noted above, deliberately unchanged.