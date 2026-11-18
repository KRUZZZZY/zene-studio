# Engine batch-13

Six engine-lane findings, each adjudicated against the current tree on branch
`bugs/bunny-engine-2026-10-05`.

### Cluster 5285 — Resampling a buffer with a zero source rate can request an enormous output
Verdict: FIXED
Evidence: `src/core/SampleBufferResample.cpp:61` guarded a zero TARGET rate (`rate == 0`) but not the
source rate, and the ratio at line 64 is `target/source`, so a rate-0 buffer divided by zero and the
`wanted` length at line 65 became an infinite multiplication cast to a frame count. `sample_rate_t` is
`std::uint32_t` (`include/LmmsTypes.h:42`) and `SampleBuffer`'s constructors take the rate raw with no
validation (`src/core/SampleBuffer.cpp:38-49`), so such a buffer is constructible and reached that divide.
The guard now rejects a non-positive source rate and passes the buffer through untouched, matching how the
other unusable cases were already handled.
Commit: c34fd2a40
Test: SampleBufferResampleTest::aBufferWithNoSourceRateIsPassedThroughUnchanged
Red/green: red build `cmake --build build --target zene SampleBufferResampleTest` EXIT=0 then
`ctest -R ^SampleBufferResampleTest$` EXIT=8 — `FAIL! Caught unhandled exception` followed by SIGABRT,
the enormous vector allocation throwing before the QCOMPARE ran; green build EXIT=0,
`ctest -R ^SampleBufferResampleTest$` EXIT=0 (2/2), `ctest -R StemSplitPipelineTest|StemExportTest`
EXIT=0 (both targets rebuilt first — they reported "Not Run" on stale binaries); complexity-gate EXIT=0,
file-length-gate EXIT=0.

### Cluster 5284 — SRC mode can change on the audio thread
Verdict: SKIPPED-SCOPE
Evidence: the mechanism is real — `src/core/Sample.cpp:118-121` calls `setMode()` when the requested
quality differs from the handle's converter, and `AudioResampler::setMode()` calls `src_new()`
(`src/core/AudioResampler.cpp:118`), which allocates — on the play path. It is not reachable in the state
the report describes: `Sample::PlaybackState` is constructed with the mode already selected
(`include/Sample.h:49-53`), and `ProjectRenderer` sets the quality once per render before playback
(`src/core/ProjectRenderer.cpp:154-157`), so a handle is normally created with the mode the render will
ask for and the comparison at `Sample.cpp:118` is false. Making this robust for every ordering means
configuring converter state in handle construction — a design change to how handles and render settings
interact, and `Sample::play()` has no off-thread caller to move it to. Per the realtime rule this is left
alone rather than restructured.
Commit: none
Test: none
Red/green: none

### Cluster 4407 — Window and warp edits race with playback reads
Verdict: FALSE-POSITIVE
Evidence: playback never reads these fields live. `SamplePlayHandle` takes value copies at construction —
`m_window = window` and `m_warp = clip->warpMarkers()` (`src/core/SamplePlayHandle.cpp:104-111`), and the
comment there states the intent: a live handle renders the mapping it was created with and nothing the
control thread does afterwards can move it. `WarpMarkers` is a fixed-capacity POD value type whose
lookups are allocation-free and lock-free by contract (`include/WarpMarkers.h:108-114`), and the audio
thread reads the handle's copy (`src/core/SamplePlayHandle.cpp:466`). The one remaining reader,
`SampleTrack::processNextBuffer` (`src/tracks/SampleTrack.cpp:140,148`), reads `sampleWindow()` when
scheduling, which is the authoring thread before the handle exists. So no concurrent read/write pair on the
audio path exists for these setters to race.
Commit: none
Test: none
Red/green: none

### Cluster 706 — Replacing sample data can discard an authored trim
Verdict: ALREADY-FIXED
Evidence: `SampleClip::setSampleBuffer()` does reset the window to the full buffer
(`src/core/SampleClip.cpp:153-160`), which is correct for a new source, and `SampleClip::replaceSample()`
is the path that preserves an authored window under the same change guard (`src/core/SampleClip.cpp:168-184`,
with the window explicitly passed and clamped). Every same-length edit that must keep source coordinates
already goes through it, not `setSampleBuffer()`: `src/core/ControlCommandsSampleRange.cpp:130`
(`windowAfter(...)`), `src/core/ControlCommandsSampleTools.cpp:75` ("the window written by its owner"),
`src/core/SampleFrameEdits.cpp:85` and `src/core/AafImportBuild.cpp:100`. The only product callers of
`setSampleBuffer()` are genuinely new sources: `src/core/StemTrackBuilder.cpp:97` and
`src/core/ControlCommandsSample.cpp:242`, both on freshly created clips. The contract comment at
`src/core/SampleOperators.h:50` still names `setSampleBuffer` as the swap-in method, which is what makes the
report read as live; the behavior itself is already covered.
Commit: none
Test: none
Red/green: none

### Cluster 5304 — Replacing a sample buffer leaves its old warp markers in place
Verdict: ALREADY-FIXED
Evidence: `setSampleBuffer()` resets the window but does not clear `m_warp`
(`src/core/SampleClip.cpp:153-166`), which is what the report describes. But no product path replaces the
buffer of an existing clip through it: the only product callers are `src/core/StemTrackBuilder.cpp:97` and
`src/core/ControlCommandsSample.cpp:242`, which both create the clip first, so there are no markers from an
old source to leave behind. Dropping new audio onto an existing clip goes through
`SampleClip::setSampleFile()`, which does clear them — `m_warp.clear()` with the comment "the markers were
anchored to frames of the old one, so they go with it" (`src/core/SampleClip.cpp:191-198`). The remaining
callers of `setSampleBuffer()` are tests that build a fresh clip. So the described trigger ("drop new sample
data onto a warped clip") does not reach the unguarded code.
Commit: none
Test: none
Red/green: none

### Cluster 4380 — Render process diagnostics are discarded
Verdict: SKIPPED-SCOPE
Evidence: the observation is accurate — `RenderQueue::runRender()` sends both streams to the null device
(`src/core/RenderQueue.cpp:269-270`) and the job error is a fixed string ("the render process crashed" /
"the render process could not be started", `src/core/RenderQueue.cpp:271-277`), so diagnostics the child does
write (e.g. the loudness-report warning at `src/core/ProjectRenderer.cpp:339`) are lost. Surfacing them means
reading the child's stderr into a buffer and putting it in the job error, and that error string is a control
command's result payload — changing it moves the ControlCommandsSnapshot canary, which rule 8 reserves for
the parent. Adding a bounded stderr capture is also a multi-site change to process wiring and error
construction rather than the minimal correction this lane makes.
Commit: none
Test: none
Red/green: none

Leftovers: the contract comment at `src/core/SampleOperators.h:50` still tells readers that a transformed
buffer is swapped in through `SampleClip::setSampleBuffer`, but the same-length paths use
`SampleClip::replaceSample` (see Cluster 706) — a stale comment left untouched as out of scope for this
batch. Cluster 5284's `setMode()` allocation on the play path remains reachable in principle if a handle is
ever created with a mode that differs from the render's quality; no current caller does this.