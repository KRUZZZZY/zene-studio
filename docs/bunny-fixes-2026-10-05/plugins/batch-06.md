# Plugins batch-06

Lane: plugin hosts and plugins. Branch `bugs/bunny-plugins-2026-10-05`.

Verdicts: 1 FIXED, 1 REAL-UNTESTED, 3 SKIPPED-SCOPE, 1 FALSE-POSITIVE.

### Cluster 4109 — A missing saved impulse silently changes the effect to the generated hall

Verdict: SKIPPED-SCOPE

Evidence: `plugins/ConvolutionReverb/ConvolutionReverb.cpp:150` —
`ConvolutionReverbControls::loadSettings()` calls `m_effect->loadImpulse(path)` and
drops the returned `bool`. The behaviour described is real: a fresh effect has already
installed the generated hall (`ConvolutionReverb.cpp:159`), and `loadImpulse()` returns
false at `:191` without touching `m_impulsePath` or the kernel, so a moved or
undecodable saved impulse leaves the hall sounding with no indication. The header
states that fallback as the contract (`ConvolutionReverb.h:78`, "False, keeping the
current one, when the file cannot be read").

The requested fix — surface the failure during project loading — has no channel to
flow through: `AudioPlugin::loadSettings()` is `final` and returns `void`
(`include/AudioPlugin.h:428-431`), so a project-load failure cannot be reported without
a new mechanism. The interactive path already reports it in full:
`plugin.impulse_load` returns `NotFound` / `InvalidArgs` for a missing or undecodable
file (`src/core/ControlCommandsDeviceTools.cpp:319-323`). Only the project-open path is
silent, and closing it means a new user-visible error surface — a design change
(rules 7 and 8), not a defect fix.

Commit: none

Test: none

Red/green: none

### Cluster 5036 — Sample-rate changes reuse delay state measured at the old rate

Verdict: REAL-UNTESTED

Evidence: `plugins/ConvolutionReverb/ConvolutionReverb.cpp:162` — the
`sampleRateChanged` handler calls `loadImpulse(m_impulsePath)`, and that path only
reaches `install()` (`:167-175`), which swaps the kernel under
`requestChangeInModel()`/`doneChangeInModel()` and nothing else. The pre-delay rings
(`ConvolutionReverb.h:100-101`), the queued input/output/dry blocks (`:102-104`) and the
two filter sections (`:105-106`) keep their old-rate contents, while
`predelayFrames()` (`:202-205`) recomputes the read offset from milliseconds at the new
rate — so up to `MaxPredelayMs` of audio written at 48 kHz is read back as a 96 kHz
delay. `designFilters()` (`:213-223`) does notice the new rate and re-designs, but
`BiquadSection::set()` (`include/BiquadDesign.h:104`) replaces coefficients only and
leaves `z1`/`z2` from the old-rate history.

Not fixed: the defect is real and clearing the rate-dependent state in the handler is
small, but no deterministic headless test can drive it. `AudioEngine::outputSampleRate()`
returns the live device's rate (`include/AudioEngine.h:239-241`) and the signal is only
emitted from device setup/teardown (`src/core/AudioEngine.cpp:209`, `:680`, `:707`); the
test audio device is a dummy with a fixed rate, and `Engine::audioEngine()->setAudioDevice()`
would require constructing a second device mid-test. Per the batch rules a finding with
no deterministic headless RED must not change product code.

Commit: none

Test: none

Red/green: none

### Cluster 4140 — De-esser cutoff can exceed Nyquist

Verdict: FALSE-POSITIVE

Evidence: `include/BiquadDesign.h:63` — `designBiquad()` clamps the requested cutoff
before designing anything: `std::clamp(hz, 1.0, 0.45 * sampleRate)`. At a 22.05 kHz
output rate a 16 kHz request is designed at 9922.5 Hz, a well-formed 0.707-Q section.
The claimed harm — an invalid or aliased detector and split filter — cannot occur: both
`m_detector` and `m_low` (and the split's subtraction in `shaped()`) come from that one
clamped function (`plugins/DeEsser/DeEsser.cpp:89-90` pre-fix). What remains is that the
Frequency control's 16 kHz maximum is wider than what a low-rate engine can use, so the
displayed value may differ from the designed cutoff; that is a parameter-range choice
for the product, not a defect, and changing it would alter the control's saved values.

Commit: none

Test: none

Red/green: none

### Cluster 5030 — The de-esser detector cancels opposite-phase stereo content

Verdict: FIXED

Evidence: `plugins/DeEsser/DeEsser.cpp:121` (pre-fix) — the detector ran on
`std::abs(m_detector.run(0.5f * (frame[0] + frame[1]), 0))`, a mono sum, so sibilance
that is loud in both channels but opposite in phase reads as near silence and nothing is
turned down. The detector is now linked: both channels are high-passed and the louder
band is taken, and `m_detector` widened from `BiquadSection<1>` to `BiquadSection<2>`
(`plugins/DeEsser/DeEsser.h:89`) to hold one state per channel. The filters remain
fixed-size data, so a block still allocates nothing and the existing realtime snapshot
test stays green.

Commit: e09a0d995

Test: DeviceDspTest::theDeEsserDetectsOppositePhaseSibilance

Red/green: `cmake --build build --target zene deesser DeviceDspTest` EXIT=0;
`cd build/tests && ctest -R '^DeviceDspTest$' --output-on-failure` EXIT=8 —
"opposite-phase sibilance was not detected: left peak 0.499743 (0.5 is untouched, 0.25
is the 6 dB the case above gets)". The `deesser` module had to be built first: before
it existed the case reported SKIP ("this build has no de-esser module"), which rule 3
does not accept as a pass. Green: same build EXIT=0, same ctest EXIT=0 (4 passed,
0 failed; both de-esser cases run, the 7 remaining skips are modules this build tree
has not built). `ctest -R '^RealtimeSnapshotTest$'` EXIT=0 with 9 passed, 0 skipped.
Both gates EXIT=0.

### Cluster 2258 — Sample-rate changes resize buffers during audio processing

Verdict: SKIPPED-SCOPE

Evidence: `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:257-261` —
`processImpl()` calls `changeSampleRate()` from the audio callback when
`m_sampleRateNeedsUpdate` is set, and that function resizes and zero-fills the ring
buffer (`:274-280`), resets the write point and glides (`:281-285`) and clears then
re-reserves the grain vector (`:287-289`). That is an allocation plus work proportional
to the ring length inside `process()`, which the module's own comment claims it avoids
(`:67-68`).

Not fixed: the finding is real, but the requested repair — prepare the buffers off the
audio thread and publish them safely — requires a prepared-state handoff (a second
ring, an atomic pointer swap and a lifetime rule for the in-flight block). That is an
architectural change well past a behaviour-preserving defect fix (rules 6 and 7), and it
has no deterministic headless test here: proving "no allocation in the audio callback"
would need the allocation probe armed across a live device rate change.

Commit: none

Test: none

Red/green: none

### Cluster 4179 — LADSPA processing still takes a mutex on the audio path

Verdict: SKIPPED-SCOPE

Evidence: `plugins/LadspaEffect/LadspaEffect.cpp:239-241` — `processLock()` calls
`m_pluginMutex.tryLock(Engine::getSong()->isExporting() ? -1 : 0)`, so every block takes
the mutex (a non-blocking `tryLock` during playback, an unbounded wait while exporting),
as described. The mutex is not incidental: it guards plugin reconfiguration, which
destroys and re-instantiates every LADSPA processor and its ports
(`LadspaEffect.cpp:113-118`, inside `changeSampleRate()`).

Not fixed: transferring that state without the mutex in the audio callback means
replacing mutual exclusion with a lock-free publication scheme for the port and control
state — a design change to the host's reconfiguration contract (rules 6 and 7), not a
local defect fix, and it cannot be regression-tested headlessly because it needs a live
LADSPA device and contention that a deterministic test will not reproduce.

Commit: none

Test: none

Red/green: none

Leftovers: Cluster 5036 (ConvolutionReverb keeps old-rate pre-delay, queued-block and
filter state across `sampleRateChanged`) is confirmed real against the current source
and still unfixed — clearing the pre-delay rings, the `m_in`/`m_out`/`m_dry` blocks and
the two filter sections in the `:162` handler would be a small fix, but it needs a test
that changes the engine's output rate, and no headless path exists today (the test
device is an `AudioDummy` with a fixed rate). Cluster 4109 needs a decision from the
parent on where a project-load failure is surfaced, since `AudioPlugin::loadSettings()`
is `final` and `void`; the interactive `plugin.impulse_load` path already reports it.
Note for whoever picks these up: this build tree had no plugin modules built, so
`DeviceDspTest` skips every native-device case until the module targets are built —
build the plugin target, not just `zene`, or a skip will read as a pass.