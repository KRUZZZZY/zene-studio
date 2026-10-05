# docmodel batch-02 — Space Bunny, 2026-10-05

Lane: `docmodel` (automation clips, transforms, commands). Six findings triaged: 2 FIXED, 1 ALREADY-FIXED, 1 DUPLICATE, 1 FALSE-POSITIVE, 0 SKIPPED-SCOPE.

### Cluster 1848 — Some discrete steps are smeared across samples
Verdict: FIXED
Evidence: `src/core/AutomationClip.cpp:809-815` — the pre-boundary knot in `writeBlockRamp` was guarded by `if( before > 1 )`, so a boundary landing between samples (e.g. frame 1.3) omitted the frame-1 knot and the ramp interpolated frame 0 → frame 2, smearing the step onto the sample before it. The fix relaxes the guard to `before > 0`; frame 0 is the block's own knot and is the only frame that must not be written twice.
Commit: bd50d0baa
Test: SampleAccurateAutomationTest::aDiscreteStepBetweenSamplesIsHeldOnTheSampleBeforeIt
Red/green: `cmake --build build --target zene SampleAccurateAutomationTest > build/bunny-b.log 2>&1` then `cd build/tests && ctest -R '^SampleAccurateAutomationTest$' --output-on-failure` — RED EXIT=8 ("frame 1 reads 50, the clip holds 20 until the boundary at frame 1.3: the step was smeared onto the sample before it"), GREEN EXIT=0 (12 cases, 100% passed). Also green: AutomationModesTest, AutomationTrackTest, NoteCurvesAcrossTest, W3EngineTest, EngineRenderBenchmarkTest, ClapEffectIntegrationTest. Gates: complexity-gate.sh --check EXIT=0, file-length-gate.sh --check EXIT=0.

### Cluster 2678 — A ramp read before the first node can use an invalid iterator
Verdict: ALREADY-FIXED
Evidence: `src/core/AutomationClip.cpp:727-733` — `rampValueAt` now returns the curve's pre-first-node value when `upperBound` yields `next == m_timeMap.begin()`, so `std::prev` is never taken on `begin()`. Fixed on this branch by `1ca92a353` (batch-01, cluster 33).
Commit: none (already on branch: 1ca92a353)
Test: SampleAccurateAutomationTest::aBlockBeforeTheFirstPointReadsThePreFirstValue (pre-existing, from batch-01)
Red/green: none — no code changed in this batch.

### Cluster 2680 — A block crossing a clip's start gets an empty ramp
Verdict: FALSE-POSITIVE
Evidence: `src/core/AutomationClip.cpp:771-775` does return early when the block starts before the clip, but the only caller, `src/core/Song.cpp:1255-1260`, skips any clip with `automationClip->startPosition() > readStart`. The partially-overlapping block described in the finding never reaches the ramp builder, so the empty-ramp path is unreachable from production code.
Commit: none
Test: none
Red/green: none — no code changed.

### Cluster 3720 — Discrete automation smears at exact tick boundaries
Verdict: ALREADY-FIXED
Evidence: `src/core/AutomationClip.cpp:801-808` — when `before == after` (the tick boundary lands exactly on a frame) the code sets `before = after - 1`, so the pre-step value gets a knot on the preceding sample and the step happens at the boundary. Fixed on this branch by `d797b9d2e` (batch-01, cluster 34). The finding's secondary claim that the same `before > 1` check also skips a needed frame-1 knot is the frame-1 remainder of cluster 1848, fixed there in `bd50d0baa`.
Commit: none for the exact-boundary half (already on branch: d797b9d2e); 1848's half committed as bd50d0baa
Test: SampleAccurateAutomationTest::aDiscreteStepOnAnExactSampleIsHeldUntilThatSample (pre-existing) + aDiscreteStepBetweenSamplesIsHeldOnTheSampleBeforeIt
Red/green: see cluster 1848 — `cd build/tests && ctest -R '^SampleAccurateAutomationTest$' --output-on-failure` GREEN EXIT=0 after bd50d0baa.

### Cluster 5531 — Exact tick boundaries omit the pre-step value
Verdict: DUPLICATE
Evidence: Same mechanism and same site as cluster 3720: for an integer `knotFrame`, `floor()` and `ceil()` select the same frame, and `src/core/AutomationClip.cpp:801-808` already places the old value at `knotFrame - 1` and the new value at the boundary. Fixed on this branch by `d797b9d2e`.
Commit: none (duplicate of 3720 / d797b9d2e)
Test: SampleAccurateAutomationTest::aDiscreteStepOnAnExactSampleIsHeldUntilThatSample (pre-existing)
Red/green: none — no code changed.

### Cluster 1850 — Flipping a range discards per-point curve settings
Verdict: FIXED
Evidence: `src/core/AutomationClipTransforms.cpp` — `mirrorRange` and `flipEnds` rebuilt `AutomationNode`s from values only, and the node constructors leave `m_curve = Curve::Clip` and `m_bend = 0.f` (`include/AutomationNode.h:190-191`, `src/core/AutomationNode.cpp:60-69`), so a Hold/Bend point flipped to a new tick lost its curve. `mirrorRange` now copies `it->curve()` / `it->bend()` onto each mirrored node, and `flipEnds` gives each rebuilt end point the curve of the node it replaced, falling back to the other end.
Commit: 3a653c779
Test: AutomationLanesTest::aFlippedRangeKeepsEachPointsOwnCurve
Red/green: `cmake --build build --target zene AutomationLanesTest > build/bunny-b3.log 2>&1` then `cd build/tests && ctest -R '^AutomationLanesTest$' --output-on-failure` — RED EXIT=8 ("Actual 0 / Expected 1" at the Hold point mirrored to tick 144), GREEN for the new case with SampleAccurateAutomationTest, AutomationModesTest, AutomationTrackTest, NoteCurvesAcrossTest, W3EngineTest all EXIT=0. Gates: complexity-gate.sh --check EXIT=0, file-length-gate.sh --check EXIT=0.

Leftovers: `AutomationLanesTest` has 5 pre-existing failures in this environment (`!parameter.isEmpty()` / `model != nullptr` at tests/src/core/AutomationLanesTest.cpp:201,245,258,295,315) caused by the `tripleoscillator` plugin not being found; proven pre-existing by a temporary one-line revert probe (same 5 failures with bd50d0baa reverted) and not touched by this lane. Nothing else half-done.
