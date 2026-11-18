# Plugins batch-05

Lane: plugin hosts and plugins. Branch `bugs/bunny-plugins-2026-10-05`.

Verdicts: 1 FIXED, 2 ALREADY-FIXED, 1 REAL-UNTESTED, 1 SKIPPED-SCOPE, 1 FALSE-POSITIVE.

### Cluster 4684 — MIDI sent to an instrument with no note port is missing from drop counts

Verdict: REAL-UNTESTED

Evidence: `plugins/ClapInstrument/ClapInstrument.cpp:232` — `handleMidiEvent()`
opens with `if (!m_plugin.acceptsNotes()) { return true; }`, so the event never reaches
`m_plugin.setNoteOn()`/`setNoteOff()`. The refusal-and-count contract lives one level
down: `HostedPlugin::pushNote()` (`plugins/ClapEffect/ClapHostNotes.cpp:112-123`) is the
single insertion point that increments `impl.notesDropped` and calls
`control::clapHostNoteCounters().recordDrop()` when `!acceptsNotes()`, and
`plugins/ClapEffect/ClapHost.h:191-192` documents that refusal as counted. The
instrument's early return therefore hides a drop the host is supposed to report, and
`plugin.host_notes` under-reports `dropped`. `modulateNote()` (`:341`) has the same
guard, but per-note param_modulation on a plug-in with no note port is not a dropped
*note*, so that one is not part of this defect.

Not fixed: the defect is real and the fix is small (let the event reach `pushNote`,
which already counts the refusal), but no existing test compiles
`plugins/ClapInstrument/ClapInstrument.cpp` — confirmed with
`rg -n -l 'ClapInstrument\.cpp|ClapInstrument::' tests src plugins/ClapEffect`, which
matches only `src/core/CMakeLists.txt`, `src/core/ControlReversibilityTableClapInstrument.cpp`
and the source manifests. The two tests that exercise a CLAP instrument
(`ControlDeviceCatalogueTest`, `InstrumentLayersCommandsTest`) load the module through
the plugin factory; both use fixtures that *do* declare a note port, so
`acceptsNotes()` is true and the refusal path is unreachable from them. Building a new
target that instantiates `ClapInstrument` (needing an `InstrumentTrack`, a live
engine, and a fixture instrument with no note port) is well past this batch's
time budget, and per the batch rules a finding without a deterministic headless RED
test must not change product code.

Commit: none

Test: none

Red/green: none

### Cluster 5550 — Instrument processing ignores plug-in errors and leaves output untouched

Verdict: REAL-UNTESTED

Evidence: `plugins/ClapInstrument/ClapInstrument.cpp:315` — `processImpl()` discards
the `bool` from `m_plugin.process(in.data(), out.data(), ..., frames)` and returns
`lmms::ProcessStatus::Continue` unconditionally. The host contract
(`plugins/ClapEffect/ClapHost.h:244`) is that `CLAP_PROCESS_ERROR` stops the request,
leaves the remaining frames untouched, and returns false "so the caller can fall back
to dry audio for the whole block it asked for". The unprepared path above it
(`:295-303`) has the same shape: it sets `m_needsReprepare` and returns `Continue`
without writing anything into `out`. The effect host already does the right thing —
`ClapEffectIntegrationTest::testAProcessingErrorFallsBackToDryAudio` drives
`CLAP_TEST_GAIN_PROCESS_ERROR` and expects the dry signal — so this is an instrument
asymmetry, not a design decision.

Not fixed: same blocker as Cluster 4684 — no test target compiles
`ClapInstrument.cpp`, and the CLAP instrument fixture
(`tests/data/clap-test-plugin/clap-test-instrument.c`) has no process-error
environment switch (unlike `clap-test-gain.c`, which the effect test uses), so there
is no way to make a `CLAP_PROCESS_ERROR` happen deterministically against an instrument
without adding both a fixture switch and a new test target. That is new test
infrastructure, not a behaviour-preserving fix, so the product code is left alone.

Commit: none

Test: none

Red/green: none

### Cluster 4689 — Read-only CLAP parameters are presented as editable

Verdict: FALSE-POSITIVE

Evidence: both halves of the claim are already handled in the current tree.
`fillClapKnobGrid()` (`plugins/ClapEffect/ClapKnobGrid.h:58-65`) skips
`descriptor.hidden || descriptor.readOnly` with the comment "A read-only parameter
ignores every write (setParamPlain drops it), so an editable knob would only move and
snap back on the next poll - it is not offered at all"; that grid is what
`ClapInstrumentView.cpp:56,60` builds, so a read-only parameter gets no knob. The
model-change connection cannot smuggle a write past the host either:
`HostedPlugin::setParamPlain()` returns early on
`impl.params[index].readOnly` (`plugins/ClapEffect/ClapHostParams.cpp:214-218`), and
`setParamNormalized()` routes through it. Host-originated updates are additionally
guarded by `m_syncing` in `ClapParamModelList::pullValues()`
(`plugins/ClapEffect/ClapParameter.cpp:144-156`), whose `dataChanged` only writes when
`!m_syncing`. The behaviour is covered by
`ClapEffectIntegrationTest::testReadOnlyParametersGetNoKnob`
(`tests/src/plugins/ClapEffectIntegrationTest.cpp:627`), which passes on this branch.
The report's line numbers come from revision 959d30852 and predate this code.

Commit: none

Test: none (existing `ClapEffectIntegrationTest::testReadOnlyParametersGetNoKnob`)

Red/green: none

### Cluster 2701 — Changing the compressor key filter races with audio processing

Verdict: ALREADY-FIXED

Evidence: the key-filter coefficients are no longer written by the GUI thread.
`CompressorEffect::calcKeyFilter()` (`plugins/Compressor/Compressor.cpp:246-250`)
designs the filter on whichever thread moved the knob and publishes it through
`m_keyMailbox`, a sequence-lock mailbox whose comment states the intent: "Designed
here, on whichever thread moved the knob, and adopted by the audio thread between
blocks (CompressorKeyFilter.h): nothing it is running is rewritten under it."
`processImpl()` calls `m_keyFilter.follow(m_keyMailbox)` once at the top of every
block (`plugins/Compressor/Compressor.cpp:263`), before any frame is read, so the
audio thread swaps filter state only at a block boundary and only from a published
immutable snapshot. `plugins/Compressor/CompressorKeyFilter.h` provides the three
pieces this needs (`designKeyFilter()`, `KeyFilterMailbox`, the audio-thread-owned
`KeyFilter`). The finding describes `DirectConnection` slots updating coefficients,
the `on` flag and filter delay state directly — none of which is in the current code.
The report's line numbers come from revision e5a603132, which predates it.

Commit: none

Test: none

Red/green: none

### Cluster 5567 — External sidechain bypasses the compressor's tilt filter

Verdict: FIXED

Evidence: `calcTiltFilter()` was applied only to the channel signal `s`
(`plugins/Compressor/Compressor.cpp:303-307`, pre-fix), but the detector picks its
input afterwards (`:326-333`): when `sidechainBuffer()` is non-null it reads the raw
`sc0[f]`/`sc1[f]`, never the tilted `s`. Since the wet path rebuilds `s` from a fresh
`drySignal` (`:489-498`), tilt had no audible effect at all in that configuration —
the control was inert exactly where its own description ("bias the sidechain to the
low or high frequencies") says it should act. The filter now runs on whichever signal
the detector listens to: the channel signal when there is no sidechain, the sidechain
sample otherwise, so the one filter state per channel is still updated exactly once
per frame.

Commit: f3778ede2

Test: `PhaseDSidechainTest::tiltAlsoBiasesAnExternalSidechainKey`

Red/green:
- Red: `cmake --build build --target zene PhaseDSidechainTest` EXIT=0;
  `cd build/tests && ctest -R '^PhaseDSidechainTest$' --output-on-failure` EXIT=8 —
  "Tilt did not bias the external sidechain key: flat 0.0243309, low-biased
  0.0243309" (7 passed, 1 failed).
- Green: same build EXIT=0; same ctest EXIT=0 (8 passed, 0 failed).
- Related tests (`rg -l compressor tests/src` → `PhaseDSidechainTest`,
  `RealtimeSnapshotTest`, `PluginPortsMigrationTest`):
  `cmake --build build --target RealtimeSnapshotTest PluginPortsMigrationTest` EXIT=0;
  `cd build/tests && ctest -R '^(RealtimeSnapshotTest|PluginPortsMigrationTest)$'`
  EXIT=0 (2/2 passed).
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0;
  `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 409 — FFTW planning and destruction can race audio processing

Verdict: SKIPPED-SCOPE

Evidence: the race is real as described — `ConvolutionKernel`'s constructor calls
`fftwf_plan_dft_r2c_1d`/`fftwf_plan_dft_c2r_1d`
(`plugins/ConvolutionReverb/ConvolutionKernel.cpp:40-42`) and its destructor calls
`fftwf_destroy_plan` (`:57-58`), while `fftwf_execute` runs per block from
`convolveChannel` (`:84`) on the audio thread; kernels are built on the control thread
in `loadImpulse()` (`plugins/ConvolutionReverb/ConvolutionReverb.cpp:198`). But the
remediation the finding itself offers — "create and destroy FFTW plans only while all
FFTW audio execution is quiescent, or a design that safely coordinates plan lifetime
globally" — is a global coordination design across every convolution instance, not a
behaviour-preserving local fix: it needs a process-wide execution guard or
`fftwf_make_planner_thread_safe`-style policy, i.e. a design change (rules 6 and 7).
Note the per-instance part is already handled: `install()`
(`plugins/ConvolutionReverb/ConvolutionReverb.cpp:167-174`) swaps and frees the old
kernel between `requestChangeInModel()`/`doneChangeInModel()`, so a block in flight is
never executing a plan that is being destroyed. What remains is only the
plan-lifetime-wide part, which is out of scope here.

Commit: none

Test: none

Red/green: none

Leftovers: Clusters 4684 and 5550 are confirmed real against the current source and
still unfixed — both are one-line-class fixes inside
`plugins/ClapInstrument/ClapInstrument.cpp`, but both need test infrastructure that
does not exist: a test target that compiles `ClapInstrument.cpp` (with an
`InstrumentTrack` and a live engine), plus a process-error switch in
`tests/data/clap-test-plugin/clap-test-instrument.c` for 5550 and a note-port-less
instrument fixture for 4684. That is a self-contained follow-up: add the fixture
switches, register a `ClapInstrumentIntegrationTest` in `tests/CMakeLists.txt` and
`tests/fork-sources.txt`, then fix `handleMidiEvent()` and `processImpl()`.