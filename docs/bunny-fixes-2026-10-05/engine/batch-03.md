# Engine lane — batch 03 (2026-10-05)

Branch `bugs/bunny-engine-2026-10-05`. Six hypotheses: four REAL (three fixed and
committed, one left untested), two FALSE-POSITIVE. No finding was skipped for
scope and no gate, tolerance or assertion was weakened.

### Cluster 5521 — Extreme stretch positions can cause undefined behavior

Verdict: FIXED

Evidence: `src/core/AudioStretcher.cpp:55` (pre-fix) cast the fractional read
position to `std::int64_t` *before* comparing it to the source length, so a
position of `1e300` (from `seek()`, or from the `m_sourcePos += hop * speed`
advance at `src/core/AudioStretcher.cpp:183` overflowing on a huge finite speed)
performed an out-of-range floating-point to integer conversion — undefined
behaviour — and then indexed the source with the garbage result. The guard now
runs on the double, before the conversion, at `src/core/AudioStretcher.cpp:57`:
an out-of-source or NaN position reads as silence. Fix is O(1), allocation-free,
and on the render path only changes a case that was previously undefined.

Commit: 9d3a9f74f

Test: AudioStretcherTest::extremePositionsProduceSilenceInsteadOfOutOfBoundsReads

Red/green: `cmake --build build --target zene AudioStretcherTest` EXIT=0, then
`build/tests/AudioStretcherTest extremePositionsProduceSilenceInsteadOfOutOfBoundsReads theFirstHopReproducesTheStartOfTheSample aSilentOverlapLeavesTheGrainStartAtTheCursor`
EXIT=3 — `FAIL! ... 'allFinite(out)' returned FALSE` at
`tests/src/core/AudioStretcherTest.cpp:491`. After the fix,
`cd build/tests && ctest -R '^AudioStretcherTest$' --output-on-failure` EXIT=0
(11 passed / 0 failed).

### Cluster 5519 — WSOLA fades the start of every stretched sample

Verdict: FIXED

Evidence: `src/core/AudioStretcher.cpp:97-102` built one periodic Hann table used
by every hop, but the first hop is emitted with no preceding grain to sum its
rising half against — so only that half reached the output and a stretched
sample faded in from zero over half a grain instead of starting on its onset. The
first hop now uses a second table, flat (`1.0`) over the emitted half and the
ordinary Hann over the carried half
(`src/core/AudioStretcher.cpp:102`, selected at `src/core/AudioStretcher.cpp:191`).
The overlap-add after the first hop is unchanged: `w[i] + w[i+hop] == 1` still
holds, so a stretch of 1 still reconstructs the waveform. Both tables are built
once in `prepare()` — the render path gains a reference selection, no branch per
sample, and no allocation.

Commit: 7f3350b0b (plus 058434540, below)

Test: AudioStretcherTest::theFirstHopReproducesTheStartOfTheSample

Red/green: same combined red run as 5521, EXIT=3 —
`FAIL! ... (output frame 0 is 0, the onset should be 1.0)` at
`tests/src/core/AudioStretcherTest.cpp:517`. After the fix, `ctest -R
'^AudioStretcherTest$'` EXIT=0.

Ratchet follow-up, commit 058434540: the added window choice pushed
`produceHop` to CCN 11, over the complexity gate (`complexity-gate.sh --check`
EXIT=1, `REGRESSION: new function over target: lmms::AudioStretcher::produceHop
(CCN 11)`). The windowed overlap-add was therefore moved into its own private
helper, `AudioStretcher::overlapAddGrain`, rather than growing the hop function.
Behaviour is identical; both gates then returned EXIT=0. Test unchanged.

### Cluster 5520 — Silent WSOLA overlap biases grain starts by the full search radius

Verdict: REAL-UNTESTED

Evidence: the tie-breaking behaviour described is real and still present in the
code. `src/core/AudioStretcher.cpp:145` seeds `bestScore` with `-infinity`, the
sweep at `src/core/AudioStretcher.cpp:146` runs from `-searchRadius` upward, and
the replacement test at `src/core/AudioStretcher.cpp:161` is a strict `>`, so
ties keep the first candidate. When the carry region is silent every candidate
scores exactly zero and `bestOffset` stays at `-searchRadius`, biasing the grain
start early by the full radius. I prototyped the documented fix (skip the search
when the carry's energy is negligible and use offset zero) and confirmed the
guard did not change the outcome of a scenario I could construct, which means the
symptom I could reproduce was a different effect: the trailing window edge of a
hard DC step under legitimate ±radius alignment, not the silent-carry bias. I
could not build a deterministic, isolated headless test for the bias itself, so
per the batch procedure I reverted the prototype and left the product code alone.
The tie-breaking lines above are cited so the next lane can target them with a
scenario that isolates the silent carry (e.g. a silence-to-signal boundary where
the correct offset is provably zero and a shifted grain is audibly detectable in
the emitted frames rather than at a window edge).

Commit: none

Test: none (the candidate regression case did not isolate the defect and was
discarded with the prototype)

Red/green: none that demonstrates this defect specifically. The discarded
candidate, `AudioStretcherTest::aSilentOverlapLeavesTheGrainStartAtTheCursor`,
failed both before and after the prototype fix (EXIT=1,
`FAIL! ... (128 of 512 patch frames are not at unity after a silent overlap)`),
which is what ruled it out as evidence for 5520.

### Cluster 4621 — `sourcePosition()` reports source frames ahead of emitted audio

Verdict: FALSE-POSITIVE

Evidence: `include/AudioStretcher.h:157-159` documents the accessor as the
*analysis* cursor — "Where the analysis cursor stands in source frames: how much
of the source has been consumed so far" — and `include/AudioStretcher.h:196`
documents `m_sourcePos` the same way, so the cursor advancing a whole synthesis
hop when that hop is generated (`src/core/AudioStretcher.cpp:183`) is the
documented contract, not an accident. There is one production caller,
`src/core/SamplePlayHandle.cpp:551-554`, and it depends on exactly this meaning:
the comment above it reads "The analysis cursor IS the source position this
period reached; the next period's rate is taken from the frame it starts on",
and it uses the value to set the play handle's frame index. Changing
`sourcePosition()` to report emitted-output progress would break that caller.
`tests/src/core/AudioStretcherTest.cpp` also encodes the current behaviour
(asserting a hop-granular advance past 1000.0 and cursor stability across an
idempotent `prepare()`). The finding asks for a semantics change that the one
caller in the tree relies on the present semantics for.

Commit: none

Test: none

Red/green: none

### Cluster 5523 — Empty input drains expose stale captured samples

Verdict: FALSE-POSITIVE

Evidence: `src/core/AudioWideInputStage.cpp:103-106` returns early on an empty
drain without clearing the buffer, which is true — but the header promise it is
measured against is scoped to the no-capture-backend case:
`include/AudioWideInputStage.h:69` states "With no capture backend, frames() is 0
and data() is a zeroed buffer", and that holds because both buffers are zeroed in
the constructor (`src/core/AudioWideInputStage.cpp:51`), so a stage that has
never captured always reads as silence. In the alternating-buffers scenario the
finding describes there are real frames in the other buffer, and both consumers
gate on the counts before reading: `src/core/HardwareInsert.cpp:145` selects the
wide buffer only when `inputWideChannels() > 0 && inputWideFrames() > 0`, and
`src/core/InputMonitor.cpp:68-72` returns on `wideFrames <= 0`. No stale sample
reaches audio. Clearing the selected buffer on every idle drain would add a
bounded `memset` of the whole capture buffer to each render period for no
observable benefit, which the realtime rule argues against.

Commit: none

Test: none (covered incidentally by
CapturePublisherTest::theWideStageCarriesTheDevicesWidthAndChangesItOnlyWhenEmpty,
which passes unchanged)

Red/green: none

### Cluster 5576 — Malformed clip gain can put NaN or infinity into playback

Verdict: FIXED

Evidence: `src/core/Clip.cpp:612-615` (pre-fix) passed the `gain` attribute
straight into `gainDbToLinear` (`src/core/ClipEdits.cpp:89-92`,
`pow(10, db/20)`) with no conversion check, so `gain="nan"`, `"inf"` or an
absurd dB value produced a non-finite or unbounded multiplier in
`ClipEdits::gain`, and one NaN sample contaminates the whole rendered buffer.
The loader now follows the clamp-don't-throw policy its neighbouring malformed
attributes already use (comment at `src/core/Clip.cpp:604-607`): an unreadable
or non-finite value loads neutral `1.0`, and a finite out-of-range value is
clamped to the `-60..+24 dB` range that `clip.set_gain` documents
(`src/core/ControlCommandsClipEdits.cpp:70-71`, the same constants as
`MinGainPointDb` / `MaxGainPointDb` in `include/ClipEdits.h:101-102`). Load-time
only; no render-path cost.

Commit: e5deecb57

Test: ClipEditsTest::malformedGainLoadsNeutralOrClampedInsteadOfNan

Red/green: `cmake --build build --target zene ClipEditsTest` EXIT=0, then
`build/tests/ClipEditsTest malformedGainLoadsNeutralOrClampedInsteadOfNan`
EXIT=1 — `FAIL! ... 'std::isfinite(gain)' returned FALSE. (gain="nan" loaded a
non-finite nan)`. After the fix, `cd build/tests && ctest -R
'^(ClipEditsTest|RangeEditsTest|SampleEditorDialogTest)$' --output-on-failure`
EXIT=0 (3/3 passed). Gates: `complexity-gate.sh --check` EXIT=0,
`file-length-gate.sh --check` EXIT=0.

Leftovers: Cluster 5520 is confirmed real but unfixed — the silent-carry
tie-break at `src/core/AudioStretcher.cpp:145-161` still returns
`-searchRadius`; the next lane needs a scenario that isolates the silent carry
from window-edge behaviour. Commit 058434540 exists only to satisfy the
complexity ratchet for the 5519 fix and carries no behaviour change.
