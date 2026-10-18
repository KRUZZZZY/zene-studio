# Cluster new-p10-499: Full worker queue silently drops audio jobs

**Verdict: BLOCKED** — the fix is implemented and the regression is green, but both required ratchet gates fail on findings outside the changed files. I did not change or re-anchor either baseline.

## File evidence

| File and lines | Evidence |
|---|---|
| [`AudioEngineWorkerThread.cpp`](../../src/core/AudioEngineWorkerThread.cpp#L114) | `JobQueue::addJob()` now processes an overflow job synchronously inside `RealtimeScope`; it increments `m_itemsDone` only after `process()` returns. |
| [`RenderJobQueueTest.cpp`](../../tests/src/core/RenderJobQueueTest.cpp#L482) | `queueOverflowRunsEveryJob()` queues 8,193 distinct jobs and checks every job ran once. It failed before the fix and after reversing the production patch. |
| [`RoutingGraphScheduleTest.cpp`](../../tests/src/core/RoutingGraphScheduleTest.cpp#L432) | The overflow instrumentation test now describes and checks the synchronous fallback. |
| [`upstream-modifications.txt`](../../tests/upstream-modifications.txt#L2428) | Records the inherited production and test file changes. |

## Build and test evidence

Build commands (all exited 0):

- `cmake --build build --target zene RenderJobQueueTest` — `build-1.log`.
- `cmake --build build --target zene RenderJobQueueTest RoutingGraphScheduleTest` — `build-2.log` through `build-5.log`; this rebuilt `zene` and both affected test targets before each subsequent CTest run.

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-499.r1/logs`.

| Stage | Exact CTest command and working directory | Exit | Result |
|---|---|---:|---|
| Initial RED | From `build/tests`: `ctest -R '^RenderJobQueueTest$' --output-on-failure` | 8 | 1/1 CTest target ran; `queueOverflowRunsEveryJob()` failed on the last job. Log: `test-1.log`. |
| GREEN family | From `build/tests`: `ctest -R '^(RenderJobQueueTest|RoutingGraphScheduleTest)$' --output-on-failure` | 0 | 2/2 CTest targets passed on both runs. Logs: `test-2.log`, `test-3.log`. |
| Reverse-patch RED | After reversing only the production patch and rebuilding, from `build/tests`: `ctest -R '^RenderJobQueueTest$' --output-on-failure` | 8 | 1/1 target ran; the same regression failed. Log: `test-4.log`. |
| Restored GREEN | After restoring the production patch and rebuilding, from `build/tests`: `ctest -R '^RenderJobQueueTest$' --output-on-failure` | 0 | 1/1 target passed. Log: `test-5.log`. |

Negative-control commands, with observed exits:

- `git diff -- src/core/AudioEngineWorkerThread.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-499.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-499.r1/logs/fix.patch` — exit 0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-499.r1/logs/fix.patch` — exit 0.

## Required gates

| Gate command | Exit | Evidence |
|---|---:|---|
| `bash tests/complexity-gate.sh --check` | 1 | Final log `gate-7.log` reports six unbaselined over-target functions in unrelated files, including `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, and `ProjectRevisions.cpp`. Neither changed function appears in the findings. |
| `bash tests/file-length-gate.sh --check` | 1 | Final log `gate-8.log` reports 19 over-limit files, including unbaselined `SmfInterchangeReader.cpp`, `TakeLane.cpp`, and unrelated test files. None of the changed files appears in the findings. |

The available tool list and shell did not provide the requested `fff` grep tool/command, so log checks used single-term `rg` searches against the required log folder.

## Files in the change

- `src/core/AudioEngineWorkerThread.cpp`
- `tests/src/core/RenderJobQueueTest.cpp`
- `tests/src/core/RoutingGraphScheduleTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-499.md`

Commit message: `fix: cluster new-p10-499 full worker queue skips audio jobs`
