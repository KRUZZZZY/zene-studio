Verdict: FALSE-POSITIVE

Cluster new-p23-21: `queued()` dereferences `queue().job(id)` after `add()`. On this branch that lookup cannot return null, because nothing between the successful `add()` and the lookup can remove the job from the queue. No product code changed, so there is no regression test and no RED/GREEN pair.

## Evidence

The finding's line 128 is still `return ControlResult::success(jobJson(*queue().job(id)));` at [ControlCommandsRenderQueue.cpp:128](src/core/ControlCommandsRenderQueue.cpp:128). The code between the add and the lookup is:

1. [ControlCommandsRenderQueue.cpp:121](src/core/ControlCommandsRenderQueue.cpp:121) `const int id = queue().add(job);` and the `id < 0` refusal at [:122-126](src/core/ControlCommandsRenderQueue.cpp:122). A non-negative id is returned only after `m_jobs.push_back(job)` at [RenderQueue.cpp:309](src/core/RenderQueue.cpp:309).
2. [ControlCommandsRenderQueue.cpp:127](src/core/ControlCommandsRenderQueue.cpp:127) `if (start) { queue().start(); }`. `start()` is `if (!isRunning()) { next(); }` at [RenderQueue.cpp:327-330](src/core/RenderQueue.cpp:327). `next()` at [RenderQueue.cpp:332](src/core/RenderQueue.cpp:332) runs one job through `runRender`, `runBounce` or `runConvert`, and those only start a process, enqueue work, or finish synchronously. None of them erases from `m_jobs`.
3. The synchronous finish path is `finish()` at [RenderQueue.cpp:406-417](src/core/RenderQueue.cpp:406). It calls `find()`, `endJob()` and `TaskCenter::finish()`, and changes the job state only. `TaskCenter::finish()` at [TaskCenter.cpp:173-176](src/core/TaskCenter.cpp:173) goes through `onMainThread()` ([TaskCenter.cpp:75-83](src/core/TaskCenter.cpp:75)). On the main thread that runs `doFinish()` at once, and `doFinish()` touches only TaskCenter's own entries.

The only code that removes render jobs is `RenderQueue::clearFinished()` at [RenderQueue.cpp:460-469](src/core/RenderQueue.cpp:460), with `m_jobs.erase(...)` at [:465-468](src/core/RenderQueue.cpp:465). Its only product caller is the `render.queue_clear` handler at [ControlCommandsRenderQueue.cpp:381](src/core/ControlCommandsRenderQueue.cpp:381), a separate command dispatched from the control socket. That handler cannot run in the middle of `queued()`. A grep for `m_jobs` in `RenderQueue.cpp` (lines 303-469) shows no other erase or clear.

So `queue().job(id)` after `add()` returns the job the add just pushed. The `*` dereference is safe on every path that reaches it.

The bounce sibling at [ControlCommandsRenderBounce.cpp:506-518](src/core/ControlCommandsRenderBounce.cpp:506) null-checks `queue.job(id)` and falls back to the state `"queued"`. That guard is defensive, and the same invariant applies to it. The render path does not need the same guard today.

## Commands and results

Run from the worktree; logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-21.r1/logs/`.

| Step | Command | EXIT | Note |
| --- | --- | --- | --- |
| Build attempt 1 | `cmake --build build --target zene ControlRenderJobTest RenderSourceGuardTest -j4 > logs/001-build-baseline.log 2>&1` | 2 | ccache could not create its temp file under the sandbox's `dsh/home/ccache-tmp-r6` path ("Permission denied"). This is an environment problem; no compile error. |
| Build attempt 2 | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p23-21 cmake --build build --target zene ControlRenderJobTest RenderSourceGuardTest -j4 > logs/002-build-baseline.log 2>&1` | 0 | Same targets with a writable ccache temp dir. |
| Test family, first run | `ctest -R "ControlRenderJobTest\|RenderSourceGuardTest\|TaskCenterTest" -j4 --output-on-failure > logs/003-ctest-baseline.log 2>&1` from `build/tests` | 8 | `TaskCenterTest` was "Not Run" because its binary was not built. The two built tests passed. |
| Build TaskCenterTest | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p23-21 cmake --build build --target TaskCenterTest -j4 > logs/004-build-taskcenter.log 2>&1` | 0 | |
| Test family, baseline | `ctest -R "ControlRenderJobTest\|RenderSourceGuardTest\|TaskCenterTest" -j4 --output-on-failure > logs/005-ctest-baseline.log 2>&1` from `build/tests` | 0 | 3/3 passed on the unchanged branch. |

No RED/GREEN run was made: no product file was changed, so the step 4b patch round-trip has nothing to apply. The regression test was not written for the same reason. A test that tries to hit the null branch would need a seam that changes the queue, and that seam would be a behaviour change the finding does not justify.

Optional follow-up, not done here: the render path could take the same null-check and fallback as the bounce path. That would only harden an invariant that is not currently broken.

## Gates

`feature_check.py` could not run from this sandbox. It writes its logs to `<worktree>-feature-check` next to the worktree, under `~/Documents/AI_KOS_PROJECT/projects/zene-studio-program/`, which is outside the write-jail. Its first step fails with `PermissionError` (log `006-feature-check.log`, EXIT=1). The script was not changed. Each gate it runs was run directly from the worktree instead, with the same commands:

| Gate | Command (run from the worktree via `bash -c`) | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `010-gate-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `011-gate-all-sources-reproduce.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `012-gate-no-upstream-regression.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `013-gate-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `013-gate-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `014-gate-doc-figures.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `015-gate-complexity.log` |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `016-gate-file-length.log` |

The two ratchets exit 1 on items in files this branch does not touch. Their 29 `REGRESSION:` lines name `src/` and `tests/` files such as `tests/src/core/VideoTrackTest.cpp (694)`. None names `ControlCommandsRenderQueue.cpp`, `RenderQueue.cpp` or the new doc. Under `feature_check.py`'s rule (fail only for items in changed files), those rows would pass. I could not reproduce the base-listing comparison: its cache is written to the qwen directory, outside the jail.

An earlier pass of the same loop ran into a zsh word-splitting problem and wrote EXIT=127 files: `014-gate-no-tautology.log`, `015-gate-doc-figures.log`, `016-gate-complexity.log` and `017-gate-file-length.log`. They are superseded by the logs listed above.

## Self-check

- **Regression test FAILS without the fix and PASSES with it:** not applicable. No product file was changed and no regression test was added, because the verdict is FALSE-POSITIVE. No RED or GREEN run exists. The `git apply -R` round trip in step 4b was not run because there is no patch.
- **Deterministic test:** not applicable (no test added).
- **Fix at the cause and complete:** not applicable (no fix). Callers and siblings were checked as evidence: the add, start, next, finish, clearFinished, the `render.queue_clear` handler, and the bounce enqueue path (`ControlCommandsRenderBounce.cpp:506-518`).
- **No collateral damage:** nothing changed in product code, saved data or limits.
- **FALSE-POSITIVE evidence:** the code at [ControlCommandsRenderQueue.cpp:121-128](src/core/ControlCommandsRenderQueue.cpp:121), [RenderQueue.cpp:303-330](src/core/RenderQueue.cpp:303), [RenderQueue.cpp:406-417](src/core/RenderQueue.cpp:406), [RenderQueue.cpp:460-469](src/core/RenderQueue.cpp:460) and [ControlCommandsRenderQueue.cpp:381](src/core/ControlCommandsRenderQueue.cpp:381), as described above.
- **Gates:** `feature_check.py` blocked by the write-jail (see "Gates"). Each of its gates was run directly; six pass, and the two ratchets fail only on items outside this branch's changed files.
- **`submit_check.py`:** run after the commit; its result is in the final report, not in this file.
