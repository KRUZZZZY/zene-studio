# Cluster new-p9-219: Windows semaphore handle

Verdict: **FIXED**

`Semaphore` now stores the handle returned by `CreateSemaphore` in `m_sem` before checking for creation failure. The existing `post`, `wait`, `tryWait`, and destructor calls therefore use the created handle.

## Evidence

- [src/core/LmmsSemaphore.cpp:79](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-219/src/core/LmmsSemaphore.cpp:79) assigns the returned handle; line 80 checks that stored handle for failure.
- [tests/src/core/AudioEngineTeardownTest.cpp:121](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-219/tests/src/core/AudioEngineTeardownTest.cpp:121) adds `windowsSemaphoreUsesCreatedHandle`. On Windows it exercises wait, try-wait, and post against the public `Semaphore` API. On other hosts it checks that the Windows constructor branch stores the `CreateSemaphore` result.
- [tests/upstream-modifications.txt:1054](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-219/tests/upstream-modifications.txt:1054) records the inherited production change and regression test.

This run was on Linux, so the source-contract assertion ran; the test's direct Win32 API path runs on Windows. An independent test target was unavailable in the preconfigured build (`cmake --build build --target zene LmmsSemaphoreWindowsTest`, exit 2: no such build target). No configure command was run; the regression uses the already registered `AudioEngineTeardownTest` target.

The RED and GREEN commands below were run from `build/tests`; each ctest invocation executed 1/1 registered test in the affected family.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED, original code | `ctest -R AudioEngineTeardownTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-219.r1/logs/test-1.log 2>&1` | 8 | `windowsSemaphoreUsesCreatedHandle` failed because the Windows branch did not assign the returned handle. |
| GREEN, fix applied | `ctest -R AudioEngineTeardownTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-219.r1/logs/test-2.log 2>&1` | 0 | 1/1 passed. |
| RED, fix reverted | `ctest -R AudioEngineTeardownTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-219.r1/logs/test-3.log 2>&1` | 8 | The same assertion failed after rebuilding without the product patch. |
| GREEN, patch reapplied | `ctest -R AudioEngineTeardownTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-219.r1/logs/test-4.log 2>&1` | 0 | 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

Each test run was preceded by a successful `cmake --build build --target zene AudioEngineTeardownTest` build (exit 0); logs are `build-2.log` through `build-5.log` in the run log folder. The required gate runs also exited 0 and were logged as `gate-1.log` and `gate-2.log`.

The fix proof used these commands, both exit 0:

```sh
git diff -- src/core/LmmsSemaphore.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-219.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-219.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-219.r1/logs/fix.patch
```

The requested `fff` log-search tool was not exposed in this runtime; saved logs were searched with term-specific `rg` queries.

## Files in the fix commit

- `src/core/LmmsSemaphore.cpp`
- `tests/src/core/AudioEngineTeardownTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-219.md`
