FIXED
new-p10-385: FIXED
new-p10-384: FIXED


## new-p10-385 — VERDICT: FIXED (REAL)
Evidence: src/core/SessionModelPrivate.h:201-207,214-230 apply each z:scene
index / z:cell coordinate without any duplicate check, so SessionModel.cpp:564-583
restores both into the candidate and commits. Confirmed by RED run: two
conflicting z:scene rows (both orders), two conflicting z:cell records, and a
duplicate at the end of a 256x512 grid all returned restoreScenesSection()==true.
Fix: grid-sized seenScenes + seenCells vectors in restoreScenesSection
(SessionModel.cpp); applySceneRow now validates only, cellIsFresh() checks
duplicates before applySceneCell() restores. Duplicates decline the whole
section, candidate discarded, Song preserves the element verbatim (claim-or-
preserve unchanged). Tests: SessionMalformedAttributeTest::duplicateSceneIndex
DeclinesSection, duplicateCellCoordinatesDeclineSection, maxGridDuplicateAtEnd
DeclinesSection (deterministic QDom fixtures, no timing).

## new-p10-384 — VERDICT: FIXED (REAL)
Evidence: src/core/SessionModelPrivate.h:61-65 doubleAttribute() returns any
successful toDouble(), so "nan"/"inf" enter SessionClip.cpp:94,96 (chance,
timeBars), :226 (gain), :231 (warpbpm), :274 (tempo). RED run: nan clip
gain/warpBpm non-finite, chance/time non-finite, inf scene tempo non-finite.
Fix: doubleAttribute() rejects non-finite to default; per-field helpers
clampedChanceAttribute [0,1], gainDbAttribute [-60,24], bpmAttribute (0,4096]
(BPM), timeBars finite-or-default. Callers in SessionClip.cpp updated.
Tests: SessionMalformedAttributeTest::nonFiniteAndOutOfRangeNumbersUseDocumen
tedPolicy (nan/inf, out-of-range finite, and valid boundaries -60/4096/0).

## Test matrix (Luna plan) coverage
- Ordinary: SessionModelTest::songProjectRoundTripsSessionBlock +
  ScenesUpconvertTest::scenesRoundTripThroughTheTopLevelSectionAndResavesIdent
  ically (valid distinct records, legacy upconversion, save/load/save stable)
  PASS before and after.
- Duplicates: both new duplicate tests above (scene both orders; cell both
  coordinates) RED before, GREEN after; distinct control loads.
- Max size: maxGridDuplicateAtEndDeclinesSection (256x512 unique accept,
  duplicate at end decline, no commit).
- Numeric: nonFinite test (nan/+inf/-inf per field, finite boundaries accepted,
  out-of-range clamped/defaulted).
- Realtime: SessionSchedulerRenderTest + SessionPlaybackRenderTest PASS before
  and after; restore-time rejection only, no audio-thread change.
- Portability: configured build is Qt 6; edit is plain C++20/QtCore (no Qt
  version-specific APIs), same results expected on Qt 5.

## Evidence table
| Item | Command | EXIT |
|---|---|---|
| Baseline family (5 tests, pre-change) | ctest -R SessionModelTest|SessionMalformedAttributeTest|ScenesUpconvertTest|SessionSchedulerRenderTest|SessionPlaybackRenderTest (from build/tests, QT_QPA_PLATFORM=offscreen) | 0 |
| RED original code | ./SessionMalformedAttributeTest > red-04.log | FAIL duplicateSceneIndexDeclinesSection, duplicateCellCoordinatesDeclineSection, maxGridDuplicateAtEndDeclinesSection, nonFiniteAndOutOfRangeNumbersUseDocumentedPolicy |
| GREEN fixed | ctest same 5 from build/tests > green-09-family.log | 0 (5/5 pass) |
| 4b RED (fix.patch reverted) | git apply -R fix.patch; cmake --build build --target SessionMalformedAttributeTest; ctest -R ^SessionMalformedAttributeTest$ > red-06-revert.log | FAIL (1/1, ctest exit 1) |
| 4b GREEN (fix.patch re-applied) | git apply fix.patch; cmake --build build --target SessionMalformedAttributeTest; ctest -R ^SessionMalformedAttributeTest$ > green-08-reapply.log | 0 (1/1 pass) |
| Gates | python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> | 0, RESULT: PASS (all 8 PASS) |
Committed files: src/core/SessionModelPrivate.h src/core/SessionClip.cpp src/core/SessionModel.cpp tests/src/core/SessionMalformedAttributeTest.cpp docs/qwen-fixes-2026-10/cluster-new-p10-385.md
One commit message names the cluster for both bugs in this batched task.

## Self-check
- REAL verdicts cited with file:line and RED assertions above (not unproven).
- Tests deterministic: QDom fixtures, Engine::init only, no clocks/sleeps.
- No collateral: full family 5/5; legacy <session> path untouched; Qt5-safe.
- Realtime: validation runs at project-load (model thread), no render change.
- git status clean after commits (verified before submit_check).

## Self-check
- RED/GREEN: fix.patch reverted → SessionMalformedAttributeTest FAILS (ctest exit 1, red-06-revert.log);
  re-applied → PASSES (exit 0, green-08-reapply.log). Recorded in Evidence table above.
- Determinism: QDom string fixtures parsed in-process; Engine::init(true) only; no sleeps,
  clocks, threads or timing anywhere in the new tests (synchronisation: none needed).
- Cause and completeness: doubleAttribute() callers checked (5 in SessionClip.cpp — all
  session doubles flow through it); duplicate paths checked (z:scene + z:cell candidate
  loop; legacy <session> restoreScenes/restoreClips reviewed and consistent with the
  non-duplicate legacy behavior — legacy duplicate row keeps earlier-taken behavior, out
  of the finding's native-section scope). Grid-sized flags only.
- No collateral: legacy <session> reader unchanged; valid distinct sections and
  ScenesUpconvert/Song round-trips unchanged (family 5/5 pass); clamp bounds (chance
  0..1, gain -60..24, BPM (0,4096]) are per-field documented ranges, not grid limits.
- ALREADY-FIXED/FALSE-POSITIVE question: N/A — both verdicts are FIXED (REAL).
