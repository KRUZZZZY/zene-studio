# Cluster new-p9-478: Pool destruction abandons queued tasks

## Verdict

**FIXED.** The finding is real. `ThreadPool::run()` previously stopped as soon as `m_done` became true, without checking whether queued work remained. It now stops only when the queue is empty, so workers finish queued tasks before exiting.

## Evidence

- [src/core/ThreadPool.cpp:62](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-478/src/core/ThreadPool.cpp:62) keeps workers in the loop during shutdown; line 67 waits for queued work or shutdown, and line 69 exits only when the queue is empty.
- [tests/src/core/ThreadPoolTest.cpp:48](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-478/tests/src/core/ThreadPoolTest.cpp:48) blocks the sole worker, queues a second task, waits until destruction marks the pool done, then releases the worker and checks the queued task's future and side effect.

## Commands and results

Build and test output was redirected to new files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-478.r1/logs`. CTest commands ran from `build/tests`.

| Purpose | Command | Exit | Log |
| --- | --- | ---: | --- |
| Initial target lookup, before the build files refreshed | `cmake --build build --target ThreadPoolTest` | 2 | `build-1.log` |
| Refresh/build existing executable | `cmake --build build --target zene` | 0 | `build-2.log` |
| First test compile attempt, using an int-returning task | `cmake --build build --target ThreadPoolTest` | 2 | `build-3.log` |
| Compile baseline regression test | `cmake --build build --target ThreadPoolTest` | 0 | `build-4.log` |
| **RED, original worker code** | `ctest -R ThreadPoolTest --output-on-failure` | **8** | `test-1.log` |
| Build fix and test | `cmake --build build --target zene ThreadPoolTest` | 0 | `build-5.log` |
| **GREEN, fixed worker code** | `ctest -R ThreadPoolTest --output-on-failure` | **0** | `test-2.log` |
| Save production patch for reversal check: `git diff -- src/core/ThreadPool.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-478.r1/logs/fix.patch` | 0 | — |
| Reverse production patch: `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-478.r1/logs/fix.patch` | 0 | — |
| Rebuild reversed code | `cmake --build build --target zene ThreadPoolTest` | 0 | `build-6.log` |
| **RED, after reversing only the production patch** | `ctest -R ThreadPoolTest --output-on-failure` | **8** | `test-3.log` |
| Restore production patch: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-478.r1/logs/fix.patch` | 0 | — |
| Rebuild restored code | `cmake --build build --target zene ThreadPoolTest` | 0 | `build-7.log` |
| **GREEN, after restoring the production patch** | `ctest -R ThreadPoolTest --output-on-failure` | **0** | `test-4.log` |
| Complexity ratchet: `bash tests/complexity-gate.sh --check` | **0** | `gate-1.log` |
| File-length ratchet: `bash tests/file-length-gate.sh --check` | **0** | `gate-2.log` |
| Target listing used to verify test target registration | `cmake --build build --target help` | 0 | `build-8.log` |

The affected ctest family ran **1 test** in each run. Both RED runs failed on the expected broken-promise path; both GREEN runs passed. The first test compile attempt used a non-void task and failed to instantiate the existing `enqueue` template; the regression uses a void task.

The session did not expose an `fff` grep tool (`command -v fff` returned 1), so the logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files in this commit

- `src/core/ThreadPool.cpp`
- `tests/src/core/ThreadPoolTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-478.md`
