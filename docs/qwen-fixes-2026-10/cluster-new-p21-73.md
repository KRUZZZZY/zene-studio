Verdict: FALSE-POSITIVE

Cluster new-p21-73: "FollowPlan::clipLengthTicks may be read before initialization" (src/core/ControlCommandsSessionSceneFollow.cpp:69)

## Evidence

The finding's premise is that `FollowPlan` does not value-initialize `clipLengthTicks`. It does:

- [include/SessionFollow.h:100](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-73/include/SessionFollow.h:100): `tick_t clipLengthTicks = 0;` (default member initializer in `struct FollowPlan`, lines 91-111).
- The only caller of the flagged loop, `scenePlanFrom`, receives a freshly default-constructed local: [src/core/ControlCommandsSessionSceneFollow.cpp:107](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-73/src/core/ControlCommandsSessionSceneFollow.cpp:107) (`FollowPlan plan;`). The default member initializer applies, so `clipLengthTicks` is 0 before the first `std::max` at [ControlCommandsSessionSceneFollow.cpp:69](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-73/src/core/ControlCommandsSessionSceneFollow.cpp:69). The finding's suggested `plan->clipLengthTicks = 0` before the loop would be a no-op.
- The initializer is not a later patch: `git log -S'tick_t clipLengthTicks = 0' -- include/SessionFollow.h` shows commit `67b1b2f1fa`, which is an ancestor of this branch's HEAD (`git merge-base --is-ancestor 67b1b2f1fa HEAD` exits 0). That commit introduced `include/SessionFollow.h` with the initializer already present (`git show 67b1b2f1fa -- include/SessionFollow.h`, line 115 of the diff). `git show HEAD:include/SessionFollow.h` line 100 confirms it on HEAD.
- Other `FollowPlan` constructions (`ControlCommandsSessionFollow.cpp:423`, `:538`) use the same default-construction path, so nothing in the tree builds a `FollowPlan` with an indeterminate field.
- The loop computes a max over per-track `loopLength()` values starting from 0. A negative `loopLength()` (`SessionModel.h:171`, `int`) also yields 0 from the max, which `followActionTicks` (`SessionFollow.h:176`) treats as "fall back to one bar". No wrong value results.

Behaviour check on this tree: the existing scene-follow test family passes, which exercises `session.scene_follow_set`'s plan path.

## Decision

No product change. A fix would be a no-op, and the requested regression test cannot fail on this tree: a test asserting `FollowPlan{}.clipLengthTicks == 0` passes with or without the (already present) fix, so it would not be regression proof. No test was added and no product file was changed.

## Commands run

Every command is listed with its EXIT code; logs are in the LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-73.r1/logs`.

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target SessionSceneFollowTest -j4 > build-1.log 2>&1` | 2 | build-1.log: ccache could not create a temp file under `/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (Permission denied, outside the write-jail). Not a code error. |
| 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp cmake --build build --target SessionSceneFollowTest -j4 > build-2.log 2>&1` | 0 | build-2.log: test built with the ccache temp dir moved into the build cache. |
| 3 | `cd build/tests && ctest -R SessionSceneFollowTest --output-on-failure > test-1.log 2>&1` | 0 | test-1.log: `100% tests passed out of 1` (1 test executed). |
| 4 | `python3 feature_check.py <worktree>` (as given) | 1 | gates-1.log: `PermissionError` creating `zene-qwen-new-p21-73-feature-check`, a sibling of the worktree outside the write-jail. Gate not run. |
| 5 | `python3 -I feature_check_logpath.py <worktree>` | 0 | gates-2.log: `RESULT: PASS` (table below). |

`feature_check_logpath.py` is a copy of `feature_check.py` in the LOG FOLDER that changes only two anchors: `ROOT` points at the original qwen directory (so `FIX-BASE` and `gate-baselines` resolve as before), and the log directory points into the LOG FOLDER instead of the sibling directory. No check, limit or baseline is changed.

Gate table (gates-2.log):

```
feature-check zene-qwen-new-p21-73 (base 37eaf0755, 0 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

Evidence table:

| Item | Result |
| --- | --- |
| RED command / EXIT | Not applicable: no product fix, so no regression test was written and no RED or GREEN cycle was run (step 4b not applicable). |
| GREEN command / EXIT | Not applicable (see above). |
| Affected-family test count | `SessionSceneFollowTest`: 1 executed, 1 passed (test-1.log, EXIT 0). |
| Feature gate EXIT | 0 (`RESULT: PASS`, gates-2.log). |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p21-73.md` only. |

## Self-check

- **Regression test fails with the product change reverted and passes with it (exit codes):** not applicable. There is no product change, so there is nothing to revert. The existing `SessionSceneFollowTest` passes on this tree (test-1.log, EXIT 0, 1/1). A test that only asserts the initializer would pass with or without the fix, so it would not be regression proof.
- **Test is deterministic:** not applicable; no test was added or changed.
- **Fix at the cause and complete:** not applicable; no change. The cause is already covered by the default member initializer at `include/SessionFollow.h:100`, and the caller that builds the plan (`ControlCommandsSessionSceneFollow.cpp:107`) and the other `FollowPlan` constructions (`ControlCommandsSessionFollow.cpp:423`, `:538`) all default-construct it.
- **No collateral damage:** no product file changed, so saved projects, command behaviour and limits are untouched.
- **FALSE-POSITIVE evidence (file:line / contract):** `include/SessionFollow.h:100` (`tick_t clipLengthTicks = 0;`), present since the file was added in `67b1b2f1fa`, an ancestor of HEAD. The read at `ControlCommandsSessionSceneFollow.cpp:69` therefore never sees an indeterminate value.
- **Submit-check gate item:** `submit_check.py` runs the stock `feature_check.py`, which fails with `PermissionError` on the sibling log directory outside the write-jail (gates-1.log, EXIT 1). The same checks on the path-redirected copy pass (gates-2.log, EXIT 0). This is a harness-path issue, not a code failure; it needs the owner to let the gate write its log folder or to move that log path.
