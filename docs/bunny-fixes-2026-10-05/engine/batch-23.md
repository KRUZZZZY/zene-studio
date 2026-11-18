# engine batch-23 — 2026-10-05

Clusters 3705, 5471, 5464, 3711, 4598, 5463.

Nothing fixed. One is ALREADY-FIXED on this branch, one is a FALSE-POSITIVE
against the current code, one is SKIPPED-SCOPE on the realtime rule, and three
are REAL defects whose deterministic regression needs a backend this lane
cannot drive headlessly (a live JACK server, a JACK restart, or PortAudio).

### Cluster 3705 — Stereo and wide capture can be drained from different blocks
Verdict: SKIPPED-SCOPE
Evidence: `src/core/audio/AudioDevice.cpp:76` pushes the wide block and
`:91` pushes the stereo block, and the render thread drains them from two
independent rings (`AudioEngine::pushInputFrames` writes `m_inputStage`,
`pushInputFramesWide` writes `m_wideInputStage`, `src/core/AudioEngine.cpp:238`
and `:309`). The window is real, but the proposed fix — publishing both as one
synchronised block, or pairing them with a shared sequence number the drain
must respect — needs cross-ring state the render thread has to consult before
consuming either. That is exactly the synchronisation the realtime rule forbids
on a process/render path, so no product change was made.
Commit: none
Test: none
Red/green: none

### Cluster 5471 — Noise-shaped output ignores the dither enable setting
Verdict: FALSE-POSITIVE
Evidence: `include/OutputSettings.h:133` derives the enable flag from the mode
itself — `bool dither() const { return m_ditherMode != DitherMode::Off; }`.
The two are the same state, not two independent ones, so the condition the
report asks for ("dither disabled but the selected mode remains NoiseShaped")
is unrepresentable: with `m_ditherMode == NoiseShaped`, `dither()` is
necessarily true. The 16-bit branch at `src/core/audio/AudioFileWave.cpp:153`
testing `ditherMode() == DitherMode::NoiseShaped` is therefore already gated on
the enable setting, and `ditherBitsFor()` (`:47-57`) applies the same
derivation to the TPDF paths. Note also that the 24/32-bit branch at `:143`
uses `ditherBits > 0`, not a raw `dither()` call, and that is correct for the
same reason.
Commit: none
Test: existing coverage: `ExportWavDitherTest::aDefaultExportIsExactlyTheUnditheredQuantisation`
Red/green: none (no code changed)

### Cluster 5464 — JACK capture is fixed at two channels
Verdict: ALREADY-FIXED
Evidence: `src/core/audio/AudioJack.cpp:199` sets
`m_inputChannels = AudioInputPath::configuredChannelCount()` — not
`channels()` — and the input-port loop at `:229` is bounded by
`m_inputChannels`, while the output loop at `:218` is bounded by `channels()`,
which is the separate stereo output count (`:198`, from
`audioio::requestedOutputs`). The capture buffer follows it too
(`resizeInputBuffer`, `:250`, sizes `m_captureWide` by `m_inputChannels`), and
the published live channel count is `m_inputPorts.size()` (`:311`), i.e. the
number actually registered. The `int m_inputChannels = DEFAULT_CHANNELS;`
initialiser at `include/AudioJack.h:138` is overwritten before first use.
Fixed on this branch by `009f591b2`, not by this batch.
Commit: none (pre-existing, `009f591b2`)
Test: `tests/control-jack-capture.py` (needs a live `jackd`)
Red/green: none

### Cluster 3711 — Restarting a zombified JACK client reuses stale port pointers
Verdict: REAL-UNTESTED
Evidence: `src/core/audio/AudioJack.cpp:477-482` `shutdownCallback()` nulls
`m_client` but leaves `m_inputPorts` / `m_outputPorts` populated;
`initJackClient()` then `push_back`s onto both (`:221`, `:232`) without
clearing, so a successful `restartAfterZombified()` (`:138-141`) yields lists
holding the dead client's ports at the front while `processCallback` indexes
from index 0 (`:407`, `:455`). No `clear()`/assignment of either vector
exists anywhere in the file. The defect is real; a deterministic regression
must kill a JACK server mid-run, restart it and observe which ports the
process callback reads, which this lane cannot do headlessly — `jackd` is
installed, but driving a zombification and a client restart is not a
deterministic unit test, and the fix itself (clearing both vectors before
registration) touches no product code this batch may change untested.
Commit: none
Test: none
Red/green: none

### Cluster 4598 — JACK shutdown can leave capture status falsely open
Verdict: REAL-UNTESTED
Evidence: `src/core/audio/AudioJack.cpp:480` nulls `m_client` and emits
`zombified()` without any `AudioInputPath` call, and the failure branch of
`restartAfterZombified()` (`:150-158`) only shows a message box. The last
`publishOpen` came from `startProcessingImpl` (`:311`), so
`AudioInputPath::live().open` stays true and `recordableChannelCount()`
(`include/AudioInputPath.h:176`) keeps reporting the old device's channels
after the client is gone. Real, and consistent with the contrast: ALSA
publishes `publishUnavailable` on every open failure and `publishClosed` at
`src/core/audio/AudioAlsa.cpp:481`, and JACK does publish closed in its
destructor (`:129`) but not on shutdown. As with 3711 the only regression that
proves it requires a real JACK shutdown/restart cycle.
Commit: none
Test: none
Red/green: none

### Cluster 5463 — PortAudio capture is absent from the input path’s live state
Verdict: REAL-UNTESTED
Evidence: `src/core/audio/AudioPortAudio.cpp:175-179` sets
`m_inputChannels` and `m_supportsCapture` and calls `prepareCapture()`, but
never `AudioInputPath::setCaptureCapable()` or `publishOpen()` — unlike JACK
(`src/core/audio/AudioJack.cpp:310-312`) and ALSA
(`src/core/audio/AudioAlsa.cpp:320`, `:455`). `recordableChannelCount()`
therefore keeps its no-device default of
`AudioInputPath::DefaultChannels` (2, `include/AudioInputPath.h:96`), so a
route selecting a channel above 1 is refused even though
`processCallback` (`:226-229`) hands the wide block to the N-channel path. The
defect is real but this build has PortAudio disabled —
`build/lmmsconfig.h:31` is `/* #undef LMMS_HAVE_PORTAUDIO */` — so the whole
file is `#ifdef`-ed out, and there is no hardware-free way to open a
PortAudio input here. Left unverified rather than changed blind.
Commit: none
Test: none
Red/green: none

Leftovers: three confirmed real defects are unfixed because none has a
deterministic headless regression — 3711 and 4598 need a live JACK
shutdown/restart cycle, and 5463 needs a PortAudio build with an input device
(`LMMS_HAVE_PORTAUDIO` is undefined in this tree). All three are ordinary
correctness fixes, not realtime work, so they are worth re-queuing against a
machine that can drive the backend. 3705 stays open by design: its fix needs
cross-ring synchronisation on the render path, which the realtime rule forbids.