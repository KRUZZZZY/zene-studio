# Script lane — batch 06

Worktree `zene-bunny-script`, branch `bugs/bunny-script-2026-10-05`. Six findings: 3 fixed, 3 dismissed
(2 already fixed earlier on the branch, 1 out of lane scope).

### Cluster 3576 — NaN progress values pass through the clamp
Verdict: FIXED
Evidence: `src/core/StemJobManager.cpp` — both the provisioner callback and the separation callback stored the
raw `std::clamp(fraction, 0.0f, 1.0f)`. `std::clamp` is built on `<` comparisons, all of which are false for
NaN, so it returned NaN unchanged and published it into `job->downloadProgress` and the UI.
Commit: 96a2a03c9
Test: `StemJobManagerTest::testANonFiniteFractionIsNeverPublished`, `StemJobManagerTest::testANonFiniteDownloadFractionIsNeverPublished`
Red/green: `cmake --build build --target zene StemJobManagerTest > build/bunny-b.log 2>&1` EXIT=1 (new cases
fail) → `cd build/tests && ctest -R '^StemJobManagerTest$' --output-on-failure` EXIT=0 after the fix. Fix adds a
file-local `usableFraction()` that maps any non-finite input to `0.0f` before clamping; allocation-free, so the
audio-thread rule holds.

### Cluster 1758 — Successful jobs can emit 100% progress twice
Verdict: FIXED
Evidence: `src/core/StemJobManager.cpp` `runJob()` — the throttled progress callback emits when
`fraction >= 1.0f`, and the `Status::Success` branch then unconditionally called `jobProgress(job->id, 1.0f)`
again. A backend that reports its final `1.0` and returns `Success` therefore produced two completion
progress events for one job.
Commit: f4627281c
Test: `StemJobManagerTest::testOneHundredPercentIsPublishedExactlyOnce`
Red/green: build of `zene` + `StemJobManagerTest` EXIT=1 with the new case (2 progress events where 1 is
expected) → `cd build/tests && ctest -R '^StemJobManagerTest$' --output-on-failure` EXIT=0. Fix records
whether completion was already published from `job->lastEmittedProgress` and suppresses the duplicate emit.

### Cluster 4495 — A corrupt model is treated as present and cannot be refreshed
Verdict: FIXED
Evidence: `src/core/StemModelStore.cpp` — `canFetchDefaultModel()` and `fetchDefaultModelIfMissing()` both
gated on `isModelPresent()` (exists + nonempty), so a nonempty file with the wrong bytes blocked fetching and
the GUI never offered a repair download.
Commit: 234e13aae
Test: `StemModelStoreTest::testACorruptDefaultModelIsNotPresentAndStaysFetchable`, `StemModelStoreTest::testAnExplicitModelPathIsTakenAtFaceValue`
Red/green: with `src/core/StemModelStore.cpp` reverted to `HEAD:` the build EXIT=0 and
`cd build/tests && ctest -R '^StemModelStoreTest$' --output-on-failure` EXIT=1 (`canFetchDefaultModel()` returned
FALSE); after the fix both gates pass and `ctest -R '^StemModelStoreTest$'` EXIT=0 (19 cases).
`StemSplitPipelineTest` was rebuilt and rerun as the covering test for the changed file: EXIT=0.
Fix adds `defaultModelUsable()`, which verifies the pinned size and SHA-256 for the default spec while keeping
the operator's `LMMS_STEM_MODEL` override on the nonempty-only check, per the header contract at
`include/StemSeparation/StemModelStore.h:77-86`.

### Cluster 782 — The model download can write an unbounded response to disk
Verdict: ALREADY-FIXED
Evidence: `src/core/StemModelStore.cpp:315-375` — `appendCapped(QNetworkReply*, QFile&, qint64 maxBytes)`
checks `partFile.pos() + chunk.size() > maxBytes` and aborts the reply before writing the offending chunk;
`streamInto` (line 331) and the final drain (line 375) both route through it, with `download()` passing
`spec.sizeBytes` as the cap. The cap and the removal of `finalPath` only after `verify(partPath, ...)` both
came in with commit `57980beef` on this branch (`git log -S'appendCapped'`).
Commit: none (pre-existing fix on the branch)
Test: existing `StemModelStoreTest` cases covering the oversize refusal
Red/green: none — no code changed; `cd build/tests && ctest -R '^StemModelStoreTest$'` EXIT=0.

### Cluster 5401 — Creating stem tracks bypasses the project undo path
Verdict: SKIPPED-SCOPE
Evidence: `src/core/StemTrackBuilder.cpp:83-100` builds four `SampleTrack`s and `SampleClip`s through
constructors that self-register, with no command or `ProjectJournal` checkpoint around the mutation — the
described behaviour is real. Making it reversible requires a new journalled `Command` subclass plus undo
semantics for the builder's public static signature in `include/StemSeparation/StemTrackBuilder.h`, which is a
design change well beyond a one-defect fix and does not touch any control-command surface.
Commit: none
Test: none
Red/green: none — product code intentionally left unchanged.

### Cluster 4567 — NaN output can be reported as deterministic
Verdict: ALREADY-FIXED
Evidence: `src/wasm/WasmOfflineRender.cpp:214-240` — `sampleDelta(float a, float b)` returns `0.0` when both
samples are NaN and `HUGE_VAL` whenever exactly one is non-finite, so an all-NaN pair is still compared
through `compareRenders` and never inflates `maxAbsDifference`; `outcome.deterministic` is set from
`tolerance.floor.covers(outcome.subject)` at line 335, and `RenderDifference::covers`
(`src/wasm/WasmOfflineRender.h:89`) compares `differingFrames` against a nonzero floor. The code predates the
reported revision: `git log -S'sampleDelta'` points at `57980beef` on this branch.
Commit: none (pre-existing fix on the branch)
Test: existing offline-render comparison cases
Red/green: none — no code changed.

Leftovers: none. Cluster 5401 is left as a known design gap for a lane that owns the project journal /
undo-command API; clusters 782 and 4567 need no work. Build and file-length gates were EXIT=0 before each of
the three fix commits.
