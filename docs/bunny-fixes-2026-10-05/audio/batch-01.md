# Audio batch-01 — Luna findings (branch `bugs/luna-audio-2026-10-05`)

Four findings, each checked against the current tree before any code was touched.
Two were real and are fixed with regression tests; two do not survive reading the code.

### Cluster 4322 — Note release velocity is ignored when sending note-off

Verdict: FALSE-POSITIVE
Evidence: `src/core/NotePlayHandle.cpp:436-441` sends the note-off as
`MidiEvent(MidiNoteOff, midiChannel(), midiKey(), midiReleaseVelocity(m_instrumentTrack->midiPort()->baseVelocity()))`,
and `src/core/Note.cpp:137-140` is `m_releaseVelocity < 0 ? 0 : min(MidiMaxVelocity, m_releaseVelocity * midiBaseVelocity / DefaultVolume)`.
The track's `baseVelocity()` is the SCALING factor the note's own `m_releaseVelocity` is multiplied by, not a substitute
for it; the `-1` "unset" fallback the report asks for is exactly what the ternary already does. A note with release
velocity 64 and a track base velocity of 100 goes out as 64 * 100 / DefaultVolume, not as 100.
Commit: none
Test: none
Red/green: none

### Cluster 5200 — MPE pressure and timbre leak into later notes

Verdict: FALSE-POSITIVE
Evidence: `src/core/NotePlayHandle.cpp:183-203` returns early when `!hasMpeExpression()`, and the comment at 185-191
states the intent: nothing captured means no event and deliberately NO reset of the instrument's own channel state,
i.e. the exact behaviour that existed before MPE expression capture. Pressure and CC74 are per-MIDI-channel state by
specification, and `docs/MPE.md:378` documents the consumer contract (a test consumer resets them itself on note-on);
`docs/MPE.md:242` records that note-off drops the channel binding without erasing captured values. Adding a reset
here would change playback for every non-MPE note on a channel, which is a design change, not a defect fix.
Commit: none
Test: none
Red/green: none

### Cluster 3511 — Comps silently ignore spans after the thirty-second

Verdict: FIXED
Evidence: `src/core/SamplePlayHandle.cpp:289` was
`if (segment.laneIndex != lane || m_compSpanCount >= MaxCompSpans) { continue; }` over a fixed
`std::array` of `MaxCompSpans` (`include/SamplePlayHandle.h:203`), so every span past the 32nd was dropped with no
error. The model it reads has no such limit — `TakeLaneModel::segments()` returns an unbounded `std::vector`
(`include/TakeLane.h:194`) and `mergeSegments()` (`src/core/TakeLane.cpp:228-242`) only merges contiguous takes — so
a take selected in 40 places really did play its first 32 and go silent. The array stays a fixed member (no
allocation, locking or unbounded growth on the render path; `applyComp()` still walks it); the budget is now 256 and
documented as a render budget rather than a model limit.
Commit: 38d1b0159
Test: TakeLaneCompPlaybackTest::everySelectedSpanSoundsEvenPastTheOldThirtySecondCap
Red/green:
  Red:  `cmake --build build --target zene TakeLaneCompPlaybackTest` EXIT=0
        `cd build/tests && ctest -R '^TakeLaneCompPlaybackTest$'` EXIT=8
        `FAIL! 'std::fabs(levelIn(4 * 39) - 0.5) < 1e-4' returned FALSE. (the 40th selected span was dropped: the cap silently ignored it)`
        `SPAN_EVIDENCE out=88200 span32(=124)=0.5000 span39(=156)=0.0000 lane1(=157)=0.0000`
  Green:`cmake --build build --target zene TakeLaneCompPlaybackTest` EXIT=0
        `ctest -R '^(TakeLaneCompPlaybackTest|TakeLaneCompTest|SampleClipWarpTest)$'` EXIT=0
        the 15 clip/sample/comp/warp suites (AudioEditingTest, ClipFadesRenderTest, ClipGainEnvelopeTest,
        ClipWarpPersistenceTest, ControlWarpCommandsTest, SampleClipStretchTest, SampleClipRubberBandTest,
        SampleClipWindowTest, AudioResamplerRatioTest, ClipEditsTest, ClipTransposeTest, ClipConsolidateTest,
        TakeLaneCompTest, TakeLaneCompPlaybackTest, SampleClipWarpTest) EXIT=0, 100% passed, 0 failed out of 15
  Gates: `tests/complexity-gate.sh --check` EXIT=0, `tests/file-length-gate.sh --check` EXIT=0

### Cluster 5302 — Source-tempo clips have the wrong playback rate when unwarped

Verdict: FIXED
Evidence: `SamplePlayHandle::warpRatio()` returned `1.0f` whenever `m_warp.empty()`, but
`SampleClip::clipFramesPerTick()` (`src/core/SampleClip.cpp:337-350`) already scales a `SourceTempo` leader by
`projectTempo / m_sourceTempo` with no markers involved, so the handle's length was tempo-adjusted while the source
was consumed at its natural rate. The early return now covers only the unusable-rate case
(`m_naturalFramesPerTick <= 0.0f`); with no markers the function falls through to
`WarpMarkers::framesPerTickAt()` (`include/WarpMarkers.h:264-272`), which returns the already-snapshotted
`m_baseFramesPerTick` (`src/core/SamplePlayHandle.cpp:112-113`). No allocation or locking added.
Commit: 56f334fce
Test: SampleClipWarpTest::unmarkedLeaderRendersAtItsLeaderRate
Red/green:
  Red:  `cmake --build build --target zene SampleClipWarpTest` EXIT=0
        `cd build/tests && ctest -R '^SampleClipWarpTest$'` EXIT=8
        `FAIL! 'leaderPitch > followerPitch * 1.7 && leaderPitch < followerPitch * 2.3' returned FALSE.`
        `(leader at half the project tempo rendered 440.0 Hz, follower rendered 440.0 Hz - expected roughly twice the follower)`
        `Totals: 13 passed, 1 failed`
  Green:`cmake --build build --target zene SampleClipWarpTest` EXIT=0
        `ctest -R '^SampleClipWarpTest$'` EXIT=0
        the same 15 related suites as cluster 3511: EXIT=0, 100% passed, 0 failed out of 15
  Gates: `tests/complexity-gate.sh --check` EXIT=0, `tests/file-length-gate.sh --check` EXIT=0

## Not part of the four: the lane's own InstrumentPlayHandle lifetime work

One extra commit, `d31b6f201`, is a compile fix only: the owner-committed case
`ArrangingCatchUpTest::switchingMonitoringOffRemovesAHandleQueuedInTheSamePeriod()` binds
`SampleTrack::monitorHandle()` (`include/SampleTrack.h:93`, a const accessor returning `const InputMonitorHandle*`)
to a non-const `PlayHandle*`, so the target did not build at all. One `const_cast` at the binding; the case is
still RED for its own reason.

Leftovers: the lane-scope lifetime fix itself is NOT landed. The candidate change — pointer-matching
`InstrumentPlayHandle::isFromTrack()` and sweeping every requested type in the staged-handle list of
`AudioEngine::removePlayHandlesOfTypes()` — built, and the two staged candidates in ArrangingCatchUpTest went from
2 failures to a hard crash inside the pre-existing case
`newDocumentsCarryTheirTypesTemplate()` (Qt's crash handler then deadlocked in `fork()` on the malloc arena lock,
which is what looked like a hang): deleting a staged `InstrumentPlayHandle` double-frees it, because such a handle
is owned by its instrument's create/delete bookkeeping, exactly as the comment in that function already warns. The
change was reverted and left uncommitted; it needs an owner decision about who returns a staged instrument handle
to its owner. Also outstanding: `MaxCompSpans` is now 256, so a lane selected in more than 256 places still drops
the surplus silently — bounding that at the model/edit layer, or reporting it at selection time, is a design
change that was out of scope here.
