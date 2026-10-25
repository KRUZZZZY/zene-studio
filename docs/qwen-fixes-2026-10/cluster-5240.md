FIXED

5240: ALREADY-FIXED

Evidence: [src/core/ProjectIds.cpp](../../src/core/ProjectIds.cpp#L353) saturates revision increments with successorOf(), and [tests/src/core/ProjectRevIdsTest.cpp](../../tests/src/core/ProjectRevIdsTest.cpp#L284) loads rev=INT_MAX, edits the track, saves, and requires a positive INT_MAX revision. Both were already present on this branch. Temporarily replacing the saturating increment with ++rev made the test fail because the saved rev parsed as 0; restoring the existing implementation made it pass.

5229: FIXED

Evidence: [src/core/ProjectIds.cpp](../../src/core/ProjectIds.cpp#L242) now uses successorOf(id) for both s_documentFloor and s_next. observe() and allocate() already used the same saturating helper at lines 201 and 209. The new [tests/src/core/StableTrackIdsTest.cpp](../../tests/src/core/StableTrackIdsTest.cpp#L485) fixture sets next-id and the first stored track id to INT_MAX, then loads an ID-less track. This drives both its retained-id assignment and duplicate repair at exhaustion. Before the fix ProjectIds::next() became -2147483648; after the fix it remains INT_MAX. The exhausted counter can repeat INT_MAX, matching the existing successorOf() contract.

## Regression matrix

Normal revision updates: ProjectRevIdsTest::theNextChangeIncrementsRev and aChangeAfterLoadStampsRevOneAndTheInstanceWriter.
Revision exhaustion and legacy behavior: ProjectRevIdsTest::aMaximumStoredRevisionSaturatesInsteadOfWrappingNegative and the unrevised-project round-trip cases.
Normal ID assignment and project reload: StableTrackIdsTest::legacyProjectGetsDeterministicIdsAndResavesByteIdentically and rootCounterIsHonouredAndNeverReused.
ID exhaustion: StableTrackIdsTest::aMaximumKeptLoadIdDoesNotWrapTheCounter.
Reconfiguration: both fixtures load through Song::loadProject, which resets ProjectIds for each document; the suites exercise multiple loads in one Engine process.
Audio-thread constraints: these changes stay in project load/save bookkeeping paths. No rendering or per-period code changed.
Platform/build: both cases use existing C++20 Qt test targets and add no dependency or platform-specific code.
The full pre-edit matrix is in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5240.r1/logs/01-test-matrix.log.

## RED/GREEN proof and family counts

All ctest commands below ran from build/tests. Each family invocation registered and executed one CTest test.

5240 existing-test proof:
- `cmake --build build --target zene ProjectRevIdsTest` — EXIT=0 (logs/06-build-5240-reversed.log)
- `ctest -R '^ProjectRevIdsTest$' --output-on-failure` with ++rev — EXIT=8; aMaximumStoredRevisionSaturatesInsteadOfWrappingNegative failed because written was 0 (logs/07-test-5240-reversed.log).
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5240.r1/logs/05-5240-reversal.patch` — EXIT=0; restored the branch's existing saturation.
- `cmake --build build --target zene ProjectRevIdsTest` — EXIT=0 (logs/08-build-5240-restored.log)
- `ctest -R '^ProjectRevIdsTest$' --output-on-failure` — EXIT=0 (logs/09-test-5240-restored.log).

5229 regression proof:
- `cmake --build build --target zene StableTrackIdsTest` — EXIT=0 (logs/10-build-5229-red.log)
- `ctest -R '^StableTrackIdsTest$' --output-on-failure` before the production fix — EXIT=8; the new test observed -2147483648 (logs/11-test-5229-red.log).
- `git diff -- src/core/ProjectIds.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5240.r1/logs/12-5229-fix.patch` — EXIT=0.
- `cmake --build build --target zene StableTrackIdsTest` — EXIT=0 (logs/13-build-5229-green.log)
- `ctest -R '^StableTrackIdsTest$' --output-on-failure` — EXIT=0 (logs/14-test-5229-green.log).
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5240.r1/logs/12-5229-fix.patch` — EXIT=0.
- `cmake --build build --target zene StableTrackIdsTest` — EXIT=0; `ctest -R '^StableTrackIdsTest$' --output-on-failure` — EXIT=8 (logs/15-build-5229-reversed.log and logs/16-test-5229-reversed.log).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5240.r1/logs/12-5229-fix.patch` — EXIT=0.
- `cmake --build build --target zene StableTrackIdsTest` — EXIT=0; `ctest -R '^StableTrackIdsTest$' --output-on-failure` — EXIT=0 (logs/17-build-5229-reapplied.log and logs/18-test-5229-reapplied.log).

Final rebuilt family runs:
- `cmake --build build --target zene ProjectRevIdsTest StableTrackIdsTest` — EXIT=0 (logs/19-build-final.log).
- `ctest -R '^ProjectRevIdsTest$' --output-on-failure` — EXIT=0, 1 CTest test (logs/20-test-rev-final.log).
- `ctest -R '^StableTrackIdsTest$' --output-on-failure` — EXIT=0, 1 CTest test (logs/21-test-stable-final.log).

## Feature gates

The first feature_check run returned EXIT=1 because tests/all-sources.txt had 1,148 tracked vendored sfizz paths despite its header recipe excluding that tree. I regenerated the derived entry list from the unchanged header recipe; no threshold or recipe was altered. The generated list is in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5240.r1/logs/24-all-sources-generated.txt (generation EXIT=0).

Final command: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5240` — EXIT=0 (logs/26-feature-check-after-report.log).

| Gate | Result |
|---|---|
| fork-sources | PASS, EXIT=0 |
| all-sources-reproduce | PASS, EXIT=0 |
| no-upstream-regression | PASS, EXIT=0 |
| check-namespace | PASS, EXIT=0 |
| no-tautology | PASS, EXIT=0 |
| doc-figures | PASS, EXIT=0 |
| complexity | PASS, no new item in changed files |
| file-length | PASS, no new item in changed files |

## Files in the commit

src/core/ProjectIds.cpp, tests/src/core/StableTrackIdsTest.cpp, tests/all-sources.txt, and docs/qwen-fixes-2026-10/cluster-5240.md. The all-sources update is the exact derived output required by its existing header recipe.

## Self-check

- Regression failure and pass with the production change reverted/restored: 5240 reversal ctest EXIT=8 (logs/07), restored ctest EXIT=0 (logs/09). 5229 pre-fix ctest EXIT=8 (logs/11), fixed ctest EXIT=0 (logs/14), product patch removed ctest EXIT=8 (logs/16), and patch reapplied ctest EXIT=0 (logs/18).
- Determinism: the new test injects literal next-id=INT_MAX and a stored INT_MAX track ID, followed by one ID-less track. It uses no wall-clock values, sleeps, or races.
- Cause and sibling paths: noteLoadAssignment had two direct id+1 expressions; both now share successorOf(id). I checked all callers in Track.cpp, Clip.cpp, Note.cpp, Effect.cpp, Mixer.cpp, TakeLane.cpp, and Song.cpp, including duplicate repairs. observe() and allocate() already use the same helper.
- Collateral behavior: successorOf returns id+1 unchanged below INT_MAX, and only saturates at the type limit. Existing ordinary assignment, revision, round-trip, reload, and duplicate repair tests pass. No realtime code or saved-data format changed.
- ALREADY-FIXED evidence for 5240: ProjectIds.cpp:353 uses the saturating helper, and ProjectRevIdsTest.cpp:284 tests the maximum revision. The temporary ++rev regression failed as recorded in logs/07 and passed after restoration in logs/09.
- Every question printed by submit_check.py is answered above with the corresponding code, test, and gate evidence.
