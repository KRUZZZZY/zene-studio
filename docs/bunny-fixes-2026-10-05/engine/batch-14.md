# Engine batch-14

Six engine-lane findings, each adjudicated against the current tree on branch
`bugs/bunny-engine-2026-10-05`.

### Cluster 3501 — Infinite source tempo can produce an invalid clip length
Verdict: FIXED
Evidence: `SampleClip::setSourceTempo` rejected only `bpm < 0.0f`
(`src/core/SampleClip.cpp:385-391` before the fix), so `+Inf` and `NaN` were both accepted as a tempo.
`clipFramesPerTick()` scales the clip's rate by `projectTempo / m_sourceTempo`
(`src/core/SampleClip.cpp:337-350`), so an infinite tempo makes that return exactly `0.0f`, and
`windowTicksFor()` then evaluates `window.length() / clipFramesPerTick()` and converts the resulting
infinity to `int` (`src/core/SampleClip.cpp:325-334`) — undefined behaviour, on the expression
`sampleLength()` and every play handle's length are built from. NaN is the mirror case: every comparison
against it is false, so the `m_sourceTempo > 0.0f` guard at line 340 falls through and the clip silently
plays as a follower. Both were reachable from the control surface: `warp.set` validates only `bpm < 0.0`
(`src/core/ControlCommandsWarpEdit.cpp:116-123`), as does `timeScale`'s derived tempo
(`src/core/ControlCommandsClipArrange.cpp:109-120`). The setter now refuses any non-finite tempo with the
negatives, one condition added to the existing guard.
Commit: 88cf7f620
Test: SampleClipWarpTest::nonFiniteSourceTempoIsRefused
Red/green: red build `cmake --build build --target zene SampleClipWarpTest` EXIT=0 then
`cd build/tests && ctest -R '^SampleClipWarpTest$'` EXIT=8 —
`FAIL! nonFiniteSourceTempoIsRefused() Compared floats are not the same (fuzzy compare)`, failing on the
stored `sourceTempo()` after the `+Inf` set; green build EXIT=0, `ctest -R '^SampleClipWarpTest$'` EXIT=0,
and `ctest -R '^(SampleClipWindowTest|SampleClipStretchTest|SampleClipWarpTest|ClipWarpPersistenceTest|WarpMarkersTest|ControlSampleOperatorTest)$'`
EXIT=0 (6/6; the five other targets were built first — on stale binaries they reported "Not Run", not
failures). Complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 4408 — Invalid sample windows are silently clamped
Verdict: FALSE-POSITIVE
Evidence: `SampleClip::setSampleWindow` (`src/core/SampleClip.cpp:448-470`) does call
`SampleWindow::clamped()` first, but clamping is the documented contract of that helper, not a substitute
for validation: `include/SampleWindow.h:58-69` states the window is clamped into `[0, bufferFrames]` and
that "the caller rejects an empty result rather than clamping an edit into something the user did not
ask for" — and `setSampleWindow` does exactly that, returning untouched for `clamped.empty()` at line 452.
The report's own scenario is the intended behaviour: requesting `[400, 900)` on a 500-frame source installs
`[400, 500)` because the window is still non-empty and in range, and the alternative (rejecting) would
break the two load-time callers that RELY on the clamp — `replaceSample`, which falls back to the full
buffer for an out-of-range window (`src/core/SampleClip.cpp:176-177`), and `loadSettings`, which reads
`srcin`/`srcout` straight from an older project's XML (`src/core/SampleClip.cpp:632`). The existing
regression case `SampleClipWindowTest::malformedWindowIsRejected`
(`tests/src/tracks/SampleClipWindowTest.cpp:230-250`) already pins the reject rule for both shapes the
report's concern covers (an empty window, and a start at or past the window end) and passes.
Commit: none
Test: none
Red/green: none

### Cluster 3512 — Tone generation divides by a zero sample rate
Verdict: FALSE-POSITIVE
Evidence: `tone()` computes `frequencyHz * TwoPi / sampleRate` with no guard
(`src/core/SampleOperators.cpp:86-97`), which is the defect as described — but `generate()` has exactly
one caller in the tree, `sample.generate`
(`src/core/ControlCommandsSample.cpp:243`), and that caller sets `spec.sampleRate` from
`Engine::audioEngine()->outputSampleRate()` (`src/core/ControlCommandsSample.cpp:141`) rather than from
the caller's arguments: the wire schema has no rate property at all
(`src/core/ControlCommandsSample.cpp:192-203`), so a caller cannot ask for a zero rate. There is no
reachable path on which `tone()` sees `sampleRate == 0`; the case is a hypothetical direct call to an
internal helper, and the reported trigger (`generate(Kind::Tone, 440.0, 1.0f, frames, 0)`) is not
reachable in this build. Adding a guard would be dead code rather than a fix.
Commit: none
Test: none
Red/green: none

### Cluster 3513 — Noise and tone generators do not enforce the documented amplitude range
Verdict: FALSE-POSITIVE
Evidence: the header documents amplitude as linear 0..1
(`src/core/SampleOperators.h:54-56`) and `generate()` does multiply by it unvalidated
(`src/core/SampleOperators.cpp:93, 107, 120`) — but the range is enforced at the boundary, before any
generator runs: `parseLevel()` rejects anything outside `0..1` with a typed `InvalidArgs` and returns false
before `parseGenerate()` reaches `sizeFrames()` and the handler
(`src/core/ControlCommandsSample.cpp:126-135, 168-173, 227`). A requested amplitude of 2 is refused by
`sample.generate`, so no generated buffer can reach magnitude 2. Note `0.0` and `NaN` are also caught:
the comparison is a range test, and `NaN` fails both halves of it.
Commit: none
Test: none
Red/green: none

### Cluster 5302 — Source-tempo clips have the wrong playback rate when unwarped
Verdict: ALREADY-FIXED
Evidence: `warpRatio()` does not special-case an empty marker map: it computes
`m_naturalFramesPerTick / rate` where `rate` comes from
`m_warp.framesPerTickAt(frame, m_baseFramesPerTick)` (`src/core/SamplePlayHandle.cpp:455-496`), and
`framesPerTickAt` returns the clip's own `baseFramesPerTick` when there are fewer than two markers
(`include/WarpMarkers.h:264-267`). `m_baseFramesPerTick` is snapshotted from
`clip->clipFramesPerTick()` at handle construction (`src/core/SamplePlayHandle.cpp:112`), which for a
`SourceTempo` clip is already scaled by `projectTempo / sourceTempo`
(`src/core/SampleClip.cpp:337-350`) — so an unmarked leader derives exactly the ratio the report says it
cannot, with no markers in play. The regression case
`SampleClipWarpTest::unmarkedLeaderRendersAtItsLeaderRate`
(`tests/src/tracks/SampleClipWarpTest.cpp`) asserts this as a RENDERED PITCH (a leader at half the project
tempo must sound roughly an octave above the follower), and it passes.
Commit: none
Test: SampleClipWarpTest::unmarkedLeaderRendersAtItsLeaderRate (pre-existing, passing)
Red/green: none

### Cluster 3511 — Comps silently ignore spans after the thirty-second
Verdict: FALSE-POSITIVE
Evidence: the report's limit is wrong by 8x: `MaxCompSpans` is 256, not 32
(`include/SamplePlayHandle.h:210`). The `32` in this area belongs to a different constant —
`CompSlip::MaxSpans` (`include/CompSlip.h:68`) — which is a separate span array in the comp-slip readers,
not the one `snapshotComp` fills. `snapshotComp` does skip silently once the array is full
(`src/core/SamplePlayHandle.cpp:289`), so the MECHANISM the report describes is real, but the trigger it
gives (more than 32 selected comp segments) cannot reach it, and reaching 256 requires 256 distinct
selected spans in one lane. The lanes themselves carry no such cap — `TakeLane::m_segments` is a
`std::vector` with no member limit (`include/TakeLane.h:194, 257`) — so this is a storage-capacity
question on the audio path, not a validation defect: growing the array per handle would mean allocation on
a play path (rule 6), and the report's own alternative (a segment limit enforced when editing) would be a
design change to the comp model. Not fixable inside this lane's rules; raised for the parent rather than
guessed at.
Commit: none
Test: none
Red/green: none

Leftovers: Cluster 3511's silent-skip at `src/core/SamplePlayHandle.cpp:289` is a genuine capacity limit
at 256 spans, not 32, and has no model-side counterpart cap — resolving it needs either a non-allocating
over-capacity strategy on the play path or an edit-time limit, both outside this lane. Cluster 3512 is
real arithmetic in `tone()` but reachable only by a direct internal call; if `sample.generate` ever grows
a `sample_rate` argument, the guard belongs with the other `sizeFrames()` validations.