# docmodel batch-01 — Space Bunny, 2026-10-05

Lane: DataFile / DocumentIndex / provenance / automation / track folders.
Six findings, all reported against `src/core/AutomationClip.cpp`. Two were fixed,
two were duplicates of those two, and two are out of scope for this lane.

### Cluster 32 — Automation ramp construction can block the audio thread

Verdict: SKIPPED-SCOPE

Evidence: `src/core/AutomationClip.cpp:763` — `writeBlockRamp()` does take
`m_clipMutex`, and `src/core/Song.cpp:1263` is the audio-thread caller, so the
report's mechanism is real. It is also the shipped design: `include/AutomationClip.h:217-219`
states that the builder takes the clip's own mutex ONCE for the whole block
rather than once per evaluation, and the mutex is a `QRecursiveMutex`
(`include/AutomationClip.h:321`) so the nested `valueAt()` calls cost nothing
extra. Removing the lock means publishing a lock-free snapshot of the node map
to the audio thread — a new representation and a new publication protocol, far
past a ~150-line behaviour-preserving fix and exactly the audio-thread
restructure delegation rule 6 sends back as SKIPPED-SCOPE.

Commit: none

Test: none

Red/green: none

### Cluster 33 — Ramp lookup can decrement before the first automation node

Verdict: FIXED

Evidence: `src/core/AutomationClip.cpp:727` — `rampValueAt()` did
`std::prev( m_timeMap.upperBound( ticks ) )` with no `begin()` check, so a clip
whose first point is not at tick 0 (the shape a user builds by deleting the
point the surface seeds) had every pre-first-point frame read a node-map header.
The fix returns the clip's pre-first value — 0, which is what
`valueAt( TimePos )` returns for the same ticks (`src/core/AutomationClip.cpp:589-592`)
— before stepping back.

Commit: 1ca92a353

Test: SampleAccurateAutomationTest::aBlockBeforeTheFirstPointReadsThePreFirstValue

Red/green: `cmake --build build --target zene SampleAccurateAutomationTest > build/bunny-b.log 2>&1` EXIT=0;
`cd build/tests && ctest -R '^SampleAccurateAutomationTest$' --output-on-failure > build/bunny-t.log 2>&1` EXIT=8
("frame 0 of a block entirely before the first point reads nan") → EXIT=0 after the fix;
`ctest -R '^(SampleAccurateAutomationTest|NoteCurvesAcrossTest|AutomationModesTest|W3EngineTest)$'` EXIT=0;
`bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 34 — Discrete automation steps are smeared at exact tick boundaries

Verdict: FIXED

Evidence: `src/core/AutomationClip.cpp:791-807` — when a tick boundary landed
exactly on a sample, `floor(knotFrame)` and `ceil(knotFrame)` were the same
frame, so both knots of the pair landed on it and both carried the value going
OUT; the ramp interpolated into the step over the frames before it. The same
held for a boundary landing on the block's LAST frame, where only the block's
end knot carried the step. Both now write the frame before the boundary with the
value coming in, which is what the loop's own comment already promised.

Commit: d797b9d2e

Test: SampleAccurateAutomationTest::aDiscreteStepOnAnExactSampleIsHeldUntilThatSample

Red/green: same build command EXIT=0;
`ctest -R '^SampleAccurateAutomationTest$' --output-on-failure` EXIT=8 ("frame 1 of tick 0 reads 35,
the clip holds 20: the step was smeared instead of held") → EXIT=0 after the fix;
`ctest -R '^(SampleAccurateAutomationTest|NoteCurvesAcrossTest|AutomationModesTest|W3EngineTest|AutomationTrackTest)$'` EXIT=0;
both gates EXIT=0.

### Cluster 1844 — Sample ramp crashes before the first automation point

Verdict: DUPLICATE

Evidence: the same statement as Cluster 33 at the same function — `prev(begin())`
on a `QMap` node map in `rampValueAt()` — fixed once under 1ca92a353 and covered
by the one regression. Two reports, one defect, one fix.

Commit: 1ca92a353 (as Cluster 33)

Test: SampleAccurateAutomationTest::aBlockBeforeTheFirstPointReadsThePreFirstValue

Red/green: as Cluster 33.

### Cluster 1845 — A clip starting mid-block is skipped for that whole block

Verdict: SKIPPED-SCOPE

Evidence: `src/core/AutomationClip.cpp:772` returns when `relStart < 0`, so the
reported condition is real — but the engine never calls the builder for a block
that begins before the clip does: `src/core/Song.cpp:1257` skips any clip with
`startPosition() > readStart`, so the "delayed onset" is the shipped latent-audio
rule, not a defect. The reachable variant is a clip whose start TIME OFFSET makes
`relStart` negative while `startPosition() <= readStart`; there the builder
returns with no knots and `AutomationRamp::valueAt` answers 0 for every frame
(`src/core/AutomationRamp.cpp:62-64`), which is its own finding. Deciding what the
ramp should hold for the pre-clip frames of that block — the clip's start value,
the model's last published value, or a refusal to publish at all — is a design
decision on published behaviour, so this lane leaves it alone.

Commit: none

Test: none

Red/green: none

### Cluster 1846 — The audio ramp path locks a mutex

Verdict: DUPLICATE

Evidence: the same statement as Cluster 32 at the same function —
`writeBlockRamp()` takes `m_clipMutex` on the audio thread, `src/core/AutomationClip.cpp:763`
— and the same rule-6 reasoning applies. One defect, not two.

Commit: none (as Cluster 32)

Test: none

Red/green: none

Leftovers: `AutomationLanesTest` fails on this branch with `'!parameter.isEmpty()' returned FALSE`
in five cases — verified PRE-EXISTING by reverting `src/core/AutomationClip.cpp` to HEAD, rebuilding
and re-running (`ctest -R '^AutomationLanesTest$'` EXIT=8 before either fix landed). It is not in
this batch's scope and was left alone. Also noted, not fixed and not a cluster finding: a
`writeBlockRamp()` that returns with no knots leaves a zero-knot ramp whose `valueAt` answers 0, which
is the open question Cluster 1845 names above.