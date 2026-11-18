# Plugins batch-07 — Luna clusters 4297, 4392, 4391, 5286, 4467, 4468

Branch `bugs/bunny-plugins-2026-10-05`. Six findings, one fixed, five left as
SKIPPED-SCOPE or REAL-UNTESTED with evidence. No product audio/DSP code was
changed: every remaining finding's proposed fix is a design change or a
redesign of an existing mechanism, which this lane may not make.

### Cluster 4297 — Starting a second Neural Amp load can freeze the GUI

Verdict: SKIPPED-SCOPE

Evidence: `plugins/NeuralAmp/NeuralAmpEffect.cpp:105-109` — `setModelPath()`
joins `m_loadThread` before starting the next worker, and
`plugins/NeuralAmp/NeuralAmpControlDialog.cpp:108` calls
`m_controls->setModelPath(path)` from the dialog's `loadModel()`. The blocking
join is real and is reached on the GUI thread.

The join is not incidental, though: it is what keeps two workers off the
published model pointer, and `publishModel()` (`NeuralAmpEffect.cpp:146-164`)
already depends on exactly one loader existing at a time — it exchanges the
pointer and then spins on `m_audioActive` before freeing the previous model.
Making the old load *cancellable or supersedable* means adding cancellation
into `nam::NamModel::loadFromFile` and a generation counter that discards a
stale worker's result before it publishes. That is a redesign of the load
lifecycle in vendored `nam/` code plus a per-request cancellation path, well
past "change only what the defect needs" and past the ~150-line limit. There
is also no deterministic headless test for it: the failure is a wall-clock
stall proportional to model size, so a regression test would have to assert a
timing bound on a fixture whose parse time varies by machine.

Commit: none

Test: none

Red/green: none

### Cluster 4392 — RNNoise processes audio at unsupported project sample rates

Verdict: SKIPPED-SCOPE

Evidence: `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:103-142` — the
loop never reads the engine rate; `rnnoise_process_frame()` is called every 480
accumulated samples regardless of `baseSampleRate()`, and a bounded search
(`rg -n "baseSampleRate|audioEngine\(\)->" plugins/RnnoiseDenoiser/`) finds no
rate handling anywhere in the plugin. The frame size is fixed by the vendored
library at `plugins/RnnoiseDenoiser/rnnoise/denoise.h:31`
(`#define FRAME_SIZE 480`), so at 44.1 kHz the plugin treats 10.9 ms as 10 ms.

Both remedies the finding offers are out of scope here. Resampling to and from
48 kHz needs a resampler instance, its state and a ratio change path in
`processImpl()` — a new DSP component on the audio path, not a bug fix.
Refusing to process at unsupported rates needs a visible error surface, which
means a new control/parameter (this plugin has no status or error control today
— `RnnoiseDenoiserControls` exposes only the wet/dry model), and adding one
moves the ControlCommandsSnapshot canary, which is the parent's job under
rule 8.

Commit: none

Test: none

Red/green: none

### Cluster 4391 — RNNoise collapses stereo audio to mono

Verdict: SKIPPED-SCOPE

Evidence: `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:107` averages the
two channels into `monoIn`, and lines 127-128 write the single denoised value
to both channels. The downmix is deliberate and commented
(`RnnoiseDenoiserEffect.cpp:105-106`, and the header's own note that the dry
path keeps stereo), and the vendored RNNoise is a single-channel model: one
`DenoiseState*` at `RnnoiseDenoiserEffect.h:62` and one `rnnoise_process_frame()`
call per frame.

"Process channels independently with separate states" is the substantive
remedy, and it doubles the neural inference cost per frame and changes audible
output for every project using the effect. That is a product/performance
decision, not a defect fix, and it cannot be made behaviour-preserving. The
finding's own alternative — "a documented stereo-preserving strategy" — is
arguably what the existing code already is.

Commit: none

Test: none

Red/green: none

### Cluster 5286 — Harness crashes when the optional stats file cannot be opened

Verdict: FIXED

Evidence: `plugins/RnnoiseDenoiser/testdata/rnn_harness.c:77` (pre-fix) read
`fs = fopen(argv[4], "w"); fprintf(fs, ...)` with no check, so an unwritable
stats path dereferenced a null `FILE *`. Reproduced as a hard segfault
(`Segmentation fault (core dumped)`, EXIT=139), not a soft error — every other
file open in the same `main()` (lines 61, 67, 92) already checked and exited 2,
so this was a lone unchecked path.

The open moved into a new `open_stats()` helper that `perror`s and exits 2, the
same shape the existing checks use, and writes the header row only after the
check passes. It is a helper rather than an inline `if` because `main` is at
its ratcheted CCN 15 (`tests/complexity-baseline.tsv:38`) and an added branch
would have tripped the complexity gate — the gate passes with CCN unchanged.

Commit: 19a9e2a03

Test: none registered — `rnn_harness.c` is a standalone test artifact, not
built by the project (`tests/QA-GATES.md:848` records it as "1, not built"), so
it has no ctest case to extend. The regression is driven with the harness's
own documented gcc build from
`plugins/RnnoiseDenoiser/testdata/run_harness.sh`.

Red/green:
- red: `gcc -O2 -I plugins/RnnoiseDenoiser/rnnoise -o /tmp/h
  plugins/RnnoiseDenoiser/testdata/rnn_harness.c <the 10 rnnoise .c files> -lm`
  then `/tmp/h in.f32 out.f32 32768 /nonexistent-dir/stats.csv` →
  `Segmentation fault (core dumped)`, EXIT=139
- green: same build after the fix → `stats: No such file or directory`,
  EXIT=2; a writable stats path → EXIT=0 with the header row and 2 frame rows;
  the stats argument omitted → EXIT=0 with byte-identical audio output
- gates: `bash tests/complexity-gate.sh --check` EXIT=0,
  `bash tests/file-length-gate.sh --check` EXIT=0

### Cluster 4467 — SlicerT allocates playback state in the audio callback

Verdict: SKIPPED-SCOPE

Evidence: `plugins/SlicerT/SlicerT.cpp:111` does
`if (!handle->m_pluginData) { handle->m_pluginData = new Sample::PlaybackState(...); }`
inside `playNoteImpl()`, which the audio engine reaches through
`InstrumentPlayHandle::play()` (`src/core/InstrumentPlayHandle.cpp:44,66`) →
`Instrument::play()` → `playNoteImpl`. So the finding's core claim — the first
note of a slice allocates on the audio path — is accurate.

The allocation is one small object per note handle, and it is the established
pattern across this codebase's instruments, not a SlicerT slip: the same
lazy-create in a `playNote*` path appears in `plugins/Monstro/Monstro.cpp:1066-1068`,
`plugins/FreeBoy/FreeBoy.cpp:241`, `plugins/Sfxr/Sfxr.cpp:478-480`,
`plugins/Sid/SidInstrument.cpp:289-296` and `plugins/Vibed/Vibed.cpp:206-211`,
each paired with its own `deleteNotePluginData`. Moving it off the audio path
needs a pre-play hook on `Instrument` (none exists — `include/Instrument.h`
offers `playNote`/`deleteNotePluginData` and no `noteOn`-style preparation
callback), i.e. a core API change affecting every instrument above, which is
outside this lane's plugin scope and beyond the line limit.

Commit: none

Test: none

Red/green: none

### Cluster 4468 — SlicerT emits Qt signals from the audio path

Verdict: SKIPPED-SCOPE

Evidence: `plugins/SlicerT/SlicerT.cpp:106,135,137` emit `isPlaying` from
`playNoteImpl()`, i.e. on the audio path reached as in cluster 4467. There is a
live receiver: `plugins/SlicerT/SlicerTWaveform.cpp:89` connects it
(so the finding's "play a note while a GUI receiver is connected" precondition
holds).

The fix the finding proposes — "publish the status through a bounded lock-free
handoff and let the GUI poll or consume it off the audio thread" — replaces the
signal with a hand-rolled SPSC handoff plus a GUI-side timer poller, touching
`SlicerT.h`, `SlicerT.cpp`, `SlicerTWaveform.h` and `SlicerTWaveform.cpp`.
That is well past the line budget and is an interface redesign, not a
behaviour-preserving fix. Note also that the default `Qt::AutoConnection`
between a GUI-thread receiver and this emission already degrades to a queued
connection, so the cost is one event post rather than a cross-thread direct
call — the risk is real but smaller than a blocking GUI call.

Commit: none

Test: none

Red/green: none

Leftovers: nothing half-done. Cluster 5286 is fixed, committed and green; the
other five are untouched source with the evidence above recorded. Two of them
(4392 rate handling, 4391 stereo) would need product decisions rather than
lane fixes, and cluster 4467 needs a core `Instrument` pre-play hook — worth
re-routing to the program rather than re-queueing to a plugin lane.