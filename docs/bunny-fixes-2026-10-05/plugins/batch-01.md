# Space Bunny fixer lane `plugins` — batch-01 report

Worktree: `zene-bunny-plugins`, branch `bugs/bunny-plugins-2026-10-05`.
Scope: plugin hosts and plugins (`plugins/ClapEffect`, `plugins/CarlaBase`).

### Cluster 5513 — CLAP sidechain mapping ignores the main-port flag

Verdict: FIXED

Evidence: `plugins/ClapEffect/ClapBusMap.cpp:61-62` assumed the first input port was
the main input and the second was the sidechain, never reading the `isMain` flag, so a
plug-in that reports its sidechain before its main input had its channels swapped.
`ClapBusMap.cpp` now orders input ports by `isMain` and maps the remainder as sidechains.

Commit: 893a28421

Test: `ClapBusMapTest::testSideChainReportedBeforeTheMainInput`,
`ClapBusMapTest::testNoMainFlagKeepsTheFirstInputAsMain`

Red/green: `cmake --build build --target zene ClapBusMapTest` EXIT=0;
`cd build/tests && ctest -R '^ClapBusMapTest$' --output-on-failure` EXIT=8 (Actual
`std::vector(1, 2)`, Expected `{2,1}`) — green after the fix, EXIT=0. Sibling suites
`ClapHostTest|ClapLoaderErrorTest|ClapEffectIntegrationTest` EXIT=0;
`bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 3745 — Invalid parameter ranges reach `std::clamp`

Verdict: FIXED

Evidence: `plugins/ClapEffect/ClapHostParams.cpp` stored plug-in bounds verbatim, so a
plug-in reporting `minValue = 1`, `maxValue = 0` reached `std::clamp` in `setParamPlain()`
with unordered bounds (UB). A new `sanitizedRange()` maps non-finite bounds to 0..1,
swaps a reversed pair and clamps the default into the surviving range.

Commit: 1e0399d5d

Test: `ClapHostTest::testAnInvalidParamRangeIsNormalized` (fixture env `CLAP_TEST_GAIN_BAD_RANGE`)

Red/green: `cmake --build build --target zene ClapHostTest` EXIT=0;
`cd build/tests && ctest -R '^ClapHostTest$' --output-on-failure` EXIT=8
(`'gain.minValue <= gain.maxValue' returned FALSE ... 1 .. 0`) — green after the fix, EXIT=0.
`ClapBusMapTest|ClapLoaderErrorTest|ClapEffectIntegrationTest` EXIT=0; both gates EXIT=0.

### Cluster 1902 — Read-only CLAP parameters are presented as editable knobs

Verdict: FIXED

Evidence: the knob grid filtered `descriptor.hidden` but not `descriptor.readOnly`, so a
visible read-only parameter got a knob whose setter ignores edits and snaps back on the
next poll. `plugins/ClapEffect/ClapKnobGrid.h` now skips `hidden || readOnly`.

Commit: b85f5f5f8

Test: `ClapEffectIntegrationTest::testReadOnlyParametersGetNoKnob` (fixture env `CLAP_TEST_GAIN_READONLY`)

Red/green: `cmake --build build --target zene ClapEffectIntegrationTest` EXIT=0;
`cd build/tests && ctest -R '^ClapEffectIntegrationTest$' --output-on-failure` EXIT=8
(knobCount 2, expected 1) — green after the fix, EXIT=0.
`ClapBusMapTest|ClapHostTest|ClapLoaderErrorTest` EXIT=0; both gates EXIT=0.

### Cluster 5514 — CLAP processing errors leave stale wet audio in the mix

Verdict: FIXED

Evidence: `plugins/ClapEffect/ClapEffect.cpp` `processImpl()` ignored the bool returned by
`m_plugin.process(...)`; on failure the unwritten remainder of the output block still held
the previous buffer's content. It now falls back to `passThrough(in, out, sideChainChannel)`
for the whole block and returns `Continue`.

Commit: d13fe9570

Test: `ClapEffectIntegrationTest::testAProcessingErrorFallsBackToDryAudio` (fixture env `CLAP_TEST_GAIN_PROCESS_ERROR`)

Red/green: `cmake --build build --target zene ClapEffectIntegrationTest` EXIT=0;
`cd build/tests && ctest -R '^ClapEffectIntegrationTest$' --output-on-failure` EXIT=8
(`left 0 is not the dry signal`) — green after the fix, EXIT=0.
`ClapBusMapTest|ClapHostTest|ClapLoaderErrorTest` EXIT=0; both gates EXIT=0.

### Cluster 4691 — Active `request_flush` requests are cleared without a flush

Verdict: FIXED

Evidence: `plugins/ClapEffect/ClapHost.cpp` cleared `Impl::flushRequested` at the end of
every `process()` request without calling the plug-in's `params.flush()`, and
`serviceMainThread()` only flushes while the plug-in is inactive — so a flush an active
plug-in requested from its own UI was silently dropped. `HostedPlugin::Impl::serviceActiveFlush()`
(declared at `plugins/ClapEffect/ClapHostInternals.h:205`, defined at
`plugins/ClapEffect/ClapHost.cpp:422`) now
answers the request on the audio thread with the preallocated `inEvents`/`outEvents` lists
before clearing the flag: no allocation, no lock, one atomic exchange.

Commit: 635d5300e

Test: `ClapHostTest::testAnActiveFlushRequestIsServicedOnTheAudioThread` (fixture env `CLAP_TEST_GAIN_REQUEST_FLUSH`)

Red/green: `cmake --build build --target zene ClapHostTest` EXIT=0;
`cd build/tests && ctest -R '^ClapHostTest$' --output-on-failure` EXIT=8
(`state.hostFlushes` Actual 0, Expected 1) — green after the fix, EXIT=0.
`cd build/tests && ctest -R '^(ClapHostTest|ClapBusMapTest|ClapLoaderErrorTest|ClapEffectIntegrationTest)$' --output-on-failure` EXIT=0;
`bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 5508 — Carla locks a mutex in the audio processing path

Verdict: SKIPPED-SCOPE

Evidence: `plugins/CarlaBase/Carla.cpp:536` — `CarlaInstrument::playImpl()` takes
`QMutexLocker ml(&fMutex)` around the third-party `fDescriptor->process(...)` call, while
`handleMidiEvent()` (`Carla.cpp:557`) takes the same mutex from another thread, so audio
thread processing can block. The finding is real in fact, but the fix requires making
plugin processing and MIDI event staging audio-thread-owned behind a bounded lock-free
exchange — a redesign of the Carla host state machine, well beyond a minimal behaviour-
preserving change and the realtime scope of this lane. No code changed.

Commit: none

Test: none

Red/green: none

Leftovers: cluster 5508 (Carla audio-thread mutex) is left unfixed by design and needs its own
lock-free design pass; the CLAP fixture `tests/data/clap-test-plugin/clap-test-gain.c` state
block is now version 3 (128 bytes) and any future test mirroring it must match.