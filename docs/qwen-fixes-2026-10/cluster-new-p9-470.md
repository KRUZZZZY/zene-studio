# Cluster new-p9-470

Verdict: FIXED

Queued cancellation erased the job from `m_queue` but kept the `Job` in `m_jobs`, retaining its source buffer. `StemJobManager::cancel()` now resets `job->mix` in the queued branch while holding the queue mutex, after removing the job and before publishing `Cancelled`.

The regression in `tests/src/core/StemJobManagerTest.cpp:228-239` moves the only strong reference into the queued job, verifies the buffer stays alive while queued, and verifies it is released synchronously by cancellation. It failed on the original implementation and failed again when the production patch was reversed.

File evidence: `src/core/StemJobManager.cpp:179`; `tests/src/core/StemJobManagerTest.cpp:228-239`; inherited-file rationale updated at `tests/upstream-modifications.txt:669`.

## Verification

Commands below ran in the worktree unless a different working directory is stated. Build and test output was captured in the listed log files; each shell invocation captured `$?` and printed the shown `EXIT` value.

| Stage | Exact command | Log | Exit |
|---|---|---|---:|
| Build regression before fix | `cmake --build build --target StemJobManagerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/build-1.log 2>&1` | `build-1.log` | 0 |
| Original-code RED (run from `build/tests`) | `ctest -R '^StemJobManagerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/test-1.log 2>&1` | `test-1.log` | 8 |
| Build fixed code | `cmake --build build --target zene StemJobManagerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/build-2.log 2>&1` | `build-2.log` | 0 |
| Fixed GREEN (run from `build/tests`) | `ctest -R '^StemJobManagerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/test-2.log 2>&1` | `test-2.log` | 0 |
| Remove production patch | `git diff -- src/core/StemJobManager.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/fix.patch` | `fix.patch` | 0 |
| Rebuild without fix | `cmake --build build --target zene StemJobManagerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/build-3.log 2>&1` | `build-3.log` | 0 |
| Reversed-patch RED (run from `build/tests`) | `ctest -R '^StemJobManagerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/test-3.log 2>&1` | `test-3.log` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/fix.patch` | `fix.patch` | 0 |
| Rebuild restored fix | `cmake --build build --target zene StemJobManagerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/build-4.log 2>&1` | `build-4.log` | 0 |
| Final GREEN (run from `build/tests`) | `ctest -R '^StemJobManagerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/test-4.log 2>&1` | `test-4.log` | 0 |
| Verbose family count (run from `build/tests`) | `ctest -V -R '^StemJobManagerTest$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/test-5.log 2>&1` | `test-5.log` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/gate-3.log 2>&1` | `gate-3.log` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-470.r1/logs/gate-4.log 2>&1` | `gate-4.log` | 0 |

The affected ctest family ran 1/1 registered test target. The final verbose QtTest run reported 15 passed and 0 failed. Both RED runs failed at `queuedMixRef.expired()`; the fixed runs passed.

## Files committed

- `src/core/StemJobManager.cpp`
- `tests/src/core/StemJobManagerTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-470.md`
