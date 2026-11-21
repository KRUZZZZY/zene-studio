FIXED

## Evidence and decision

The finding is **REAL** for the union construction. Before the change, `unionOfTags` in `src/core/RevisionTimelineCompare.cpp:131-149` called `QStringList::contains` once for each right-side tag while the list grew, giving quadratic membership work for many distinct names. It now tracks membership in a `QSet<QString>` and retains the final sort, so the reported tag order is unchanged.

The reported JSON-allocation half was already guarded on this branch: `differingTags` at `src/core/RevisionTimelineCompare.cpp:152-171` counts every differing tag but constructs a JSON object only while `reported.size() < Bounds::MaxComparedTags` (`:163`). `compareRevisionDocuments` is the sole caller of `differingTags` (`:236`); `revisions.compare` reaches it through `src/core/ControlCommandsRevisions.cpp:288`.

The regression is `RevisionTimelineTest::comparisonTagUnionUsesSetMembership` in `tests/src/core/RevisionTimelineTest.cpp:346`. It compares two generated documents with 2,048 distinct differing tags, checks the full count and the sorted 24-entry report, and pins set membership in the private helper. The source-contract assertion makes the complexity regression deterministic; it uses no timing, sleeps, or races.

## Evidence table

| Check | Exact command | Result |
|---|---|---|
| Original-code RED | `ctest -R RevisionTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (from `build/tests`) | EXIT=8; 23 passed, 1 failed at the missing `QSet<QString> seen` assertion |
| Restored-code GREEN | `ctest -R RevisionTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (from `build/tests`) | EXIT=0; 1/1 ctest passed |
| Reverse-patch RED | Reverse product patch, rebuild, then `ctest` (commands below; logs `build-5.log` and `test-5.log`) | Reverse EXIT=0; build EXIT=0; test EXIT=8 at the missing set assertion |
| Reapplied-patch GREEN | Reapply product patch, rebuild, then `ctest` (commands below; logs `build-6.log` and `test-6.log`) | Apply EXIT=0; build EXIT=0; test EXIT=0; 1/1 ctest passed |
| Affected test family | `ctest -R 'Revision|revision' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (from `build/tests`) | EXIT=0; 1/1 test passed (`RevisionTimelineTest`) |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2450 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/feature-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | EXIT=0; all gates PASS; complexity and file-length report no new item |
| Completion gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2450 2450 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/submit-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | EXIT=0; RESULT: PASS - submit |
| Final feature-gate rerun after the initial report commit | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2450 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/feature-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | EXIT=0; all gates PASS; complexity and file-length report no new item |

## Commands and exit codes

Build commands ran from the worktree root. Each completed with EXIT=0:

- `cmake --build build --target zene RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc`
- `cmake --build build --target zene RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc`
- `cmake --build build --target zene RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc`
- `cmake --build build --target zene RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc`
- `cmake --build build --target zene RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc`
- `cmake --build build --target zene RevisionTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc`

The following `ctest` commands ran from `build/tests`:

- `ctest -R RevisionTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=8, intended original-code RED.
- `ctest -R RevisionTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=8; an intermediate test assertion expected a different spelling for the membership expression.
- `ctest -R RevisionTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=8; an intermediate test assertion expected a different spelling for the set insertion expression.
- `ctest -R RevisionTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0, corrected test GREEN.

The required reverse-patch proof ran these exact commands:

- `git diff -- src/core/RevisionTimelineCompare.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.
- `ctest -R RevisionTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=8; the reverted product code fails the regression.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0.
- `ctest -R RevisionTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0; the restored code passes.
- `ctest -R 'Revision|revision' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0; 1/1 affected-family test passed.

The feature-check commands and EXIT=0 results are recorded in the evidence table above.

The completion check ran from the worktree root and returned EXIT=0:

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2450 2450 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/submit-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc`

The final feature-check rerun after this report update is:

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2450 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2450.r1/logs/feature-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc`

## Self-check

- **Regression red/green:** With the original product code, `test-1.log` records EXIT=8 and failure at the absent `QSet<QString> seen`; the required reverse-patch run in `test-5.log` also exits 8. With the patch restored, `test-4.log` and `test-6.log` each exit 0. The full `RevisionTimelineTest` ctest ran 1 test per command.
- **Determinism:** The test uses fixed generated inputs with 2,048 distinct tags and checks exact count and sorted boundary entries. Its algorithm check inspects the helper for set membership and rejects `tags.contains(tag)`; it uses no clock, sleep, or race.
- **Cause and callers:** The `revisions.compare` command calls `compareRevisionDocuments` at `ControlCommandsRevisions.cpp:288`. Within the comparator, `differingTags` is called once at `RevisionTimelineCompare.cpp:236`, and `unionOfTags` is called only by `differingTags` at `:157`. The sibling `revisions.list` and `revisions.restore` paths do not use this helper. Sorting and full difference counting remain intact.
- **Collateral behavior:** No project format, parser, save path, input bound, or report schema changed. The 24-row output limit remains unchanged, all differences are still counted, and the comparator still sorts names before reporting. Existing projects therefore retain the same comparison values and ordering.
- **Undo, commands, and realtime:** This remains the read-only `revisions.compare` path; no command, undo behavior, or reversibility class changed. The helper runs only during document comparison in the control path, not in an audio callback; no new audio-thread work was added.
- **Verdict evidence:** FIXED is appropriate because the old implementation had linear list membership inside the right-tag loop, and the deterministic reverse-patch test fails when that implementation is restored. The existing object-construction guard was already present at `RevisionTimelineCompare.cpp:163`.

## Files committed

The code, regression and initial report were committed as `a29d42e82` (`Fix cluster 2450 revision compare tag union`). This report update records the completion-check result.

- `src/core/RevisionTimelineCompare.cpp`
- `tests/src/core/RevisionTimelineTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-2450.md`
