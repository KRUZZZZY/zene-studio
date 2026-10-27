FIXED

# Cluster new-p15-6: clip.consolidate removed the clips before the composite refused the replacement

Finding (Luna patrol, historical line 182): the replacement called `TakeLaneModel::selectSegment` without asking
`canSelect` first, unlike the other composite edits. A base lane at its span budget refused the selection after
the source clips were already deleted; the command still reported `recomped: true`, and the composite kept
pointing at removed takes.

## Verdict and evidence

REAL, and the cause is as described. Verified on the branch before the fix:

- `src/core/ControlCommandsConsolidate.cpp` (pre-fix line 184): `if (*recomped) { lanes.selectSegment(...) }` ignores
  the return value, after `track->removeClip(clip)` (line 174) has already deleted the source clips.
- `src/core/TakeLane.cpp:453-456`: `selectSegment` returns `false` without changing anything when
  `canSelect` refuses; `canSelect` (`src/core/TakeLane.cpp:432-449`) enforces the per-lane budget through
  `withinPlaybackSpanBudget` (`src/core/TakeLane.cpp:127-135`), with `TakeLaneModel::MaxCompositeSpansPerLane = 256`
  (`include/TakeLane.h:169`).
- The siblings all ask first: `src/core/ControlCommandsCompEdits.cpp:160` (`canSelect`) before `:171`
  (`selectSegment`), and `src/core/ControlCommandsCompGroup.cpp:223` before `:237`. Only the consolidate path skipped it.
- `Track::removeClip` (`src/core/Track.cpp:1044-1058`) does not touch composite segments; `removeTakeClip` only
  changes the take-group's active lane. `assignTakeLane` (`src/core/ControlCommandsComp.cpp:364-367`) only sets the
  clip's lane tag. So `canSelect` asked before the render gives the same answer the replacement would have got.

## The fix (product: `src/core/ControlCommandsConsolidate.cpp`)

- New `compositeRefusal(track, region)` (line 167): when there is a composite to repaint (`laneCount() > 0` and
  segments present), it asks `canSelect(region.start, region.end, baseLane())` and returns a Refused message if
  that fails. The condition mirrors `replaceWithConsolidated`'s own, so it applies to the same callers and no others.
- `clipConsolidate` (line 226) calls it after `collectRegion` and before the render, so a refusal writes no file,
  removes no clip, and records no checkpoint (no undo step).
- `replaceWithConsolidated` (line 199): `*recomped = repaints && lanes.selectSegment(...)`, so the flag reports what
  happened rather than what was intended. The pre-check guarantees the selection holds.
- No new command, no header change, no change to `canSelect` or the span budget. Tracks without a composite and
  composites under budget behave exactly as before.

## Regression test (`tests/src/core/ClipConsolidateTest.cpp`)

- `aReplacementTheCompositeCannotCarryIsRefusedBeforeAnythingChanges` (line 255): the base lane holds 256 spans
  (its budget), one tick clear of the region; a second lane holds the clip's span over `[0, length)`. The consolidate
  must be Refused (line 276), write no file, remove no clip, and leave the segments unchanged.
- `aReplacementThatFitsTheBudgetRepaintsTheComposite` (line 288): the boundary, 255 spans. The consolidate succeeds,
  `recomped` is true, and the composite's first segment is the new clip's span on the base lane.
- Deterministic: no timing, no sleeps, no threads. The spans are built with `selectSegment`, the region is the clip's
  own extent.
- Test-design note: the spans sit one tick past the region. A span that touched the region would merge into the
  replacement (`continuesTake`, `src/core/TakeLane.cpp:81-86`) and add nothing. The first draft did that, so
  `canSelect` accepted the replacement even with the fix and the test failed for the wrong reason. It was corrected
  before the proof run.

## Commands and results (all run from `build/tests` or the worktree, logs in the LOG FOLDER)

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-6.r1/logs/`

| Step | Command | EXIT | Result |
|---|---|---|---|
| build (ccache temp dir fix, see below) | `CCACHE_TEMPDIR=… cmake --build build --target ClipConsolidateTest -j4` (`build-1-red.log`, first try) | 2 | ccache could not create its temp file in the sandbox; no compile error |
| build (RED, v1 test) | same, `build-2-red.log` | 0 | |
| RED v1 | `ctest -R ClipConsolidateTest --output-on-failure` (`test-1-red.log`) | 8 | refusal test: `done.errorKind` 0, expected 5 (`ClipConsolidateTest.cpp:275` in v1); the other 8 QTest entries pass |
| GREEN attempt, v1 test + fix (`test-2-green.log`) | same ctest | 8 | still errorKind 0: the v1 spans touched the region and merged, so the premise was wrong, not the fix. Test corrected (one-tick gap) |
| 4b: capture fix | `git diff -- src/core/ControlCommandsConsolidate.cpp > .../logs/fix.patch` | 0 | |
| 4b: revert fix | `git apply -R .../logs/fix.patch` | 0 | |
| 4b: build unfixed | `cmake --build build --target ClipConsolidateTest -j4` (`build-4-unfixed.log`) | 0 | |
| 4b: RED (must FAIL) | `ctest -R ClipConsolidateTest --output-on-failure` (`test-3-unfixed-red.log`) | 8 | `FAIL! aReplacementTheCompositeCannotCarryIsRefusedBeforeAnythingChanges`: Actual `done.errorKind` 0, Expected `ControlErrorKind::Refused` 5. Other functions pass: 9 passed, 1 failed |
| 4b: re-apply fix | `git apply .../logs/fix.patch` | 0 | |
| 4b: build fixed | `cmake --build build --target ClipConsolidateTest -j4` (`build-5-fixed.log`) | 0 | |
| GREEN (must PASS) | `ctest -R ClipConsolidateTest --output-on-failure` (`test-4-fixed-green.log`) | 0 | `100% tests passed out of 1` |
| GREEN, per function | `./ClipConsolidateTest` (in `build/tests`, `test-6-direct-green.log`) | 0 | `Totals: 10 passed, 0 failed` (8 test functions + init/cleanup), incl. both new functions |
| Family build | `cmake --build build --target RangeEditsTest ProvenanceVersionTest -j4` (`build-6-siblings.log`) | 0 | |
| Family run | `ctest -R "ClipConsolidate|RangeEdits|ProvenanceVersion"` (`test-7-family.log`) | 0 | `100% tests passed out of 3` (3 tests executed; these are the tests that reference `clip.consolidate`) |
| Gate | `python3 feature_check.py <worktree>`, see note (`gate-1.log`) | 0 | `RESULT: PASS`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all exit 0; complexity and file-length "no new item in your files" |

Note on the gate run: `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check`, which is outside the
write-jail (`Permission denied`, first attempt, not logged). I ran the unmodified script in place, with only its
`logs = …` line redirected into the LOG FOLDER (`gate-logs/`). Its checks, baselines and cache are unchanged.

Note on ccache: the first build failed only because the ccache wrapper could not create temp files under the
sandboxed `dsh/home` path. The rebuilds set `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-p15-6`
(a build cache directory). The compiled code is unaffected.

## Scope and not checked

- Build: targeted only (`ClipConsolidateTest`, `RangeEditsTest`, `ProvenanceVersionTest`, and `zene` as a dependency).
  The full suite was not run; `TakeLane*` tests are not affected because `src/core/TakeLane.cpp` is unchanged.
- Undo: the refusal records no checkpoint, so no undo step is created. The existing `REV_UNDO_OR_FAIL` path is covered
  by the unchanged `theCompositeIsHonouredThenNamesTheOneClip` and `twoClipsAndAGapBecomeOneClipAndUndoBringsThemBack`.
- Realtime: no audio-thread code changed. The new check runs on the control thread.
- No `tests/fork-sources.txt` or `tests/upstream-modifications.txt` change is needed: the product file is already
  registered in `fork-sources.txt` (line 1437), and no new file was added.

## Self-check

- **Regression test fails with the product change reverted, passes with it.** Reverted (`git apply -R fix.patch`, rebuild
  EXIT 0, `ctest -R ClipConsolidateTest` EXIT 8): `aReplacementTheCompositeCannotCarryIsRefusedBeforeAnythingChanges`
  fails with `done.errorKind` 0, expected 5 (Refused). Restored (`git apply fix.patch`, rebuild EXIT 0, ctest EXIT 0,
  binary EXIT 0, `Totals: 10 passed, 0 failed`). The full list is in the table above. Logs: `test-3-unfixed-red.log`,
  `test-4-fixed-green.log`, `test-6-direct-green.log`.
- **Deterministic.** No wall-clock, sleep, thread or race. The composite is built with `selectSegment` on integer ticks
  (`length + 1 + 2*i`), the region is the clip's own extent (`unmutedExtent`), and the output file goes to the test's
  own temporary directory. The refusal is decided by `canSelect`, which is a pure function of the segments.
- **Caller and sibling check.**
  - `compositeRefusal` and `replaceWithConsolidated` have one caller each (`ControlCommandsConsolidate.cpp:226` and
    `:242`, from `rg -n "replaceWithConsolidated|compositeRefusal" src tests include`).
  - The sibling composite edits already ask `canSelect` before `selectSegment`: `ControlCommandsCompEdits.cpp:160/171`,
    `ControlCommandsCompGroup.cpp:223/237`, `ControlCommandsRecordingPass.cpp:128/133`. The same rule now applies
    to consolidate.
  - `Track::removeClip` (`Track.cpp:1044-1058`) and `assignTakeLane` (`ControlCommandsComp.cpp:364-367`) do not touch the
    composite's segments, so the pre-render answer equals the answer the replacement would have got.
- **No collateral damage.**
  - The check runs only when the track has a composite with segments. Tracks without one are unchanged.
  - A composite under budget is unchanged: the 255-span boundary test succeeds with `recomped` true, and the existing
    `theCompositeIsHonouredThenNamesTheOneClip` and `twoClipsAndAGapBecomeOneClipAndUndoBringsThemBack` pass.
  - Saved projects: no serialisation, load path, or saved field changed. `TakeLane.cpp` and `canSelect`'s budget are
    unchanged, so no other caller's limit moved.
  - Verdict is REAL, so no already-fixed or false-positive citation is needed.
