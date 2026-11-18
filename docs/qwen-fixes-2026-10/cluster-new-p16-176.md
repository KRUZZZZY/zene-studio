Verdict: FIXED

new-p16-176: FIXED. Revision restore installed an oversized `.revN` without checking `ProjectRevisionPolicy::MaxRevisionBytes`.

Evidence (the finding's line 281 has moved; the restore is now `restoreProjectRevision`, [ProjectRevisions.cpp:353](src/core/ProjectRevisions.cpp:353)):

- Before the fix, the restore staged its revision with `QFile::copy(source, staging)` and never looked at the size. A pending stage left by an earlier attempt was installed without a check too. Nothing between the copy and `replaceProjectWithStaged` refused an oversized file.
- Rotation does not catch it. `shiftRevisionsUp` only reads the slots below the restored one (`rev(n-1)` for `n` from `Keep-1` down to 1), so restoring `rev2` with an oversized `rev2` reached the install with nothing refusing it. Measured: `restoreProjectRevision(project, 2)` returned true and the live project became the oversized file (RED run below).
- Fix: the check lives in a new helper, `stageRestoreTarget` ([ProjectRevisions.cpp:175](src/core/ProjectRevisions.cpp:175)). It takes the pending stage if one exists, otherwise the revision itself, and tests the size of those bytes at [ProjectRevisions.cpp:180](src/core/ProjectRevisions.cpp:180). Over the cap, it refuses with an error that names the file, the size and the policy, before any copy is made and before the live file is rotated. The copy of the revision is made only after the check passes ([ProjectRevisions.cpp:193](src/core/ProjectRevisions.cpp:193)). `restoreProjectRevision` calls it at [ProjectRevisions.cpp:383](src/core/ProjectRevisions.cpp:383). A refusal leaves the live file, the retained set and any existing stage untouched.
- Callers and siblings checked:
  - Every restore path goes through `restoreProjectRevision`: `ControlCommandsRevisions.cpp:152,194`, `ControlCommandsProjectFiles.cpp:353,372,395`, and `RevisionTimeline.cpp:415`. The check covers all of them.
  - `replaceProjectWithStaged` has one other caller, `RevisionTimeline.cpp:454`, for the slot restore. That path reads its bytes through `readFileBytes(slot.path, Bounds::MaxRevisionBytes, …)` at `RevisionTimeline.cpp:391`, and `readFileBytes` refuses anything over the cap at `RevisionTimeline.cpp:169-173`. It was already bounded, so it is unchanged.
  - The rotation's `copyRevision` cap check (`ProjectRevisions.cpp:119`) and the shift refusal in `shiftRevisionsUp` (`ProjectRevisions.cpp:222-245`) are unchanged.
- Limit: the bound is `>` `MaxRevisionBytes`, the same comparison the rotation uses. A revision of exactly the cap still restores; the test `aRevisionExactlyAtTheCapStillRestores` checks this.

Regression tests (added to `tests/src/core/ProjectRevisionRestoreStagingTest.cpp`, registered already in `tests/fork-sources.txt`; no new files):

- `anOversizedRevisionIsRefusedBeforeTheRetainedSetMoves` (line 189): `rev2` is `MaxRevisionBytes + 1` bytes. The restore must fail; the live file, `rev0`, `rev1` and `rev2` keep their bytes, and no `.restore` stage is left in the directory.
- `anOversizedPendingStageIsNotInstalled` (line 214): a pending `rev0.restore.<uuid>` stage is oversized. The restore must fail, the live file must be unchanged, and the stage is kept.
- `aRevisionExactlyAtTheCapStillRestores` (line 234): a `rev1` of exactly `MaxRevisionBytes` restores. This passes before and after the fix; it guards against the limit catching legitimate revisions.

Test matrix (recorded before editing):

- Normal path: the existing four restore tests (`failedRestoreRetryUsesPendingStagedTarget`, `failedRestoreRetryFindsHiddenUuidStage`, `failedRestoreRetryFindsStageForANameWithGlobCharacters`, plus the Windows-only case that is skipped here) and `RevisionTimelineTest`.
- Failure paths: oversized revision, oversized pending stage, and the at-cap boundary.
- Realtime: none. Restore is a control-thread path. No audio code or locks were touched.
- Undo: no change. `ControlCommandsRevisions` records its undo step only after `restoreProjectRevision` succeeds (per the comment at `ControlCommandsProjectFiles.cpp:347-352`), so a refused restore records nothing.
- Platform: the Windows case is compiled under `#ifdef Q_OS_WIN` and was not built here.

Commands, with EXIT codes. `L` is the LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-176.r1/logs`. Builds ran with `CCACHE_DISABLE=1` because ccache could not create its temp file under the sandboxed path (the first attempt, `build-red-1.log`, exited 2 for that reason only).

| Evidence | Command | Result |
| --- | --- | --- |
| Build of the regression target, RED (first try, ccache failure, not a code error) | `cmake --build build --target ProjectRevisionRestoreStagingTest -j4 > $L/build-red-1.log 2>&1` | EXIT=2 (ccache permission error) |
| Build of the regression target, RED (with the first test file version, `rev1`-based) | `CCACHE_DISABLE=1 cmake --build build --target ProjectRevisionRestoreStagingTest -j4 > $L/build-red-2.log 2>&1` | EXIT=0 |
| RED run, first version. `rev1` test failed for a different reason (stage left behind after shift refusal), so the test was moved to `rev2` | `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure > $L/test-red-1.log 2>&1` (from `build/tests`) | EXIT=8 |
| RED run, final test file, original product code | `CCACHE_DISABLE=1 cmake --build build --target ProjectRevisionRestoreStagingTest -j4 > $L/build-red-3.log 2>&1` then `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure > $L/test-red-2.log 2>&1` | build EXIT=0; test EXIT=8: `FAIL! … anOversizedRevisionIsRefusedBeforeTheRetainedSetMoves() '!restoreProjectRevision(project, 2, &error)' returned FALSE` and `FAIL! … anOversizedPendingStageIsNotInstalled() '!restoreProjectRevision(project, 0, &error)' returned FALSE`; Totals 6 passed, 2 failed |
| Build after fix, GREEN (`zene` and both revision targets) | `CCACHE_DISABLE=1 cmake --build build --target zene ProjectRevisionRestoreStagingTest RevisionTimelineTest -j4 > $L/build-green-1.log 2>&1` | EXIT=0 (0 `error:` lines) |
| GREEN regression run | `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure > $L/test-green-1.log 2>&1` | EXIT=0 |
| GREEN per-case count | `ctest -R '^ProjectRevisionRestoreStagingTest$' -V > $L/test-green-2-verbose.log 2>&1` | EXIT=0; Totals 8 passed, 0 failed (includes the three new cases) |
| Revision family, GREEN | `ctest -R 'Revision' > $L/test-green-family.log 2>&1` (from `build/tests`) | EXIT=0; 2/2 executed and passed: `RevisionTimelineTest`, `ProjectRevisionRestoreStagingTest` |
| Step 4b: save the fix, revert it | `git diff -- src/core/ProjectRevisions.cpp include/ProjectRevisions.h > $L/fix.patch`; `git apply -R $L/fix.patch` | EXIT=0; EXIT=0 |
| Step 4b: rebuild reverted, run regression | `CCACHE_DISABLE=1 cmake --build build --target zene ProjectRevisionRestoreStagingTest -j4 > $L/build-4b-revert.log 2>&1`; `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure > $L/test-4b-red.log 2>&1` | build EXIT=0; test EXIT=8, the same two `FAIL!` lines |
| Step 4b: re-apply fix, rebuild, run regression | `git apply $L/fix.patch`; `CCACHE_DISABLE=1 cmake --build build --target zene ProjectRevisionRestoreStagingTest -j4 > $L/build-4b-fix.log 2>&1`; `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure > $L/test-4b-green.log 2>&1` | apply EXIT=0; build EXIT=0; test EXIT=0 |
| Gates: the wrapper could not run (see below) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $L/feature-check-1.log 2>&1` | EXIT=1: `PermissionError` creating `zene-qwen-new-p16-176-feature-check` next to the worktree, outside the write-jail. No gate ran. |
| Gate, fork-sources (same command the wrapper runs) | `bash tests/fork-sources-gate.sh > $L/gate-fork-sources.log 2>&1` | EXIT=0 |
| Gate, all-sources-reproduce | `bash tests/all-sources-reproduce.sh > $L/gate-all-sources.log 2>&1` | EXIT=0 |
| Gate, no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > $L/gate-no-upstream.log 2>&1` | EXIT=0 |
| Gate, check-namespace | `tests/scripted/check-namespace > $L/gate-check-namespace.log 2>&1` | EXIT=0 |
| Gate, no-tautology | `bash tests/no-tautology-gate.sh > $L/gate-no-tautology.log 2>&1` | EXIT=0 |
| Gate, doc-figures | `python3 tests/doc-figures-gate.py > $L/gate-doc-figures.log 2>&1` | EXIT=0 |
| Ratchet, complexity (raw) | `bash tests/complexity-gate.sh --check > $L/gate-complexity.log 2>&1` | raw EXIT=1 (red on the train, as the brief says); 7 `REGRESSION:` lines, none in the three files changed here |
| Ratchet, file-length (raw) | `bash tests/file-length-gate.sh --check > $L/gate-file-length.log 2>&1` | raw EXIT=1 (red on the train); 19 `REGRESSION:` lines, none in the files changed here |
| Ratchet comparison, as the wrapper defines it | Python (`-I`) reads `$L/gate-*.log`, drops lines whose key is in the cached base listing `gate-baselines/cf3618e61a61a44554f4fbac3be29a1027e3a776.json`, and keeps items in the changed files | new items in my files: complexity 0, file-length 0 → both rows PASS under the wrapper rule |

Gate outcome: the six simple gates each exit 0 and the two ratchets have no new item in the changed files. The wrapper itself could not run because it writes its logs to a sibling of the worktree, which is outside the write-jail. I ran the same commands it runs, from the worktree, and did not change the wrapper, the gate limits or the baselines.

Changed files, committed together with this report:

- `src/core/ProjectRevisions.cpp`: new helper `stageRestoreTarget`; `restoreProjectRevision` calls it.
- `include/ProjectRevisions.h`: `restoreProjectRevision` doc now states the refusal.
- `tests/src/core/ProjectRevisionRestoreStagingTest.cpp`: three new cases and two small helpers.
- `docs/qwen-fixes-2026-10/cluster-new-p16-176.md`: this report.

Observations, not changed here:

- `ControlCommandsProjectFiles.cpp:353` maps every failure of `restoreProjectRevision` to `ErrorKind::NotFound`, so the new refusal reports the same way as the existing rotation refusal. Changing that would alter behaviour outside this bug.
- A restore of `rev0` (or any slot with an oversized `rev(n-1)`) still fails in the rotation's shift after staging, and the stage is kept on purpose so a retry can find it. That is existing behaviour and is unchanged.

## Self-check

- **The regression test fails with the change reverted and passes with it.** Reverted: `test-4b-red.log`, `ctest -R '^ProjectRevisionRestoreStagingTest$'` EXIT=8, with `FAIL!` on `anOversizedRevisionIsRefusedBeforeTheRetainedSetMoves` and `anOversizedPendingStageIsNotInstalled`. Re-applied: `test-4b-green.log` EXIT=0, 8 of 8 cases passed in `test-green-2-verbose.log`. The `Revision` family (`RevisionTimelineTest`, `ProjectRevisionRestoreStagingTest`) EXIT=0, 2/2. The full command list with exit codes is in the table above.
- **Deterministic.** No clock, sleep or thread is involved. Each case uses a `QTemporaryDir`, fixed byte patterns (`documentWith(n)`, `bytesOfSize`) and a fixed stage name (`.restore.4c0c55e0-…`), so the test controls the exact file the restore sees. The seam is the file contents on disk; no injected timestamp or hook is needed, because the decision is a pure size comparison.
- **Fix at the cause; callers and siblings checked.** The check is in `stageRestoreTarget` (`src/core/ProjectRevisions.cpp:175`), which every path into `restoreProjectRevision` uses. The callers are `ControlCommandsRevisions.cpp:152,194`, `ControlCommandsProjectFiles.cpp:353,372,395` and `RevisionTimeline.cpp:415`. The one other user of the install step, `replaceProjectWithStaged` (`RevisionTimeline.cpp:454`), reads its bytes through `readFileBytes(…, Bounds::MaxRevisionBytes)` (`RevisionTimeline.cpp:391`, `:169-173`), so it was already capped. The rotation's own cap (`ProjectRevisions.cpp:119`, `:282`) is unchanged and consistent with the new check (`>` against the same `ProjectRevisionPolicy::MaxRevisionBytes`).
- **No collateral damage.** Only the restore path changed. Stored `.revN` files and the save path are not touched, so saved projects and revision files load as before. The limit is the one the header already states: `include/ProjectRevisions.h:62-68` says a single revision is never larger than `MaxRevisionBytes`. A revision of exactly the cap still restores (`aRevisionExactlyAtTheCapStillRestores`, which passes before and after). The four existing restore-staging tests and `RevisionTimelineTest` pass with the change (family run above).
- **Verdict basis.** The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so there is no "already fixed" claim to support. The contract the fix enforces is the one quoted above from `include/ProjectRevisions.h:62-68`.
- **Gate row (`feature_check.py`), not satisfied here.** The wrapper writes its logs to `<worktree>/../zene-qwen-new-p16-176-feature-check`, which is outside the write-jail, so it stops with `PermissionError` before any gate runs (`feature-check-1.log`, EXIT=1). `submit_check.py` runs the same wrapper, so its gate row reports FAIL for that reason only. I ran the wrapper's own gate commands from the worktree instead (table above): the six simple gates exit 0, and the two ratchets have no new item in the changed files. The wrapper itself needs a log location the sandbox allows; I did not change it or its limits.
